#import <Metal/Metal.h>
#include "MetalBondRenderer.h"
#include "MetalShaderLibrary.h"
#include "MetalTypes.h"
#include "MetalUnitCellShared.h"
#include "../common/BondRenderData.h"
#include "../../data/Structure.h"
#include <QDebug>
#include <vector>

namespace atom::render::metal {

struct MetalBondRenderer::Impl {
    id<MTLDevice> device = nil;
    id<MTLBuffer> quadVertexBuffer = nil;
    id<MTLBuffer> instanceBuffer = nil;      // N × BondInstance
};

MetalBondRenderer::MetalBondRenderer()
    : m_impl(std::make_unique<Impl>())
{
}

MetalBondRenderer::~MetalBondRenderer() {
    cleanup();
}

bool MetalBondRenderer::initialize(void* device, MetalShaderLibrary* shaderLibrary) {
    if (m_initialized) return true;

    m_impl->device = (__bridge id<MTLDevice>)device;
    m_shaderLibrary = shaderLibrary;

    if (!m_impl->device || !m_shaderLibrary || !m_shaderLibrary->isInitialized()) {
        qCritical() << "MetalBondRenderer: invalid device or shader library";
        return false;
    }

    createQuadGeometry();
    m_initialized = true;
    return true;
}

void MetalBondRenderer::cleanup() {
    m_impl->quadVertexBuffer = nil;
    m_impl->instanceBuffer = nil;
    m_bondCount = 0;
    m_initialized = false;
}

void MetalBondRenderer::createQuadGeometry() {
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

void MetalBondRenderer::setBondData(const data::Structure* structure) {
    if (!m_initialized) return;

    if (!structure || structure->bonds().bondCount() == 0) {
        m_bondCount = 0;
        m_impl->instanceBuffer = nil;
        return;
    }

    std::vector<BondRenderSegment> segments = collectBondRenderSegments(structure);
    m_bondCount = segments.size();

    std::vector<BondInstance> instances(m_bondCount);
    for (size_t i = 0; i < m_bondCount; ++i) {
        const BondRenderSegment& segment = segments[i];
        instances[i].start = simd_make_float3(segment.startX, segment.startY, segment.startZ);
        instances[i].end   = simd_make_float3(segment.endX, segment.endY, segment.endZ);
        instances[i].startColor = simd_make_float4(segment.startColorR, segment.startColorG, segment.startColorB, segment.startColorA);
        instances[i].endColor = simd_make_float4(segment.endColorR, segment.endColorG, segment.endColorB, segment.endColorA);
        instances[i].startRadius = segment.startRadius;
        instances[i].endRadius = segment.endRadius;
    }

    NSUInteger size = m_bondCount * sizeof(BondInstance);
    m_impl->instanceBuffer = [m_impl->device newBufferWithBytes:instances.data()
                                                         length:size
                                                        options:MTLResourceStorageModeShared];
}

void MetalBondRenderer::render(void* encoderPtr, const SceneUniforms& uniforms) {
    if (!m_initialized || m_bondCount == 0) return;

    id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)encoderPtr;
    id<MTLRenderPipelineState> pipeline = (__bridge id<MTLRenderPipelineState>)m_shaderLibrary->bondPipeline();
    id<MTLDepthStencilState> depthState = (__bridge id<MTLDepthStencilState>)m_shaderLibrary->depthLessWriteState();

    [encoder setRenderPipelineState:pipeline];
    [encoder setDepthStencilState:depthState];
    [encoder setCullMode:MTLCullModeNone];

    [encoder setVertexBytes:&uniforms length:sizeof(SceneUniforms) atIndex:0];
    [encoder setFragmentBytes:&uniforms length:sizeof(SceneUniforms) atIndex:0];

    // buffer(1): quad vertices
    [encoder setVertexBuffer:m_impl->quadVertexBuffer offset:0 atIndex:1];

    // buffer(2): per-bond instance data
    [encoder setVertexBuffer:m_impl->instanceBuffer offset:0 atIndex:2];

    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:6
              instanceCount:m_bondCount];
}

} // namespace atom::render::metal
