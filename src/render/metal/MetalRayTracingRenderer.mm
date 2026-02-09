#import <Metal/Metal.h>
#include "MetalRayTracingRenderer.h"
#include "MetalTypes.h"
#include "../common/Camera.h"
#include "../../data/Structure.h"
#include <QDebug>
#include <QMatrix4x4>
#include <cstring>
#include <functional>

namespace atom::render::metal {

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
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> commandQueue = nil;

    // Full-screen quad (6 × float2)
    id<MTLBuffer> quadVertexBuffer = nil;

    // Accumulation texture (RGBA32Float)
    id<MTLTexture> accumTexture = nil;

    // Display output texture (BGRA8Unorm) — what the viewport shows
    id<MTLTexture> outputTexture = nil;

    // Atom data buffers
    id<MTLBuffer> atomPositionBuffer = nil;  // float4(x,y,z,radius) per atom
    id<MTLBuffer> atomColorBuffer = nil;     // float4(r,g,b,a) per atom
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

    // Compile shaders
    if (!m_shaderLibrary.initialize((__bridge void*)m_impl->device)) {
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

    m_initialized = true;
    qInfo() << "MetalRayTracingRenderer: initialized successfully";
    return true;
}

void MetalRayTracingRenderer::cleanup() {
    m_shaderLibrary.cleanup();

    m_impl->quadVertexBuffer = nil;
    m_impl->accumTexture = nil;
    m_impl->outputTexture = nil;
    m_impl->atomPositionBuffer = nil;
    m_impl->atomColorBuffer = nil;
    m_impl->commandQueue = nil;

    m_structure = nullptr;
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
    resetAccumulation();
}

void MetalRayTracingRenderer::render(const Camera& camera) {
    if (!m_initialized || m_width == 0 || m_height == 0) return;

    if (m_atomDataDirty) {
        uploadAtomData();
    }

    if (m_atomCount == 0) {
        // Clear output to background
        if (m_impl->outputTexture) {
            id<MTLCommandBuffer> cmd = [m_impl->commandQueue commandBuffer];
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = m_impl->outputTexture;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            const auto& bg = m_settings.backgroundColor;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(
                bg.redF(), bg.greenF(), bg.blueF(), 1.0);
            id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:pass];
            [enc endEncoding];
            [cmd commit];
            [cmd waitUntilCompleted];
        }
        return;
    }

    // State change detection
    uint64_t currentHash = computeStateHash(camera);
    if (currentHash != m_lastStateHash) {
        m_lastStateHash = currentHash;
        resetAccumulation();
    }

    if (isConverged()) return;

    m_sampleCount++;

    // Ensure render targets exist
    if (!m_impl->accumTexture) {
        createRenderTargets();
    }

    renderRTPass(camera);
    renderDisplayPass();
}

void MetalRayTracingRenderer::invalidateAtomData() {
    m_atomDataDirty = true;
    resetAccumulation();
}

void MetalRayTracingRenderer::invalidateBondData() {
    resetAccumulation();
}

void MetalRayTracingRenderer::resetAccumulation() {
    m_sampleCount = 0;

    if (m_impl->accumTexture && m_initialized) {
        // Clear accumulation texture to black
        id<MTLCommandBuffer> cmd = [m_impl->commandQueue commandBuffer];
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = m_impl->accumTexture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);

        id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:pass];
        [enc endEncoding];
        [cmd commit];
        [cmd waitUntilCompleted];
    }
}

void* MetalRayTracingRenderer::outputTexture() const {
    return (__bridge void*)m_impl->outputTexture;
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

    // Display output: BGRA8Unorm
    MTLTextureDescriptor* outputDesc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                    width:m_width
                                   height:m_height
                                mipmapped:NO];
    outputDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    outputDesc.storageMode = MTLStorageModePrivate;
    m_impl->outputTexture = [m_impl->device newTextureWithDescriptor:outputDesc];
}

void MetalRayTracingRenderer::uploadAtomData() {
    if (!m_structure || m_structure->atomCount() == 0) {
        m_atomCount = 0;
        m_impl->atomPositionBuffer = nil;
        m_impl->atomColorBuffer = nil;
        m_atomDataDirty = false;
        return;
    }

    m_atomCount = static_cast<int>(m_structure->atomCount());

    auto posData = m_structure->packPositionsAndRadii();
    m_impl->atomPositionBuffer = [m_impl->device
        newBufferWithBytes:posData.data()
                    length:posData.size() * sizeof(float)
                   options:MTLResourceStorageModeShared];

    auto colorData = m_structure->packColors();
    m_impl->atomColorBuffer = [m_impl->device
        newBufferWithBytes:colorData.data()
                    length:colorData.size() * sizeof(float)
                   options:MTLResourceStorageModeShared];

    m_atomDataDirty = false;
}

void MetalRayTracingRenderer::renderRTPass(const Camera& camera) {
    // Build RT uniforms
    RTUniforms rt{};
    rt.invView = qMatToSimd(camera.viewMatrix().inverted());
    rt.invProjection = qMatToSimd(camera.projectionMatrix().inverted());

    QVector3D camPos = camera.position();
    rt.cameraPosition = simd_make_float3(camPos.x(), camPos.y(), camPos.z());
    rt.atomScale = m_settings.atomScale;

    QVector3D lightDir(m_settings.lightDirX, m_settings.lightDirY, m_settings.lightDirZ);
    lightDir.normalize();
    rt.lightDir = simd_make_float3(lightDir.x(), lightDir.y(), lightDir.z());
    rt.ambient = m_settings.ambientStrength;

    QColor bg = m_settings.backgroundColor;
    rt.backgroundColor = simd_make_float3(bg.redF(), bg.greenF(), bg.blueF());
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

    // Render
    id<MTLCommandBuffer> cmdBuffer = [m_impl->commandQueue commandBuffer];

    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = m_impl->accumTexture;
    pass.colorAttachments[0].loadAction = MTLLoadActionLoad; // keep existing accumulation
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

    // buffer(1),(2): atom data (fragment stage)
    [encoder setFragmentBuffer:m_impl->atomPositionBuffer offset:0 atIndex:1];
    [encoder setFragmentBuffer:m_impl->atomColorBuffer offset:0 atIndex:2];

    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    [encoder endEncoding];

    [cmdBuffer commit];
    [cmdBuffer waitUntilCompleted];
}

void MetalRayTracingRenderer::renderDisplayPass() {
    DisplayUniforms disp{};
    disp.sampleCount = static_cast<float>(m_sampleCount);

    id<MTLCommandBuffer> cmdBuffer = [m_impl->commandQueue commandBuffer];

    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = m_impl->outputTexture;
    pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;

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
    [encoder endEncoding];

    [cmdBuffer commit];
    [cmdBuffer waitUntilCompleted];
}

uint64_t MetalRayTracingRenderer::computeStateHash(const Camera& camera) const {
    std::hash<float> hf;
    std::hash<int> hi;
    std::hash<bool> hb;

    uint64_t h = 0;
    auto combine = [&](uint64_t val) {
        h ^= val + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    };

    combine(hf(camera.azimuth()));
    combine(hf(camera.elevation()));
    combine(hf(camera.distance()));
    combine(hf(camera.target().x()));
    combine(hf(camera.target().y()));
    combine(hf(camera.target().z()));
    combine(hf(camera.fieldOfView()));
    combine(hf(camera.aspectRatio()));
    combine(hb(camera.isPerspective()));
    combine(hf(camera.orthoScale()));

    combine(hf(m_settings.atomScale));
    combine(hb(m_settings.enableShadows));
    combine(hb(m_settings.enableAmbientOcclusion));
    combine(hi(m_settings.aoSamples));
    combine(hf(m_settings.aoRadius));
    combine(hf(m_settings.ambientStrength));
    combine(hf(m_settings.diffuseStrength));
    combine(hf(m_settings.specularStrength));
    combine(hf(m_settings.shininess));
    combine(hf(m_settings.lightDirX));
    combine(hf(m_settings.lightDirY));
    combine(hf(m_settings.lightDirZ));

    return h;
}

} // namespace atom::render::metal
