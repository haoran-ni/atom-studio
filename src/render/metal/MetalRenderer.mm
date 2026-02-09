#import <Metal/Metal.h>
#include "MetalRenderer.h"
#include "MetalTypes.h"
#include "../common/Camera.h"
#include "../../data/Structure.h"
#include <QDebug>
#include <QMatrix4x4>
#include <cstring>

namespace atom::render::metal {

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
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> commandQueue = nil;
    id<MTLTexture> colorTexture = nil;
    id<MTLTexture> depthTexture = nil;
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
    if (!m_shaderLibrary.initialize((__bridge void*)m_impl->device)) {
        return false;
    }

    // Initialize sub-renderers
    void* dev = (__bridge void*)m_impl->device;
    if (!m_sphereRenderer.initialize(dev, &m_shaderLibrary)) return false;
    if (!m_bondRenderer.initialize(dev, &m_shaderLibrary)) return false;
    if (!m_unitCellRenderer.initialize(dev, &m_shaderLibrary)) return false;

    m_initialized = true;
    qInfo() << "MetalRenderer: initialized successfully";
    return true;
}

void MetalRenderer::cleanup() {
    m_sphereRenderer.cleanup();
    m_bondRenderer.cleanup();
    m_unitCellRenderer.cleanup();
    m_shaderLibrary.cleanup();

    m_impl->colorTexture = nil;
    m_impl->depthTexture = nil;
    m_impl->commandQueue = nil;

    m_structure = nullptr;
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

void MetalRenderer::render(const Camera& camera) {
    if (!m_initialized || m_width == 0 || m_height == 0) return;
    if (!m_impl->colorTexture || !m_impl->depthTexture) return;

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

    QVector3D lightDir(m_settings.lightDirX, m_settings.lightDirY, m_settings.lightDirZ);
    lightDir.normalize();
    uniforms.lightDir = simd_make_float3(lightDir.x(), lightDir.y(), lightDir.z());
    uniforms.ambient = m_settings.ambientStrength;
    uniforms.diffuse = m_settings.diffuseStrength;
    uniforms.specular = m_settings.specularStrength;
    uniforms.shininess = m_settings.shininess;
    uniforms.atomScale = m_settings.atomScale;
    uniforms.bondRadius = m_settings.bondRadius;

    // Create command buffer and render pass
    id<MTLCommandBuffer> cmdBuffer = [m_impl->commandQueue commandBuffer];

    MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    passDesc.colorAttachments[0].texture = m_impl->colorTexture;
    passDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
    passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

    const auto& bg = m_settings.backgroundColor;
    passDesc.colorAttachments[0].clearColor = MTLClearColorMake(
        bg.redF(), bg.greenF(), bg.blueF(), 1.0);

    passDesc.depthAttachment.texture = m_impl->depthTexture;
    passDesc.depthAttachment.loadAction = MTLLoadActionClear;
    passDesc.depthAttachment.storeAction = MTLStoreActionDontCare;
    passDesc.depthAttachment.clearDepth = 1.0;

    id<MTLRenderCommandEncoder> encoder =
        [cmdBuffer renderCommandEncoderWithDescriptor:passDesc];

    [encoder setViewport:(MTLViewport){0, 0,
        static_cast<double>(m_width), static_cast<double>(m_height),
        0.0, 1.0}];

    // Render order: unit-cell object -> bonds -> spheres
    if (m_settings.showUnitCell) {
        m_unitCellRenderer.render((__bridge void*)encoder, uniforms, m_settings);
    }
    if (m_settings.showBonds) {
        m_bondRenderer.render((__bridge void*)encoder, uniforms);
    }
    if (m_settings.showAtoms) {
        m_sphereRenderer.render((__bridge void*)encoder, uniforms);
    }

    [encoder endEncoding];
    [cmdBuffer commit];
    [cmdBuffer waitUntilCompleted];
}

void MetalRenderer::invalidateAtomData() {
    m_atomDataDirty = true;
}

void MetalRenderer::invalidateBondData() {
    m_bondDataDirty = true;
}

void* MetalRenderer::colorTexture() const {
    return (__bridge void*)m_impl->colorTexture;
}

void MetalRenderer::createRenderTargets() {
    // Color texture: BGRA8Unorm
    MTLTextureDescriptor* colorDesc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                    width:m_width
                                   height:m_height
                                mipmapped:NO];
    colorDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    colorDesc.storageMode = MTLStorageModePrivate;
    m_impl->colorTexture = [m_impl->device newTextureWithDescriptor:colorDesc];

    // Depth texture: Depth32Float
    MTLTextureDescriptor* depthDesc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                    width:m_width
                                   height:m_height
                                mipmapped:NO];
    depthDesc.usage = MTLTextureUsageRenderTarget;
    depthDesc.storageMode = MTLStorageModePrivate;
    m_impl->depthTexture = [m_impl->device newTextureWithDescriptor:depthDesc];
}

} // namespace atom::render::metal
