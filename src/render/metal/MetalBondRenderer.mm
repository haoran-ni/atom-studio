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
    id<MTLBuffer> cylinderVertexBuffer = nil;
    id<MTLBuffer> cylinderIndexBuffer = nil;
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

    createCylinderGeometry(12);
    m_initialized = true;
    return true;
}

void MetalBondRenderer::cleanup() {
    m_impl->cylinderVertexBuffer = nil;
    m_impl->cylinderIndexBuffer = nil;
    m_impl->instanceBuffer = nil;
    m_bondCount = 0;
    m_cylinderIndexCount = 0;
    m_initialized = false;
}

void MetalBondRenderer::createCylinderGeometry(int segments) {
    std::vector<BondMeshVertex> vertices;
    std::vector<uint32_t> indices;
    buildCappedUnitCylinderMesh(segments, vertices, indices);

    m_cylinderIndexCount = static_cast<int>(indices.size());

    m_impl->cylinderVertexBuffer = [m_impl->device
        newBufferWithBytes:vertices.data()
                    length:vertices.size() * sizeof(BondMeshVertex)
                   options:MTLResourceStorageModeShared];

    m_impl->cylinderIndexBuffer = [m_impl->device
        newBufferWithBytes:indices.data()
                    length:indices.size() * sizeof(uint32_t)
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
    // Mesh indices are authored CCW; make winding explicit instead of relying on
    // Metal's encoder default.
    [encoder setFrontFacingWinding:MTLWindingCounterClockwise];
    [encoder setCullMode:MTLCullModeBack];

    [encoder setVertexBytes:&uniforms length:sizeof(SceneUniforms) atIndex:0];
    [encoder setFragmentBytes:&uniforms length:sizeof(SceneUniforms) atIndex:0];

    // buffer(1): cylinder mesh vertices
    [encoder setVertexBuffer:m_impl->cylinderVertexBuffer offset:0 atIndex:1];

    // buffer(2): per-bond instance data
    [encoder setVertexBuffer:m_impl->instanceBuffer offset:0 atIndex:2];

    [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                        indexCount:m_cylinderIndexCount
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:m_impl->cylinderIndexBuffer
                 indexBufferOffset:0
                     instanceCount:m_bondCount];
}

} // namespace atom::render::metal
