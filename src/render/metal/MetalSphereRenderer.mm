#import <Metal/Metal.h>
#include "MetalSphereRenderer.h"
#include "MetalShaderLibrary.h"
#include "MetalTypes.h"
#include "../common/BondRenderData.h"
#include "../../data/Structure.h"
#include <QDebug>
#include <vector>

namespace atom::render::metal {

struct MetalSphereRenderer::Impl {
    id<MTLDevice> device = nil;
    id<MTLBuffer> quadVertexBuffer = nil;   // 6 × float3 billboard quad
    id<MTLBuffer> instanceBuffer = nil;     // N × SphereInstance
};

MetalSphereRenderer::MetalSphereRenderer()
    : m_impl(std::make_unique<Impl>())
{
}

MetalSphereRenderer::~MetalSphereRenderer() {
    cleanup();
}

bool MetalSphereRenderer::initialize(void* device, MetalShaderLibrary* shaderLibrary) {
    if (m_initialized) return true;

    m_impl->device = (__bridge id<MTLDevice>)device;
    m_shaderLibrary = shaderLibrary;

    if (!m_impl->device || !m_shaderLibrary || !m_shaderLibrary->isInitialized()) {
        qCritical() << "MetalSphereRenderer: invalid device or shader library";
        return false;
    }

    createQuadGeometry();
    m_initialized = true;
    return true;
}

void MetalSphereRenderer::cleanup() {
    m_impl->quadVertexBuffer = nil;
    m_impl->instanceBuffer = nil;
    m_atomCount = 0;
    m_initialized = false;
}

void MetalSphereRenderer::createQuadGeometry() {
    // Billboard quad: two triangles from -1 to 1 (packed_float3)
    const float quadVertices[] = {
        -1.0f, -1.0f, 0.0f,
         1.0f, -1.0f, 0.0f,
         1.0f,  1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f,
         1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f,
    };

    m_impl->quadVertexBuffer = [m_impl->device newBufferWithBytes:quadVertices
                                                           length:sizeof(quadVertices)
                                                          options:MTLResourceStorageModeShared];
}

void MetalSphereRenderer::setAtomData(const data::Structure* structure) {
    if (!m_initialized) return;

    if (!structure || structure->atomCount() == 0) {
        m_atomCount = 0;
        m_impl->instanceBuffer = nil;
        return;
    }

    m_atomCount = structure->atomCount();

    // Pack SphereInstance array (positionAndRadius + color)
    std::vector<SphereInstance> instances(m_atomCount);
    const float* px = structure->positionsX();
    const float* py = structure->positionsY();
    const float* pz = structure->positionsZ();
    const float* radii = structure->radii();
    std::vector<float> colorData = packAtomRenderColors(structure);

    for (size_t i = 0; i < m_atomCount; ++i) {
        instances[i].positionAndRadius = simd_make_float4(px[i], py[i], pz[i], radii[i]);
        instances[i].color = simd_make_float4(colorData[i * 4 + 0],
                                              colorData[i * 4 + 1],
                                              colorData[i * 4 + 2],
                                              colorData[i * 4 + 3]);
    }

    NSUInteger size = m_atomCount * sizeof(SphereInstance);
    m_impl->instanceBuffer = [m_impl->device newBufferWithBytes:instances.data()
                                                         length:size
                                                        options:MTLResourceStorageModeShared];
}

void MetalSphereRenderer::render(void* encoderPtr, const SceneUniforms& uniforms) {
    if (!m_initialized || m_atomCount == 0) return;

    id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)encoderPtr;
    id<MTLRenderPipelineState> pipeline = (__bridge id<MTLRenderPipelineState>)m_shaderLibrary->spherePipeline();
    id<MTLDepthStencilState> depthState = (__bridge id<MTLDepthStencilState>)m_shaderLibrary->depthLessWriteState();

    [encoder setRenderPipelineState:pipeline];
    [encoder setDepthStencilState:depthState];
    [encoder setCullMode:MTLCullModeNone]; // Billboard quads are camera-facing

    // buffer(0): SceneUniforms — vertex + fragment
    [encoder setVertexBytes:&uniforms length:sizeof(SceneUniforms) atIndex:0];
    [encoder setFragmentBytes:&uniforms length:sizeof(SceneUniforms) atIndex:0];

    // buffer(1): quad vertices
    [encoder setVertexBuffer:m_impl->quadVertexBuffer offset:0 atIndex:1];

    // buffer(2): instance data
    [encoder setVertexBuffer:m_impl->instanceBuffer offset:0 atIndex:2];

    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:6
              instanceCount:m_atomCount];
}

} // namespace atom::render::metal
