#import <Metal/Metal.h>
#include "MetalRayTracingRenderer.h"
#include "MetalAsyncOutput.h"
#include "MetalBufferUtil.h"
#include "MetalTypes.h"
#include "MetalUnitCellShared.h"
#include "../common/BondRenderData.h"
#include "../common/Camera.h"
#include "../common/BVH.h"
#include "../common/RenderStateHash.h"
#include "../../data/Structure.h"
#include <QDebug>
#include <QMatrix4x4>
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace atom::render::metal {

static constexpr NSUInteger kPreferredOverlaySampleCount = 4;

// Adaptive sample batching: target GPU time per submission and a hard cap on
// samples per command buffer. Keeps the UI responsive while letting fast
// scenes converge far quicker than one sample per vsync.
static constexpr uint64_t kRTGpuBudgetNanos = 30ull * 1000 * 1000;  // 30 ms
static constexpr int kRTMaxSamplesPerSubmit = 256;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static simd_float4x4 qMatToSimd(const QMatrix4x4& m) {
    simd_float4x4 result;
    std::memcpy(&result, m.constData(), 16 * sizeof(float));
    return result;
}

// ---------------------------------------------------------------------------
// PIMPL
// ---------------------------------------------------------------------------

struct MetalRayTracingRenderer::Impl {
    struct OutputSlot {
        uint64_t frameRequestToken = 0;
        id<MTLTexture> msaaOutputTexture = nil;
        id<MTLTexture> outputTexture = nil;
        id<MTLTexture> msaaOverlayDepthTexture = nil;
        id<MTLTexture> overlayDepthTexture = nil;
    };

    id<MTLDevice> device = nil;
    id<MTLCommandQueue> commandQueue = nil;
    NSUInteger overlaySampleCount = 1;
    std::array<OutputSlot, kOutputSlotCount> outputSlots{};
    std::shared_ptr<AsyncFrameState> asyncState = std::make_shared<AsyncFrameState>();

    // Full-screen quad (6 × float2)
    id<MTLBuffer> quadVertexBuffer = nil;

    // Accumulation texture (RGBA32Float)
    id<MTLTexture> accumTexture = nil;

    // Atom data buffers
    id<MTLBuffer> atomPositionBuffer = nil;  // float4(x,y,z,radius) per atom
    id<MTLBuffer> atomColorBuffer = nil;     // float4(r,g,b,a) per atom
    id<MTLBuffer> atomSelectionBuffer = nil; // uint selected flag per atom
    id<MTLBuffer> bvhNodeMinBuffer = nil;    // float4(min.xyz, maxRadius) per node
    id<MTLBuffer> bvhNodeMaxBuffer = nil;    // float4(max.xyz, pad) per node
    id<MTLBuffer> bvhNodeMetaBuffer = nil;   // uint4(left,right,first,count) per node
    id<MTLBuffer> bvhPrimIndexBuffer = nil;  // uint primitive indices

    // Bond data buffers
    id<MTLBuffer> bondStartBuffer = nil;     // float4(x,y,z,startRadius) per bond
    id<MTLBuffer> bondEndBuffer = nil;       // float4(x,y,z,endRadius) per bond
    id<MTLBuffer> bondStartColorBuffer = nil; // float4(r,g,b,a) per bond start
    id<MTLBuffer> bondEndColorBuffer = nil;   // float4(r,g,b,a) per bond end
    id<MTLBuffer> bondRadiusBuffer = nil;      // float radius per bond
    id<MTLBuffer> bondSelectionBuffer = nil;   // uint selected flag per bond

    // Unit-cell overlay geometry + instance buffers
    id<MTLBuffer> unitCellCylinderVertexBuffer = nil;   // unit cylinder mesh (packed_float3)
    id<MTLBuffer> unitCellCylinderIndexBuffer = nil;
    id<MTLBuffer> unitCellEdgeInstanceBuffer = nil;     // 12 × BondInstance

    id<MTLBuffer> unitCellSphereVertexBuffer = nil;     // unit sphere mesh (packed_float3)
    id<MTLBuffer> unitCellSphereIndexBuffer = nil;
    id<MTLBuffer> unitCellJointInstanceBuffer = nil;    // 8 × SphereInstance
};

MetalRayTracingRenderer::MetalRayTracingRenderer()
    : m_impl(std::make_unique<Impl>())
{
}

MetalRayTracingRenderer::~MetalRayTracingRenderer() {
    cleanup();
}

void MetalRayTracingRenderer::setDevice(void* device) {
    m_impl->device = (__bridge id<MTLDevice>)device;
}

bool MetalRayTracingRenderer::initialize() {
    if (m_initialized) return true;

    if (!m_impl->device) {
        qCritical() << "MetalRayTracingRenderer: device not set";
        return false;
    }

    m_impl->commandQueue = [m_impl->device newCommandQueue];

    m_impl->overlaySampleCount =
        [m_impl->device supportsTextureSampleCount:kPreferredOverlaySampleCount]
            ? kPreferredOverlaySampleCount
            : 1;
    if (m_impl->overlaySampleCount == 1) {
        qWarning() << "MetalRayTracingRenderer: 4x MSAA not supported for display/overlay; using single-sample";
    }

    // Compile shaders
    if (!m_shaderLibrary.initialize((__bridge void*)m_impl->device,
                                    static_cast<int>(m_impl->overlaySampleCount))) {
        return false;
    }

    // Full-screen quad: 6 × packed_float2
    const float quadVerts[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f,
    };
    m_impl->quadVertexBuffer = [m_impl->device newBufferWithBytes:quadVerts
                                                           length:sizeof(quadVerts)
                                                          options:MTLResourceStorageModeShared];

    // Unit-cell overlay meshes (constant topology, instance-transformed).
    std::vector<float> cylinderVertices;
    std::vector<uint32_t> cylinderIndices;
    buildUnitCylinderMesh(48, cylinderVertices, cylinderIndices);
    m_impl->unitCellCylinderVertexBuffer = [m_impl->device
        newBufferWithBytes:cylinderVertices.data()
                    length:cylinderVertices.size() * sizeof(float)
                   options:MTLResourceStorageModeShared];
    m_impl->unitCellCylinderIndexBuffer = [m_impl->device
        newBufferWithBytes:cylinderIndices.data()
                    length:cylinderIndices.size() * sizeof(uint32_t)
                   options:MTLResourceStorageModeShared];
    m_unitCellCylinderIndexCount = static_cast<int>(cylinderIndices.size());

    std::vector<float> sphereVertices;
    std::vector<uint32_t> sphereIndices;
    buildUnitSphereMesh(10, 16, sphereVertices, sphereIndices);
    m_impl->unitCellSphereVertexBuffer = [m_impl->device
        newBufferWithBytes:sphereVertices.data()
                    length:sphereVertices.size() * sizeof(float)
                   options:MTLResourceStorageModeShared];
    m_impl->unitCellSphereIndexBuffer = [m_impl->device
        newBufferWithBytes:sphereIndices.data()
                    length:sphereIndices.size() * sizeof(uint32_t)
                   options:MTLResourceStorageModeShared];
    m_unitCellSphereIndexCount = static_cast<int>(sphereIndices.size());

    if (!m_gizmoRenderer.initialize((__bridge void*)m_impl->device, &m_shaderLibrary)) return false;
    if (!m_viewportAxesRenderer.initialize((__bridge void*)m_impl->device, &m_shaderLibrary)) return false;

    m_initialized = true;
    qInfo() << "MetalRayTracingRenderer: initialized successfully";
    return true;
}

void MetalRayTracingRenderer::cleanup() {
    m_gizmoRenderer.cleanup();
    m_viewportAxesRenderer.cleanup();
    m_shaderLibrary.cleanup();

    m_impl->asyncState->generation.fetch_add(1, std::memory_order_relaxed);
    m_impl->asyncState->inFlightSlot.store(-1, std::memory_order_relaxed);
    resetOutputSlots(*m_impl->asyncState);

    m_impl->quadVertexBuffer = nil;
    m_impl->accumTexture = nil;
    for (auto& slot : m_impl->outputSlots) {
        slot.msaaOutputTexture = nil;
        slot.outputTexture = nil;
        slot.msaaOverlayDepthTexture = nil;
        slot.overlayDepthTexture = nil;
    }
    m_impl->atomPositionBuffer = nil;
    m_impl->atomColorBuffer = nil;
    m_impl->atomSelectionBuffer = nil;
    m_impl->bvhNodeMinBuffer = nil;
    m_impl->bvhNodeMaxBuffer = nil;
    m_impl->bvhNodeMetaBuffer = nil;
    m_impl->bvhPrimIndexBuffer = nil;
    m_impl->bondStartBuffer = nil;
    m_impl->bondEndBuffer = nil;
    m_impl->bondStartColorBuffer = nil;
    m_impl->bondEndColorBuffer = nil;
    m_impl->bondRadiusBuffer = nil;
    m_impl->bondSelectionBuffer = nil;
    m_impl->unitCellCylinderVertexBuffer = nil;
    m_impl->unitCellCylinderIndexBuffer = nil;
    m_impl->unitCellEdgeInstanceBuffer = nil;
    m_impl->unitCellSphereVertexBuffer = nil;
    m_impl->unitCellSphereIndexBuffer = nil;
    m_impl->unitCellJointInstanceBuffer = nil;
    m_impl->commandQueue = nil;
    m_impl->overlaySampleCount = 1;

    m_structure = nullptr;
    m_atomCount = 0;
    m_bondCount = 0;
    m_bvhNodeCount = 0;
    m_unitCellEdgeCount = 0;
    m_unitCellJointCount = 0;
    m_unitCellCylinderIndexCount = 0;
    m_unitCellSphereIndexCount = 0;
    m_lastStateHash = 0;
    m_outputGeneration = m_impl->asyncState->generation.load(std::memory_order_relaxed);
    m_lastPresentedSlot = -1;
    m_initialized = false;
}

void MetalRayTracingRenderer::resize(int width, int height) {
    if (width == m_width && height == m_height) return;
    if (width <= 0 || height <= 0) return;

    m_width = width;
    m_height = height;
    createRenderTargets();
    resetAccumulation();
}

void MetalRayTracingRenderer::setStructure(const data::Structure* structure) {
    m_structure = structure;
    m_atomDataDirty = true;
    m_bondDataDirty = true;
    m_unitCellDataDirty = true;
    resetAccumulation();
}

void MetalRayTracingRenderer::render(const Camera& camera, const RenderSettings& settings) {
    if (!m_initialized || m_width == 0 || m_height == 0) return;

    // Store settings for isConverged() and state hashing.
    m_settings = settings;

    // Submit at most one GPU frame at a time. Skip all CPU-side work
    // (scene packing, BVH build, buffer allocation) while a frame is
    // in-flight — dirty flags stay set and are processed once it drains.
    if (m_impl->asyncState->inFlightSlot.load(std::memory_order_acquire) >= 0) {
        return;
    }

    if (m_atomDataDirty || m_bondDataDirty) {
        uploadSceneData();
    } else if (m_appearanceDirty) {
        uploadAppearanceData();
    }
    if (m_unitCellDataDirty) {
        uploadUnitCellData();
    }

    // Ensure render targets exist
    if (!m_impl->accumTexture || !m_impl->outputSlots[0].outputTexture ||
        !m_impl->outputSlots[0].overlayDepthTexture ||
        (m_impl->overlaySampleCount > 1 &&
         (!m_impl->outputSlots[0].msaaOutputTexture ||
          !m_impl->outputSlots[0].msaaOverlayDepthTexture))) {
        createRenderTargets();
        resetAccumulation();
    }

    const int outputSlotIndex = acquireOutputSlot(*m_impl->asyncState, m_lastPresentedSlot);
    if (outputSlotIndex < 0) {
        return;
    }
    m_impl->outputSlots[static_cast<size_t>(outputSlotIndex)].frameRequestToken =
        settings.frameRequestToken;

    // Single command buffer for the entire frame — sub-passes encode into it
    // as separate render command encoders, committed once at the end.
    id<MTLCommandBuffer> cmdBuffer = [m_impl->commandQueue commandBuffer];

    if (m_atomCount == 0) {
        const bool hasUnitCellOverlay =
            m_settings.showUnitCell && m_unitCellEdgeCount > 0 && m_unitCellJointCount > 0;
        const bool hasAnyOverlay =
            hasUnitCellOverlay || m_settings.showViewportAxes || m_settings.showRotationCenter;

        if (hasAnyOverlay) {
            renderUnitCellOverlay(camera, (__bridge void*)cmdBuffer, outputSlotIndex);
        } else if (m_impl->outputSlots[outputSlotIndex].outputTexture) {
            // Clear output to background
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = m_impl->outputSlots[outputSlotIndex].outputTexture;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            const auto& bg = m_settings.backgroundColor;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(
                bg.redF() * bg.alphaF(), bg.greenF() * bg.alphaF(), bg.blueF() * bg.alphaF(), bg.alphaF());
            id<MTLRenderCommandEncoder> enc = [cmdBuffer renderCommandEncoderWithDescriptor:pass];
            [enc endEncoding];
        }
        trackSubmittedFrame((__bridge void*)cmdBuffer, outputSlotIndex, 0, 0, m_outputGeneration);
        [cmdBuffer commit];
        return;
    }

    // State change detection
    uint64_t currentHash = computeRenderStateHash(camera, m_settings);
    if (currentHash != m_lastStateHash) {
        m_lastStateHash = currentHash;
        resetAccumulation();
    }

    // Keep rendering the display/output passes even after convergence so
    // overlays (unit cell) and RT output refresh remain responsive.
    int batchSampleCount = 0;
    if (!isConverged()) {
        // Adaptive batching: encode several accumulation passes per command
        // buffer, sized from the measured GPU cost of one sample. The first
        // sample after a reset is always submitted alone so interaction
        // (camera moves reset accumulation every frame) stays at one cheap
        // sample per frame and feedback is immediate.
        const int remaining = std::max(m_settings.maxRTSamples - m_sampleCount, 1);
        int batch = 1;
        if (m_sampleCount > 0) {
            const uint64_t perSampleNanos =
                m_impl->asyncState->gpuNanosPerSample.load(std::memory_order_relaxed);
            if (perSampleNanos > 0) {
                const uint64_t fitting = kRTGpuBudgetNanos / perSampleNanos;
                batch = static_cast<int>(std::clamp<uint64_t>(fitting, 1, kRTMaxSamplesPerSubmit));
            } else {
                batch = 4;  // mild ramp until the first GPU timing arrives
            }
            batch = std::min(batch, remaining);
        }

        for (int i = 0; i < batch; ++i) {
            m_sampleCount++;
            renderRTPass(camera, (__bridge void*)cmdBuffer);
        }
        batchSampleCount = batch;
    }

    renderDisplayPass(camera, (__bridge void*)cmdBuffer, outputSlotIndex);

    trackSubmittedFrame((__bridge void*)cmdBuffer, outputSlotIndex, m_sampleCount,
                        batchSampleCount, m_outputGeneration);
    [cmdBuffer commit];
}

void MetalRayTracingRenderer::invalidateAtomData() {
    m_atomDataDirty = true;
    resetAccumulation();
}

void MetalRayTracingRenderer::invalidateBondData() {
    m_bondDataDirty = true;
    resetAccumulation();
}

void MetalRayTracingRenderer::invalidateAppearance() {
    m_appearanceDirty = true;
    resetAccumulation();
}

bool MetalRayTracingRenderer::needsMoreFrames() const {
    if (m_impl->asyncState->inFlightSlot.load(std::memory_order_acquire) >= 0) {
        return true;
    }
    return m_atomCount > 0 && !isConverged();
}

void MetalRayTracingRenderer::resetAccumulation() {
    m_sampleCount = 0;
    m_accumNeedsClear = true;
    m_outputGeneration = m_impl->asyncState->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
}

void* MetalRayTracingRenderer::outputTexture(uint64_t& frameRequestToken) {
    frameRequestToken = 0;
    const int readySlot = m_impl->asyncState->latestReadySlot.load(std::memory_order_acquire);
    if (readySlot < 0 || readySlot >= kOutputSlotCount) {
        return nullptr;
    }

    m_lastPresentedSlot = readySlot;
    frameRequestToken = m_impl->outputSlots[static_cast<size_t>(readySlot)].frameRequestToken;
    return (__bridge void*)m_impl->outputSlots[readySlot].outputTexture;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void MetalRayTracingRenderer::createRenderTargets() {
    // Accumulation: RGBA32Float
    MTLTextureDescriptor* accumDesc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                    width:m_width
                                   height:m_height
                                mipmapped:NO];
    accumDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    accumDesc.storageMode = MTLStorageModePrivate;
    m_impl->accumTexture = [m_impl->device newTextureWithDescriptor:accumDesc];

    for (auto& slot : m_impl->outputSlots) {
        // Display output: BGRA8Unorm
        MTLTextureDescriptor* outputDesc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                        width:m_width
                                       height:m_height
                                    mipmapped:NO];
        outputDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        outputDesc.storageMode = MTLStorageModePrivate;
        slot.outputTexture = [m_impl->device newTextureWithDescriptor:outputDesc];

        if (m_impl->overlaySampleCount > 1) {
            MTLTextureDescriptor* msaaOutputDesc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                            width:m_width
                                           height:m_height
                                        mipmapped:NO];
            msaaOutputDesc.textureType = MTLTextureType2DMultisample;
            msaaOutputDesc.sampleCount = m_impl->overlaySampleCount;
            msaaOutputDesc.usage = MTLTextureUsageRenderTarget;
            msaaOutputDesc.storageMode = MTLStorageModePrivate;
            slot.msaaOutputTexture = [m_impl->device newTextureWithDescriptor:msaaOutputDesc];

            MTLTextureDescriptor* msaaDepthDesc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                            width:m_width
                                           height:m_height
                                        mipmapped:NO];
            msaaDepthDesc.textureType = MTLTextureType2DMultisample;
            msaaDepthDesc.sampleCount = m_impl->overlaySampleCount;
            msaaDepthDesc.usage = MTLTextureUsageRenderTarget;
            msaaDepthDesc.storageMode = MTLStorageModePrivate;
            slot.msaaOverlayDepthTexture = [m_impl->device newTextureWithDescriptor:msaaDepthDesc];
        } else {
            slot.msaaOutputTexture = nil;
            slot.msaaOverlayDepthTexture = nil;
        }

        MTLTextureDescriptor* overlayDepthDesc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                        width:m_width
                                       height:m_height
                                    mipmapped:NO];
        overlayDepthDesc.usage = MTLTextureUsageRenderTarget;
        overlayDepthDesc.storageMode = MTLStorageModePrivate;
        slot.overlayDepthTexture = [m_impl->device newTextureWithDescriptor:overlayDepthDesc];
    }

    // Do NOT reset inFlightSlot here — let the old completion handler drain
    // it naturally.  Forcing it to -1 would allow render() to submit a new
    // frame before the old handler fires, and the old handler's CAS would
    // then incorrectly clear the *new* submission's inFlightSlot.
    resetOutputSlots(*m_impl->asyncState);
    m_lastPresentedSlot = -1;
}

void MetalRayTracingRenderer::uploadSceneData() {
    if (!m_structure || m_structure->atomCount() == 0) {
        m_atomCount = 0;
        m_bondCount = 0;
        m_bvhNodeCount = 0;
        m_impl->atomPositionBuffer = nil;
        m_impl->atomColorBuffer = nil;
        m_impl->atomSelectionBuffer = nil;
        m_impl->bvhNodeMinBuffer = nil;
        m_impl->bvhNodeMaxBuffer = nil;
        m_impl->bvhNodeMetaBuffer = nil;
        m_impl->bvhPrimIndexBuffer = nil;
        m_impl->bondStartBuffer = nil;
        m_impl->bondEndBuffer = nil;
        m_impl->bondStartColorBuffer = nil;
        m_impl->bondEndColorBuffer = nil;
        m_impl->bondRadiusBuffer = nil;
        m_impl->bondSelectionBuffer = nil;
        m_atomDataDirty = false;
        m_bondDataDirty = false;
        m_appearanceDirty = false;
        return;
    }

    m_atomCount = static_cast<int>(m_structure->atomCount());

    // ── Atom buffers ──────────────────────────────────────────
    // Buffer reuse is safe here: uploads only run while no command buffer is
    // in flight (render() gates on inFlightSlot before any upload work).

    auto posData = m_structure->packPositionsAndRadii();
    m_impl->atomPositionBuffer = fillSharedBuffer(
        m_impl->device, m_impl->atomPositionBuffer,
        posData.data(), posData.size() * sizeof(float));

    auto colorData = packAtomRenderColors(m_structure);
    m_impl->atomColorBuffer = fillSharedBuffer(
        m_impl->device, m_impl->atomColorBuffer,
        colorData.data(), colorData.size() * sizeof(float));

    auto atomSelectionData = packAtomSelectionMask(m_structure);
    m_impl->atomSelectionBuffer = fillSharedBuffer(
        m_impl->device, m_impl->atomSelectionBuffer,
        atomSelectionData.data(), atomSelectionData.size() * sizeof(uint32_t));

    // ── Bond buffers ──────────────────────────────────────────

    m_bondCount = static_cast<int>(bondRenderSegmentCount(m_structure));
    PackedBondRenderData packedBonds;

    if (m_bondCount > 0) {
        packedBonds = packBondRenderData(m_structure, BondPositionPacking::XYZW4);

        m_impl->bondStartBuffer = fillSharedBuffer(
            m_impl->device, m_impl->bondStartBuffer,
            packedBonds.startPositions.data(), packedBonds.startPositions.size() * sizeof(float));
        m_impl->bondEndBuffer = fillSharedBuffer(
            m_impl->device, m_impl->bondEndBuffer,
            packedBonds.endPositions.data(), packedBonds.endPositions.size() * sizeof(float));
        m_impl->bondStartColorBuffer = fillSharedBuffer(
            m_impl->device, m_impl->bondStartColorBuffer,
            packedBonds.startColors.data(), packedBonds.startColors.size() * sizeof(float));
        m_impl->bondEndColorBuffer = fillSharedBuffer(
            m_impl->device, m_impl->bondEndColorBuffer,
            packedBonds.endColors.data(), packedBonds.endColors.size() * sizeof(float));
        m_impl->bondRadiusBuffer = fillSharedBuffer(
            m_impl->device, m_impl->bondRadiusBuffer,
            packedBonds.bondRadii.data(), packedBonds.bondRadii.size() * sizeof(float));
        auto bondSelectionData = packBondSelectionMask(m_structure);
        m_impl->bondSelectionBuffer = fillSharedBuffer(
            m_impl->device, m_impl->bondSelectionBuffer,
            bondSelectionData.data(), bondSelectionData.size() * sizeof(uint32_t));
    } else {
        m_impl->bondStartBuffer = nil;
        m_impl->bondEndBuffer = nil;
        m_impl->bondStartColorBuffer = nil;
        m_impl->bondEndColorBuffer = nil;
        m_impl->bondRadiusBuffer = nil;
        m_impl->bondSelectionBuffer = nil;
    }

    // ── Unified BVH (atoms + bonds) ──────────────────────────

    size_t totalPrims = static_cast<size_t>(m_atomCount) + static_cast<size_t>(m_bondCount);
    std::vector<PrimitiveBounds> primBounds(totalPrims);

    const float* ax = m_structure->positionsX();
    const float* ay = m_structure->positionsY();
    const float* az = m_structure->positionsZ();
    const float* ar = m_structure->radii();
    for (size_t i = 0; i < static_cast<size_t>(m_atomCount); ++i) {
        float r = ar[i];
        primBounds[i] = {
            ax[i] - r, ay[i] - r, az[i] - r,
            ax[i] + r, ay[i] + r, az[i] + r,
            ax[i], ay[i], az[i],
            r
        };
    }

    for (size_t i = 0; i < static_cast<size_t>(m_bondCount); ++i) {
        float sx = packedBonds.startPositions[i * 4 + 0];
        float sy = packedBonds.startPositions[i * 4 + 1];
        float sz = packedBonds.startPositions[i * 4 + 2];
        float ex = packedBonds.endPositions[i * 4 + 0];
        float ey = packedBonds.endPositions[i * 4 + 1];
        float ez = packedBonds.endPositions[i * 4 + 2];
        float bondR = packedBonds.bondRadii[i];
        size_t pi = static_cast<size_t>(m_atomCount) + i;
        primBounds[pi] = {
            std::min(sx, ex) - bondR, std::min(sy, ey) - bondR, std::min(sz, ez) - bondR,
            std::max(sx, ex) + bondR, std::max(sy, ey) + bondR, std::max(sz, ez) + bondR,
            (sx + ex) * 0.5f, (sy + ey) * 0.5f, (sz + ez) * 0.5f,
            0.0f
        };
    }

    // Scene AABB over all primitives (for conservative outline-width bounds)
    for (int axis = 0; axis < 3; ++axis) {
        m_sceneBoundsMin[axis] = 1e30f;
        m_sceneBoundsMax[axis] = -1e30f;
    }
    for (const PrimitiveBounds& pb : primBounds) {
        m_sceneBoundsMin[0] = std::min(m_sceneBoundsMin[0], pb.minX);
        m_sceneBoundsMin[1] = std::min(m_sceneBoundsMin[1], pb.minY);
        m_sceneBoundsMin[2] = std::min(m_sceneBoundsMin[2], pb.minZ);
        m_sceneBoundsMax[0] = std::max(m_sceneBoundsMax[0], pb.maxX);
        m_sceneBoundsMax[1] = std::max(m_sceneBoundsMax[1], pb.maxY);
        m_sceneBoundsMax[2] = std::max(m_sceneBoundsMax[2], pb.maxZ);
    }

    BVHData bvh = buildBVH(primBounds.data(), totalPrims);
    assert(!bvh.nodes.empty() && "Expected non-empty BVH for non-empty structure");
    assert(!bvh.primitiveIndices.empty() && "Expected non-empty BVH primitive index list");
    m_bvhNodeCount = static_cast<int>(bvh.nodes.size());

    // Write node data straight into the (reused) shared-storage buffers —
    // no staging vectors.
    const size_t nodeCount = bvh.nodes.size();
    m_impl->bvhNodeMinBuffer = ensureSharedBuffer(
        m_impl->device, m_impl->bvhNodeMinBuffer, nodeCount * sizeof(simd_float4));
    m_impl->bvhNodeMaxBuffer = ensureSharedBuffer(
        m_impl->device, m_impl->bvhNodeMaxBuffer, nodeCount * sizeof(simd_float4));
    m_impl->bvhNodeMetaBuffer = ensureSharedBuffer(
        m_impl->device, m_impl->bvhNodeMetaBuffer, nodeCount * sizeof(simd_uint4));

    auto* nodeMins = static_cast<simd_float4*>(m_impl->bvhNodeMinBuffer.contents);
    auto* nodeMaxs = static_cast<simd_float4*>(m_impl->bvhNodeMaxBuffer.contents);
    auto* nodeMeta = static_cast<simd_uint4*>(m_impl->bvhNodeMetaBuffer.contents);
    for (size_t i = 0; i < nodeCount; ++i) {
        const BVHNodeGPU& node = bvh.nodes[i];
        nodeMins[i] = simd_make_float4(
            node.minAndMaxRadius[0],
            node.minAndMaxRadius[1],
            node.minAndMaxRadius[2],
            node.minAndMaxRadius[3]);
        nodeMaxs[i] = simd_make_float4(
            node.maxAndPad[0],
            node.maxAndPad[1],
            node.maxAndPad[2],
            node.maxAndPad[3]);
        nodeMeta[i] = simd_make_uint4(
            node.meta[0], node.meta[1], node.meta[2], node.meta[3]);
    }

    m_impl->bvhPrimIndexBuffer = fillSharedBuffer(
        m_impl->device, m_impl->bvhPrimIndexBuffer,
        bvh.primitiveIndices.data(), bvh.primitiveIndices.size() * sizeof(uint32_t));

    m_atomDataDirty = false;
    m_bondDataDirty = false;
    m_appearanceDirty = false;
}

void MetalRayTracingRenderer::uploadAppearanceData() {
    m_appearanceDirty = false;
    if (!m_structure || m_atomCount == 0) {
        return;
    }

    // Appearance updates assume unchanged geometry/topology. If counts moved
    // under us (shouldn't happen — topology changes go through the geometry
    // path), fall back to a full upload.
    if (static_cast<int>(m_structure->atomCount()) != m_atomCount ||
        static_cast<int>(bondRenderSegmentCount(m_structure)) != m_bondCount) {
        uploadSceneData();
        return;
    }

    auto colorData = packAtomRenderColors(m_structure);
    m_impl->atomColorBuffer = fillSharedBuffer(
        m_impl->device, m_impl->atomColorBuffer,
        colorData.data(), colorData.size() * sizeof(float));
    auto atomSelectionData = packAtomSelectionMask(m_structure);
    m_impl->atomSelectionBuffer = fillSharedBuffer(
        m_impl->device, m_impl->atomSelectionBuffer,
        atomSelectionData.data(), atomSelectionData.size() * sizeof(uint32_t));

    if (m_bondCount > 0) {
        std::vector<float> startColors;
        std::vector<float> endColors;
        packBondRenderColors(m_structure, startColors, endColors);

        m_impl->bondStartColorBuffer = fillSharedBuffer(
            m_impl->device, m_impl->bondStartColorBuffer,
            startColors.data(), startColors.size() * sizeof(float));
        m_impl->bondEndColorBuffer = fillSharedBuffer(
            m_impl->device, m_impl->bondEndColorBuffer,
            endColors.data(), endColors.size() * sizeof(float));
        auto bondSelectionData = packBondSelectionMask(m_structure);
        m_impl->bondSelectionBuffer = fillSharedBuffer(
            m_impl->device, m_impl->bondSelectionBuffer,
            bondSelectionData.data(), bondSelectionData.size() * sizeof(uint32_t));
    }
}

void MetalRayTracingRenderer::uploadUnitCellData() {
    UnitCellInstanceData instances;
    if (!buildUnitCellInstances(m_structure, instances)) {
        m_impl->unitCellEdgeInstanceBuffer = nil;
        m_impl->unitCellJointInstanceBuffer = nil;
        m_unitCellEdgeCount = 0;
        m_unitCellJointCount = 0;
        m_unitCellDataDirty = false;
        return;
    }

    m_impl->unitCellEdgeInstanceBuffer = fillSharedBuffer(
        m_impl->device, m_impl->unitCellEdgeInstanceBuffer,
        instances.edges.data(), instances.edges.size() * sizeof(BondInstance));
    m_impl->unitCellJointInstanceBuffer = fillSharedBuffer(
        m_impl->device, m_impl->unitCellJointInstanceBuffer,
        instances.joints.data(), instances.joints.size() * sizeof(SphereInstance));

    m_unitCellEdgeCount = kUnitCellEdgeCount;
    m_unitCellJointCount = kUnitCellJointCount;
    m_unitCellDataDirty = false;
}

void MetalRayTracingRenderer::renderRTPass(const Camera& camera, void* cmdBuf) {
    // Build RT uniforms
    RTUniforms rt{};
    rt.invView = qMatToSimd(camera.viewMatrix().inverted());
    rt.invProjection = qMatToSimd(camera.projectionMatrix().inverted());

    QVector3D camPos = camera.position();
    rt.cameraPosition = simd_make_float3(camPos.x(), camPos.y(), camPos.z());
    rt.atomScale = m_settings.atomScale;

    // Light direction is already in world space
    QVector3D worldLightDir = m_settings.lightDirWorld();
    rt.lightDir = simd_make_float3(worldLightDir.x(), worldLightDir.y(), worldLightDir.z());
    rt.ambient = m_settings.ambientStrength;

    rt.diffuse = m_settings.diffuseStrength;
    rt.specular = m_settings.specularStrength;
    rt.shininess = m_settings.shininess;
    rt.atomCount = m_atomCount;
    rt.width = m_width;
    rt.height = m_height;
    rt.frameCount = static_cast<uint32_t>(m_sampleCount);
    rt.enableShadows = m_settings.enableShadows ? 1 : 0;
    rt.enableAO = m_settings.enableAmbientOcclusion ? 1 : 0;
    rt.aoSamples = m_settings.aoSamples;
    rt.aoRadius = m_settings.aoRadius;
    rt.maxSamples = m_settings.maxRTSamples;
    rt.bvhNodeCount = m_bvhNodeCount;
    rt.bondCount = m_bondCount;
    rt.bondRadius = m_settings.bondRadius;
    rt.showAtoms = m_settings.showAtoms ? 1 : 0;
    rt.showBonds = m_settings.showBonds ? 1 : 0;
    rt.isPerspective = camera.isPerspective() ? 1 : 0;

    // Stroke outlines: pixel→world scale factor valid for both projections
    // (P[1][1] = 1/tan(fovY/2) perspective, 2/orthoHeight orthographic).
    const bool outlineOn = m_settings.outlineEnabled && m_settings.outlineWidth > 0.0f;
    const float selectionOutlineWidthPx =
        4.0f * std::max(m_settings.viewportAxesPixelRatio, 1.0f);
    const float p11 = camera.projectionMatrix()(1, 1);
    const float pixelScale =
        (p11 > 1e-6f) ? 2.0f / (p11 * static_cast<float>(m_height)) : 0.0f;
    rt.outlineScale = outlineOn ? m_settings.outlineWidth * pixelScale : 0.0f;
    rt.selectionOutlineScale = selectionOutlineWidthPx * pixelScale;
    const auto& oc = m_settings.outlineColor;
    rt.outlineColor = simd_make_float4(oc.redF(), oc.greenF(), oc.blueF(), 1.0f);
    if ((outlineOn || rt.selectionOutlineScale > 0.0f) && camera.isPerspective()) {
        // Conservative BVH padding: outline width at the farthest scene corner
        // (1.1× margin absorbs runtime atom-scale AABB growth).
        float dFarSq = 0.0f;
        for (int corner = 0; corner < 8; ++corner) {
            const float cx = (corner & 1) ? m_sceneBoundsMax[0] : m_sceneBoundsMin[0];
            const float cy = (corner & 2) ? m_sceneBoundsMax[1] : m_sceneBoundsMin[1];
            const float cz = (corner & 4) ? m_sceneBoundsMax[2] : m_sceneBoundsMin[2];
            const float dx = cx - camPos.x();
            const float dy = cy - camPos.y();
            const float dz = cz - camPos.z();
            dFarSq = std::max(dFarSq, dx * dx + dy * dy + dz * dz);
        }
        rt.outlineWorldMax = rt.outlineScale * std::sqrt(dFarSq) * 1.1f;
        rt.selectionOutlineWorldMax = rt.selectionOutlineScale * std::sqrt(dFarSq) * 1.1f;
    } else {
        rt.outlineWorldMax = rt.outlineScale;
        rt.selectionOutlineWorldMax = rt.selectionOutlineScale;
    }

    id<MTLCommandBuffer> cmdBuffer = (__bridge id<MTLCommandBuffer>)cmdBuf;

    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = m_impl->accumTexture;
    // Deferred accumulation clear: use Clear on first sample after reset,
    // then Load for subsequent samples to preserve existing accumulation.
    if (m_accumNeedsClear) {
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
        m_accumNeedsClear = false;
    } else {
        pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
    }
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> encoder = [cmdBuffer renderCommandEncoderWithDescriptor:pass];

    [encoder setViewport:(MTLViewport){0, 0,
        static_cast<double>(m_width), static_cast<double>(m_height),
        0.0, 1.0}];

    id<MTLRenderPipelineState> pipeline = (__bridge id<MTLRenderPipelineState>)m_shaderLibrary.rtPipeline();
    id<MTLDepthStencilState> depthState = (__bridge id<MTLDepthStencilState>)m_shaderLibrary.depthDisabledState();

    [encoder setRenderPipelineState:pipeline];
    [encoder setDepthStencilState:depthState];

    // buffer(0): quad vertices (vertex stage)
    [encoder setVertexBuffer:m_impl->quadVertexBuffer offset:0 atIndex:0];

    // buffer(0): RT uniforms (fragment stage)
    [encoder setFragmentBytes:&rt length:sizeof(RTUniforms) atIndex:0];

    // buffer(1)-(6): atom + BVH data (fragment stage)
    [encoder setFragmentBuffer:m_impl->atomPositionBuffer offset:0 atIndex:1];
    [encoder setFragmentBuffer:m_impl->atomColorBuffer offset:0 atIndex:2];
    [encoder setFragmentBuffer:m_impl->bvhNodeMinBuffer offset:0 atIndex:3];
    [encoder setFragmentBuffer:m_impl->bvhNodeMaxBuffer offset:0 atIndex:4];
    [encoder setFragmentBuffer:m_impl->bvhNodeMetaBuffer offset:0 atIndex:5];
    [encoder setFragmentBuffer:m_impl->bvhPrimIndexBuffer offset:0 atIndex:6];
    [encoder setFragmentBuffer:m_impl->atomSelectionBuffer offset:0 atIndex:12];

    // buffer(7)-(11), (13): bond data (fragment stage)
    if (m_impl->bondStartBuffer) {
        [encoder setFragmentBuffer:m_impl->bondStartBuffer offset:0 atIndex:7];
        [encoder setFragmentBuffer:m_impl->bondEndBuffer offset:0 atIndex:8];
        [encoder setFragmentBuffer:m_impl->bondStartColorBuffer offset:0 atIndex:9];
        [encoder setFragmentBuffer:m_impl->bondEndColorBuffer offset:0 atIndex:10];
        [encoder setFragmentBuffer:m_impl->bondRadiusBuffer offset:0 atIndex:11];
        [encoder setFragmentBuffer:m_impl->bondSelectionBuffer offset:0 atIndex:13];
    }

    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    [encoder endEncoding];
}

void MetalRayTracingRenderer::renderDisplayPass(const Camera& camera,
                                                void* cmdBuf,
                                                int outputSlotIndex) {
    DisplayUniforms disp{};
    disp.sampleCount = static_cast<float>(m_sampleCount);
    const QColor& bg = m_settings.backgroundColor;
    disp.backgroundColor = simd_make_float4(bg.redF(), bg.greenF(), bg.blueF(), bg.alphaF());

    id<MTLCommandBuffer> cmdBuffer = (__bridge id<MTLCommandBuffer>)cmdBuf;
    const auto& outputSlot = m_impl->outputSlots[static_cast<size_t>(outputSlotIndex)];

    // Overlay-free frames: a fullscreen quad gains nothing from MSAA, so
    // render straight into the resolved output texture — no MSAA target
    // writes, no resolve, no depth attachment.
    const bool hasOverlay = (m_settings.showUnitCell && hasUnitCellOverlayData()) ||
                            m_settings.showRotationCenter ||
                            m_settings.showViewportAxes;
    if (!hasOverlay) {
        MTLRenderPassDescriptor* directPass = [MTLRenderPassDescriptor renderPassDescriptor];
        directPass.colorAttachments[0].texture = outputSlot.outputTexture;
        directPass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        directPass.colorAttachments[0].storeAction = MTLStoreActionStore;

        id<MTLRenderCommandEncoder> directEncoder =
            [cmdBuffer renderCommandEncoderWithDescriptor:directPass];
        [directEncoder setViewport:(MTLViewport){0, 0,
            static_cast<double>(m_width), static_cast<double>(m_height),
            0.0, 1.0}];

        id<MTLRenderPipelineState> directPipeline =
            (__bridge id<MTLRenderPipelineState>)m_shaderLibrary.displayPipelineSingleSample();
        [directEncoder setRenderPipelineState:directPipeline];
        [directEncoder setVertexBuffer:m_impl->quadVertexBuffer offset:0 atIndex:0];
        [directEncoder setFragmentBytes:&disp length:sizeof(DisplayUniforms) atIndex:0];
        [directEncoder setFragmentTexture:m_impl->accumTexture atIndex:0];
        [directEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
        [directEncoder endEncoding];
        return;
    }

    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    if (outputSlot.msaaOutputTexture) {
        pass.colorAttachments[0].texture = outputSlot.msaaOutputTexture;
        pass.colorAttachments[0].resolveTexture = outputSlot.outputTexture;
        pass.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
    } else {
        pass.colorAttachments[0].texture = outputSlot.outputTexture;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    }
    pass.depthAttachment.texture = outputSlot.msaaOutputTexture ? outputSlot.msaaOverlayDepthTexture
                                                                : outputSlot.overlayDepthTexture;
    pass.depthAttachment.loadAction = MTLLoadActionClear;
    pass.depthAttachment.storeAction = MTLStoreActionDontCare;
    pass.depthAttachment.clearDepth = 1.0;

    id<MTLRenderCommandEncoder> encoder = [cmdBuffer renderCommandEncoderWithDescriptor:pass];

    [encoder setViewport:(MTLViewport){0, 0,
        static_cast<double>(m_width), static_cast<double>(m_height),
        0.0, 1.0}];

    id<MTLRenderPipelineState> pipeline = (__bridge id<MTLRenderPipelineState>)m_shaderLibrary.displayPipeline();
    id<MTLDepthStencilState> depthState = (__bridge id<MTLDepthStencilState>)m_shaderLibrary.depthDisabledState();

    [encoder setRenderPipelineState:pipeline];
    [encoder setDepthStencilState:depthState];

    [encoder setVertexBuffer:m_impl->quadVertexBuffer offset:0 atIndex:0];
    [encoder setFragmentBytes:&disp length:sizeof(DisplayUniforms) atIndex:0];
    [encoder setFragmentTexture:m_impl->accumTexture atIndex:0];

    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];

    if (m_settings.showUnitCell && hasUnitCellOverlayData()) {
        const RTUnitCellUniforms unitCell = makeRTUnitCellUniforms(
            camera, m_settings,
            m_atomCount,
            m_bondCount,
            m_bvhNodeCount);
        encodeUnitCellOverlayDraws((__bridge void*)encoder, unitCell);
    }

    if (m_settings.showRotationCenter) {
        // Gizmo now uses the bond/cylinder shader path, which needs view + projection.
        QMatrix4x4 bias;
        bias(2, 2) = 0.5f;
        bias(2, 3) = 0.5f;
        SceneUniforms gizmoUniforms{};
        gizmoUniforms.viewMatrix = qMatToSimd(camera.viewMatrix());
        gizmoUniforms.projectionMatrix = qMatToSimd(bias * camera.projectionMatrix());
        gizmoUniforms.viewProjectionMatrix = qMatToSimd(bias * camera.viewProjectionMatrix());
        gizmoUniforms.isPerspective = camera.isPerspective() ? 1 : 0;
        float len = camera.viewScale() * 0.03f;
        m_gizmoRenderer.render((__bridge void*)encoder, gizmoUniforms,
                               m_settings.rotationCenterX, m_settings.rotationCenterY,
                               m_settings.rotationCenterZ, len, /*depthTest=*/true);
    }

    if (m_settings.showViewportAxes) {
        m_viewportAxesRenderer.render((__bridge void*)encoder, camera, m_settings, m_width, m_height);
    }

    [encoder endEncoding];
}

void MetalRayTracingRenderer::renderUnitCellOverlay(const Camera& camera,
                                                    void* cmdBuf,
                                                    int outputSlotIndex) {
    const auto& outputSlot = m_impl->outputSlots[static_cast<size_t>(outputSlotIndex)];
    if (!outputSlot.outputTexture) {
        return;
    }

    const bool drawUnitCell = m_settings.showUnitCell && hasUnitCellOverlayData();
    const bool drawViewportAxes = m_settings.showViewportAxes;
    const bool drawRotationCenter = m_settings.showRotationCenter;
    if (!drawUnitCell && !drawViewportAxes && !drawRotationCenter) {
        return;
    }

    RTUnitCellUniforms unitCell{};
    if (drawUnitCell) {
        unitCell = makeRTUnitCellUniforms(camera, m_settings,
                                          m_atomCount,
                                          m_bondCount,
                                          m_bvhNodeCount);
    }

    id<MTLCommandBuffer> cmdBuffer = (__bridge id<MTLCommandBuffer>)cmdBuf;

    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    if (outputSlot.msaaOutputTexture) {
        pass.colorAttachments[0].texture = outputSlot.msaaOutputTexture;
        pass.colorAttachments[0].resolveTexture = outputSlot.outputTexture;
        pass.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
    } else {
        pass.colorAttachments[0].texture = outputSlot.outputTexture;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    }
    pass.depthAttachment.texture = outputSlot.msaaOutputTexture ? outputSlot.msaaOverlayDepthTexture
                                                                : outputSlot.overlayDepthTexture;
    pass.depthAttachment.loadAction = MTLLoadActionClear;
    pass.depthAttachment.storeAction = MTLStoreActionDontCare;
    pass.depthAttachment.clearDepth = 1.0;
    if (m_atomCount == 0) {
        const auto& bg = m_settings.backgroundColor;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(
            bg.redF(), bg.greenF(), bg.blueF(), 1.0);
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    } else {
        pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
    }

    id<MTLRenderCommandEncoder> encoder = [cmdBuffer renderCommandEncoderWithDescriptor:pass];

    [encoder setViewport:(MTLViewport){0, 0,
        static_cast<double>(m_width), static_cast<double>(m_height),
        0.0, 1.0}];

    if (drawUnitCell) {
        encodeUnitCellOverlayDraws((__bridge void*)encoder, unitCell);
    }

    if (drawRotationCenter) {
        QMatrix4x4 bias;
        bias(2, 2) = 0.5f;
        bias(2, 3) = 0.5f;
        SceneUniforms gizmoUniforms{};
        gizmoUniforms.viewMatrix = qMatToSimd(camera.viewMatrix());
        gizmoUniforms.projectionMatrix = qMatToSimd(bias * camera.projectionMatrix());
        gizmoUniforms.viewProjectionMatrix = qMatToSimd(bias * camera.viewProjectionMatrix());
        gizmoUniforms.isPerspective = camera.isPerspective() ? 1 : 0;
        float len = camera.viewScale() * 0.03f;
        m_gizmoRenderer.render((__bridge void*)encoder, gizmoUniforms,
                               m_settings.rotationCenterX, m_settings.rotationCenterY,
                               m_settings.rotationCenterZ, len, /*depthTest=*/true);
    }

    if (drawViewportAxes) {
        m_viewportAxesRenderer.render((__bridge void*)encoder, camera, m_settings, m_width, m_height);
    }

    [encoder endEncoding];
}

bool MetalRayTracingRenderer::hasUnitCellOverlayData() const {
    return m_unitCellEdgeCount > 0 && m_unitCellJointCount > 0 &&
           m_impl->unitCellCylinderVertexBuffer && m_impl->unitCellCylinderIndexBuffer &&
           m_impl->unitCellEdgeInstanceBuffer &&
           m_impl->unitCellSphereVertexBuffer && m_impl->unitCellSphereIndexBuffer &&
           m_impl->unitCellJointInstanceBuffer;
}

void MetalRayTracingRenderer::encodeUnitCellOverlayDraws(
    void* encoderPtr, const RTUnitCellUniforms& unitCell) {
    id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)encoderPtr;
    id<MTLDepthStencilState> depthState =
        (__bridge id<MTLDepthStencilState>)m_shaderLibrary.depthDisabledState();

    [encoder setDepthStencilState:depthState];
    [encoder setCullMode:MTLCullModeNone];
    [encoder setFragmentBuffer:m_impl->atomPositionBuffer offset:0 atIndex:1];
    [encoder setFragmentBuffer:m_impl->bvhNodeMinBuffer offset:0 atIndex:3];
    [encoder setFragmentBuffer:m_impl->bvhNodeMaxBuffer offset:0 atIndex:4];
    [encoder setFragmentBuffer:m_impl->bvhNodeMetaBuffer offset:0 atIndex:5];
    [encoder setFragmentBuffer:m_impl->bvhPrimIndexBuffer offset:0 atIndex:6];
    [encoder setFragmentBuffer:m_impl->bondStartBuffer offset:0 atIndex:7];
    [encoder setFragmentBuffer:m_impl->bondEndBuffer offset:0 atIndex:8];

    // Edge cylinders.
    {
        id<MTLRenderPipelineState> pipeline =
            (__bridge id<MTLRenderPipelineState>)m_shaderLibrary.rtUnitCellCylinderPipeline();
        [encoder setRenderPipelineState:pipeline];
        [encoder setVertexBytes:&unitCell length:sizeof(RTUnitCellUniforms) atIndex:0];
        [encoder setVertexBuffer:m_impl->unitCellCylinderVertexBuffer offset:0 atIndex:1];
        [encoder setVertexBuffer:m_impl->unitCellEdgeInstanceBuffer offset:0 atIndex:2];
        [encoder setFragmentBytes:&unitCell length:sizeof(RTUnitCellUniforms) atIndex:0];

        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:m_unitCellCylinderIndexCount
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:m_impl->unitCellCylinderIndexBuffer
                     indexBufferOffset:0
                         instanceCount:m_unitCellEdgeCount];
    }

    // Corner joints.
    {
        id<MTLRenderPipelineState> pipeline =
            (__bridge id<MTLRenderPipelineState>)m_shaderLibrary.rtUnitCellSpherePipeline();
        [encoder setRenderPipelineState:pipeline];
        [encoder setVertexBytes:&unitCell length:sizeof(RTUnitCellUniforms) atIndex:0];
        [encoder setVertexBuffer:m_impl->unitCellSphereVertexBuffer offset:0 atIndex:1];
        [encoder setVertexBuffer:m_impl->unitCellJointInstanceBuffer offset:0 atIndex:2];
        [encoder setFragmentBytes:&unitCell length:sizeof(RTUnitCellUniforms) atIndex:0];

        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:m_unitCellSphereIndexCount
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:m_impl->unitCellSphereIndexBuffer
                     indexBufferOffset:0
                         instanceCount:m_unitCellJointCount];
    }
}

void MetalRayTracingRenderer::trackSubmittedFrame(void* cmdBuf,
                                                  int outputSlotIndex,
                                                  int submittedSampleCount,
                                                  int batchSampleCount,
                                                  uint64_t generation) {
    auto asyncState = m_impl->asyncState;
    asyncState->slotStates[static_cast<size_t>(outputSlotIndex)].store(
        static_cast<uint8_t>(OutputSlotState::InFlight), std::memory_order_release);
    asyncState->inFlightSlot.store(outputSlotIndex, std::memory_order_release);

    id<MTLCommandBuffer> cmdBuffer = (__bridge id<MTLCommandBuffer>)cmdBuf;
    [cmdBuffer addCompletedHandler:^(id<MTLCommandBuffer> completedBuffer) {
        const auto freeState = static_cast<uint8_t>(OutputSlotState::Free);
        const auto readyState = static_cast<uint8_t>(OutputSlotState::Ready);

        int expectedSlot = outputSlotIndex;
        asyncState->inFlightSlot.compare_exchange_strong(expectedSlot, -1, std::memory_order_acq_rel);

        if (completedBuffer.status != MTLCommandBufferStatusCompleted ||
            asyncState->generation.load(std::memory_order_acquire) != generation) {
            asyncState->slotStates[static_cast<size_t>(outputSlotIndex)].store(
                freeState, std::memory_order_release);
            return;
        }

        // Feed the adaptive batch size: measured GPU nanoseconds per RT sample
        // (display/overlay overhead is amortized into the estimate, which only
        // makes the next batch slightly conservative).
        if (batchSampleCount > 0) {
            const double gpuSeconds = completedBuffer.GPUEndTime - completedBuffer.GPUStartTime;
            if (gpuSeconds > 0.0) {
                const uint64_t perSample = static_cast<uint64_t>(
                    gpuSeconds * 1e9 / static_cast<double>(batchSampleCount));
                asyncState->gpuNanosPerSample.store(std::max<uint64_t>(perSample, 1),
                                                    std::memory_order_relaxed);
            }
        }

        asyncState->slotStates[static_cast<size_t>(outputSlotIndex)].store(
            readyState, std::memory_order_release);
        asyncState->latestReadySampleCount.store(submittedSampleCount, std::memory_order_release);
        asyncState->latestReadySlot.store(outputSlotIndex, std::memory_order_release);
    }];
}

} // namespace atom::render::metal
