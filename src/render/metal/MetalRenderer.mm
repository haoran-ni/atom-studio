#import <Metal/Metal.h>
#include "MetalRenderer.h"
#include "MetalTypes.h"
#include "../common/Camera.h"
#include "../../data/Structure.h"
#include <QDebug>
#include <QMatrix4x4>
#include <array>
#include <cstring>
#include <memory>

namespace atom::render::metal {

static constexpr NSUInteger kPreferredRasterSampleCount = 4;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static simd_float4x4 qMatToSimd(const QMatrix4x4& m) {
    simd_float4x4 result;
    // QMatrix4x4::constData() is column-major, same as simd_float4x4
    std::memcpy(&result, m.constData(), 16 * sizeof(float));
    return result;
}

/// Remap projection matrix from OpenGL depth [-1,1] to Metal depth [0,1].
static simd_float4x4 remapDepthToMetal(const QMatrix4x4& proj) {
    // bias = diag(1, 1, 0.5, 1) with translation (0, 0, 0.5)
    // This maps z from [-1,1] to [0,1]: z_metal = z_gl * 0.5 + 0.5
    QMatrix4x4 bias;
    bias(2, 2) = 0.5f;
    bias(2, 3) = 0.5f;
    return qMatToSimd(bias * proj);
}

// ---------------------------------------------------------------------------
// PIMPL
// ---------------------------------------------------------------------------

struct MetalRenderer::Impl {
    struct OutputSlot {
        uint64_t frameRequestToken = 0;
        id<MTLTexture> msaaColorTexture = nil;
        id<MTLTexture> colorTexture = nil;
        id<MTLTexture> depthTexture = nil;
    };

    id<MTLDevice> device = nil;
    id<MTLCommandQueue> commandQueue = nil;
    NSUInteger rasterSampleCount = 1;
    std::array<OutputSlot, kOutputSlotCount> outputSlots{};
    std::shared_ptr<AsyncFrameState> asyncState = std::make_shared<AsyncFrameState>();
};

MetalRenderer::MetalRenderer()
    : m_impl(std::make_unique<Impl>())
{
}

MetalRenderer::~MetalRenderer() {
    cleanup();
}

void MetalRenderer::setDevice(void* device) {
    m_impl->device = (__bridge id<MTLDevice>)device;
}

bool MetalRenderer::initialize() {
    if (m_initialized) return true;

    if (!m_impl->device) {
        qCritical() << "MetalRenderer: device not set before initialize()";
        return false;
    }

    m_impl->commandQueue = [m_impl->device newCommandQueue];
    if (!m_impl->commandQueue) {
        qCritical() << "MetalRenderer: failed to create command queue";
        return false;
    }

    // Compile shaders and create pipeline states
    m_impl->rasterSampleCount =
        [m_impl->device supportsTextureSampleCount:kPreferredRasterSampleCount]
            ? kPreferredRasterSampleCount
            : 1;
    if (m_impl->rasterSampleCount == 1) {
        qWarning() << "MetalRenderer: 4x MSAA not supported; using single-sample rasterization";
    }

    if (!m_shaderLibrary.initialize((__bridge void*)m_impl->device,
                                    static_cast<int>(m_impl->rasterSampleCount))) {
        return false;
    }

    // Initialize sub-renderers
    void* dev = (__bridge void*)m_impl->device;
    if (!m_sphereRenderer.initialize(dev, &m_shaderLibrary)) return false;
    if (!m_bondRenderer.initialize(dev, &m_shaderLibrary)) return false;
    if (!m_unitCellRenderer.initialize(dev, &m_shaderLibrary)) return false;
    if (!m_gizmoRenderer.initialize(dev, &m_shaderLibrary)) return false;
    if (!m_viewportAxesRenderer.initialize(dev, &m_shaderLibrary)) return false;

    m_initialized = true;
    qInfo() << "MetalRenderer: initialized successfully";
    return true;
}

void MetalRenderer::cleanup() {
    m_sphereRenderer.cleanup();
    m_bondRenderer.cleanup();
    m_unitCellRenderer.cleanup();
    m_gizmoRenderer.cleanup();
    m_viewportAxesRenderer.cleanup();
    m_shaderLibrary.cleanup();

    m_impl->asyncState->generation.fetch_add(1, std::memory_order_relaxed);
    m_impl->asyncState->inFlightSlot.store(-1, std::memory_order_relaxed);
    resetOutputSlots(*m_impl->asyncState);

    for (auto& slot : m_impl->outputSlots) {
        slot.msaaColorTexture = nil;
        slot.colorTexture = nil;
        slot.depthTexture = nil;
    }
    m_impl->commandQueue = nil;
    m_impl->rasterSampleCount = 1;

    m_structure = nullptr;
    m_pendingRender = false;
    m_outputGeneration = m_impl->asyncState->generation.load(std::memory_order_relaxed);
    m_lastPresentedSlot = -1;
    m_initialized = false;
}

void MetalRenderer::resize(int width, int height) {
    if (width == m_width && height == m_height) return;
    if (width <= 0 || height <= 0) return;

    m_width = width;
    m_height = height;
    createRenderTargets();
}

void MetalRenderer::setStructure(const data::Structure* structure) {
    m_structure = structure;
    m_atomDataDirty = true;
    m_bondDataDirty = true;
    m_unitCellDataDirty = true;
}

void MetalRenderer::render(const Camera& camera, const RenderSettings& settings) {
    if (!m_initialized || m_width == 0 || m_height == 0) return;
    if (!m_impl->outputSlots[0].colorTexture || !m_impl->outputSlots[0].depthTexture) return;
    if (m_impl->rasterSampleCount > 1 && !m_impl->outputSlots[0].msaaColorTexture) return;

    // Submit at most one GPU frame at a time. If a frame is still in flight,
    // remember that newer state is waiting so the viewport schedules another
    // attempt; dirty flags stay set and are processed then.
    const int outputSlotIndex = acquireOutputSlot(*m_impl->asyncState, m_lastPresentedSlot);
    if (outputSlotIndex < 0) {
        m_pendingRender = true;
        return;
    }
    m_pendingRender = false;
    m_impl->outputSlots[static_cast<size_t>(outputSlotIndex)].frameRequestToken =
        settings.frameRequestToken;

    // Upload dirty data
    if (m_atomDataDirty) {
        m_sphereRenderer.setAtomData(m_structure);
        m_atomDataDirty = false;
    }
    if (m_bondDataDirty) {
        m_bondRenderer.setBondData(m_structure);
        m_bondDataDirty = false;
    }
    if (m_unitCellDataDirty) {
        m_unitCellRenderer.setUnitCellData(m_structure);
        m_unitCellDataDirty = false;
    }

    // Build shared uniforms
    SceneUniforms uniforms{};
    uniforms.viewMatrix = qMatToSimd(camera.viewMatrix());
    uniforms.projectionMatrix = remapDepthToMetal(camera.projectionMatrix());
    uniforms.viewProjectionMatrix = remapDepthToMetal(camera.viewProjectionMatrix());

    // Light direction: world space → view space for raster shader
    QVector3D worldLightDir = settings.lightDirWorld();
    QVector3D viewLightDir = (camera.viewMatrix() * QVector4D(worldLightDir, 0.0f)).toVector3D().normalized();
    uniforms.lightDir = simd_make_float3(viewLightDir.x(), viewLightDir.y(), viewLightDir.z());
    uniforms.ambient = settings.ambientStrength;
    uniforms.diffuse = settings.diffuseStrength;
    uniforms.specular = settings.specularStrength;
    uniforms.shininess = settings.shininess;
    uniforms.atomScale = settings.atomScale;
    uniforms.bondRadius = settings.bondRadius;
    uniforms.isPerspective = camera.isPerspective() ? 1 : 0;

    // Stroke outlines: world-units-per-pixel factor valid for both projections
    // (P[1][1] = 1/tan(fovY/2) perspective, 2/orthoHeight orthographic).
    const bool outlineOn = settings.outlineEnabled && settings.outlineWidth > 0.0f;
    const float p11 = camera.projectionMatrix()(1, 1);
    uniforms.outlineWidthPx = outlineOn ? settings.outlineWidth : 0.0f;
    uniforms.selectionOutlineWidthPx = 4.0f * std::max(settings.viewportAxesPixelRatio, 1.0f);
    uniforms.outlinePixelScale =
        (p11 > 1e-6f) ? 2.0f / (p11 * static_cast<float>(m_height)) : 0.0f;
    const auto& oc = settings.outlineColor;
    uniforms.outlineColor = simd_make_float4(oc.redF(), oc.greenF(), oc.blueF(), 1.0f);

    // Sphere early-Z: enabled only when the gate proves the near-tangent
    // billboard placement renders identically to the default placement.
    uniforms.sphereEarlyZ =
        (settings.showAtoms &&
         m_sphereRenderer.canUseEarlyZ(camera, settings.atomScale,
                                       std::max(uniforms.outlineWidthPx,
                                                uniforms.selectionOutlineWidthPx),
                                       uniforms.outlinePixelScale)) ? 1 : 0;

    // Create command buffer and render pass
    id<MTLCommandBuffer> cmdBuffer = [m_impl->commandQueue commandBuffer];
    const auto& outputSlot = m_impl->outputSlots[static_cast<size_t>(outputSlotIndex)];

    // Per-bond frame precompute (compute pass, must precede the render pass).
    if (settings.showBonds) {
        m_bondRenderer.encodeFramePrecompute((__bridge void*)cmdBuffer, uniforms);
    }

    const bool needsOverlayPass = settings.showViewportAxes || settings.showRotationCenter;

    MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    passDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
    if (outputSlot.msaaColorTexture) {
        passDesc.colorAttachments[0].texture = outputSlot.msaaColorTexture;
        passDesc.colorAttachments[0].resolveTexture = outputSlot.colorTexture;
        passDesc.colorAttachments[0].storeAction = needsOverlayPass
            ? MTLStoreActionStoreAndMultisampleResolve
            : MTLStoreActionMultisampleResolve;
    } else {
        passDesc.colorAttachments[0].texture = outputSlot.colorTexture;
        passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
    }

    const auto& bg = settings.backgroundColor;
    passDesc.colorAttachments[0].clearColor = MTLClearColorMake(
        bg.redF() * bg.alphaF(), bg.greenF() * bg.alphaF(), bg.blueF() * bg.alphaF(), bg.alphaF());

    passDesc.depthAttachment.texture = outputSlot.depthTexture;
    passDesc.depthAttachment.loadAction = MTLLoadActionClear;
    passDesc.depthAttachment.storeAction = MTLStoreActionDontCare;
    passDesc.depthAttachment.clearDepth = 1.0;

    id<MTLRenderCommandEncoder> encoder =
        [cmdBuffer renderCommandEncoderWithDescriptor:passDesc];

    [encoder setViewport:(MTLViewport){0, 0,
        static_cast<double>(m_width), static_cast<double>(m_height),
        0.0, 1.0}];

    // Render order: unit-cell object -> bonds -> spheres
    if (settings.showUnitCell) {
        m_unitCellRenderer.render((__bridge void*)encoder, uniforms, settings);
    }
    if (settings.showBonds) {
        m_bondRenderer.render((__bridge void*)encoder, uniforms, settings.cylinderSegments);
    }
    if (settings.showAtoms) {
        m_sphereRenderer.render((__bridge void*)encoder, uniforms);
    }

    [encoder endEncoding];

    if (needsOverlayPass) {
        MTLRenderPassDescriptor* overlayPass = [MTLRenderPassDescriptor renderPassDescriptor];
        if (outputSlot.msaaColorTexture) {
            overlayPass.colorAttachments[0].texture = outputSlot.msaaColorTexture;
            overlayPass.colorAttachments[0].resolveTexture = outputSlot.colorTexture;
            overlayPass.colorAttachments[0].loadAction = MTLLoadActionLoad;
            overlayPass.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
        } else {
            overlayPass.colorAttachments[0].texture = outputSlot.colorTexture;
            overlayPass.colorAttachments[0].loadAction = MTLLoadActionLoad;
            overlayPass.colorAttachments[0].storeAction = MTLStoreActionStore;
        }

        overlayPass.depthAttachment.texture = outputSlot.depthTexture;
        overlayPass.depthAttachment.loadAction = MTLLoadActionClear;
        overlayPass.depthAttachment.storeAction = MTLStoreActionDontCare;
        overlayPass.depthAttachment.clearDepth = 1.0;

        id<MTLRenderCommandEncoder> overlayEncoder =
            [cmdBuffer renderCommandEncoderWithDescriptor:overlayPass];

        [overlayEncoder setViewport:(MTLViewport){0, 0,
            static_cast<double>(m_width), static_cast<double>(m_height),
            0.0, 1.0}];

        if (settings.showRotationCenter) {
            float len = camera.viewScale() * 0.03f;
            m_gizmoRenderer.render((__bridge void*)overlayEncoder, uniforms,
                                   settings.rotationCenterX, settings.rotationCenterY,
                                   settings.rotationCenterZ, len, /*depthTest=*/true);
        }

        if (settings.showViewportAxes) {
            m_viewportAxesRenderer.render((__bridge void*)overlayEncoder, camera, settings, m_width, m_height);
        }
        [overlayEncoder endEncoding];
    }

    trackSubmittedFrame((__bridge void*)cmdBuffer, outputSlotIndex, m_outputGeneration);
    [cmdBuffer commit];
}

void MetalRenderer::invalidateAtomData() {
    m_atomDataDirty = true;
}

void MetalRenderer::invalidateBondData() {
    m_bondDataDirty = true;
}

void* MetalRenderer::colorTexture(uint64_t& frameRequestToken) {
    frameRequestToken = 0;
    const int readySlot = m_impl->asyncState->latestReadySlot.load(std::memory_order_acquire);
    if (readySlot < 0 || readySlot >= kOutputSlotCount) {
        return nullptr;
    }

    m_lastPresentedSlot = readySlot;
    frameRequestToken = m_impl->outputSlots[static_cast<size_t>(readySlot)].frameRequestToken;
    return (__bridge void*)m_impl->outputSlots[static_cast<size_t>(readySlot)].colorTexture;
}

bool MetalRenderer::needsMoreFrames() const {
    if (m_pendingRender) {
        return true;
    }
    if (m_impl->asyncState->inFlightSlot.load(std::memory_order_acquire) >= 0) {
        return true;
    }
    // A completed frame that has not been handed to the scene graph yet.
    const int readySlot = m_impl->asyncState->latestReadySlot.load(std::memory_order_acquire);
    return readySlot >= 0 && readySlot != m_lastPresentedSlot;
}

void MetalRenderer::trackSubmittedFrame(void* cmdBuf, int outputSlotIndex, uint64_t generation) {
    auto asyncState = m_impl->asyncState;
    asyncState->slotStates[static_cast<size_t>(outputSlotIndex)].store(
        static_cast<uint8_t>(OutputSlotState::InFlight), std::memory_order_release);
    asyncState->inFlightSlot.store(outputSlotIndex, std::memory_order_release);

    id<MTLCommandBuffer> cmdBuffer = (__bridge id<MTLCommandBuffer>)cmdBuf;
    [cmdBuffer addCompletedHandler:^(id<MTLCommandBuffer> completedBuffer) {
        int expectedSlot = outputSlotIndex;
        asyncState->inFlightSlot.compare_exchange_strong(expectedSlot, -1, std::memory_order_acq_rel);

        if (completedBuffer.status != MTLCommandBufferStatusCompleted ||
            asyncState->generation.load(std::memory_order_acquire) != generation) {
            asyncState->slotStates[static_cast<size_t>(outputSlotIndex)].store(
                static_cast<uint8_t>(OutputSlotState::Free), std::memory_order_release);
            return;
        }

        asyncState->slotStates[static_cast<size_t>(outputSlotIndex)].store(
            static_cast<uint8_t>(OutputSlotState::Ready), std::memory_order_release);
        asyncState->latestReadySlot.store(outputSlotIndex, std::memory_order_release);
    }];
}

void MetalRenderer::createRenderTargets() {
    for (auto& slot : m_impl->outputSlots) {
        // Resolved display texture (single-sample, sampled by Qt scene graph).
        MTLTextureDescriptor* colorDesc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                        width:m_width
                                       height:m_height
                                    mipmapped:NO];
        colorDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        colorDesc.storageMode = MTLStorageModePrivate;
        slot.colorTexture = [m_impl->device newTextureWithDescriptor:colorDesc];

        if (m_impl->rasterSampleCount > 1) {
            MTLTextureDescriptor* msaaColorDesc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                            width:m_width
                                           height:m_height
                                        mipmapped:NO];
            msaaColorDesc.textureType = MTLTextureType2DMultisample;
            msaaColorDesc.sampleCount = m_impl->rasterSampleCount;
            msaaColorDesc.usage = MTLTextureUsageRenderTarget;
            msaaColorDesc.storageMode = MTLStorageModePrivate;
            slot.msaaColorTexture = [m_impl->device newTextureWithDescriptor:msaaColorDesc];
            if (!slot.msaaColorTexture) {
                qCritical() << "MetalRenderer: failed to create MSAA color texture";
            }
        } else {
            slot.msaaColorTexture = nil;
        }

        // Depth texture (matches raster sample count).
        MTLTextureDescriptor* depthDesc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                        width:m_width
                                       height:m_height
                                    mipmapped:NO];
        if (m_impl->rasterSampleCount > 1) {
            depthDesc.textureType = MTLTextureType2DMultisample;
            depthDesc.sampleCount = m_impl->rasterSampleCount;
        }
        depthDesc.usage = MTLTextureUsageRenderTarget;
        depthDesc.storageMode = MTLStorageModePrivate;
        slot.depthTexture = [m_impl->device newTextureWithDescriptor:depthDesc];
        if (!slot.depthTexture) {
            qCritical() << "MetalRenderer: failed to create depth texture";
        }
    }

    // Invalidate any frame still in flight against the old-size textures:
    // bumping the generation makes its completion handler mark the slot Free
    // instead of Ready, so a never-rendered new texture is never presented.
    // inFlightSlot itself drains naturally via that handler.
    m_outputGeneration =
        m_impl->asyncState->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
    resetOutputSlots(*m_impl->asyncState);
    m_lastPresentedSlot = -1;
    m_pendingRender = true;  // latest state must be re-rendered at the new size
}

} // namespace atom::render::metal
