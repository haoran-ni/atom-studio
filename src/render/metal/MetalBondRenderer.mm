#import <Metal/Metal.h>
#include "MetalBondRenderer.h"
#include "MetalBufferUtil.h"
#include "MetalShaderLibrary.h"
#include "MetalTypes.h"
#include "MetalUnitCellShared.h"
#include "../common/BondRenderData.h"
#include "../../data/Structure.h"
#include <QDebug>
#include <algorithm>
#include <vector>

namespace atom::render::metal {

// Below this bond count the per-frame compute dispatch costs more than the
// per-vertex recomputation it saves.
static constexpr size_t kBondFramePrecomputeThreshold = 2048;

// Must match the MSL BondFrame struct (4 × float4).
static constexpr size_t kBondFrameStride = 4 * sizeof(simd_float4);

struct MetalBondRenderer::Impl {
    id<MTLDevice> device = nil;
    id<MTLBuffer> cylinderVertexBuffer = nil;
    id<MTLBuffer> cylinderIndexBuffer = nil;
    id<MTLBuffer> instanceBuffer = nil;      // N × BondInstance
    id<MTLBuffer> frameBuffer = nil;         // N × BondFrame (GPU-only)
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

    ensureCylinderGeometry(20);
    m_initialized = true;
    return true;
}

void MetalBondRenderer::cleanup() {
    m_impl->cylinderVertexBuffer = nil;
    m_impl->cylinderIndexBuffer = nil;
    m_impl->instanceBuffer = nil;
    m_impl->frameBuffer = nil;
    m_cylinderIndexCount = 0;
    m_meshSegments = 0;
    m_bondCount = 0;
    m_usePrecomputedFrames = false;
    m_initialized = false;
}

void MetalBondRenderer::createCylinderGeometry(int segments) {
    std::vector<BondMeshVertex> vertices;
    std::vector<uint32_t> indices;
    buildCappedUnitCylinderMesh(segments, vertices, indices);

    m_cylinderIndexCount = static_cast<int>(indices.size());
    m_meshSegments = segments;

    m_impl->cylinderVertexBuffer = [m_impl->device
        newBufferWithBytes:vertices.data()
                    length:vertices.size() * sizeof(BondMeshVertex)
                   options:MTLResourceStorageModeShared];

    m_impl->cylinderIndexBuffer = [m_impl->device
        newBufferWithBytes:indices.data()
                    length:indices.size() * sizeof(uint32_t)
                   options:MTLResourceStorageModeShared];
}

void MetalBondRenderer::ensureCylinderGeometry(int segments) {
    const int clampedSegments = std::max(segments, 3);
    if (m_cylinderIndexCount > 0 && m_meshSegments == clampedSegments &&
        m_impl->cylinderVertexBuffer && m_impl->cylinderIndexBuffer) {
        return;
    }

    createCylinderGeometry(clampedSegments);
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
        instances[i].bondRadius = segment.bondRadius;
    }

    // Reuse safe: setBondData only runs from render() after a free output
    // slot was acquired, i.e. no command buffer is in flight.
    m_impl->instanceBuffer = fillSharedBuffer(m_impl->device, m_impl->instanceBuffer,
                                              instances.data(),
                                              m_bondCount * sizeof(BondInstance));
}

void MetalBondRenderer::encodeFramePrecompute(void* cmdBuf, const SceneUniforms& uniforms) {
    m_usePrecomputedFrames = false;
    if (!m_initialized || m_bondCount < kBondFramePrecomputeThreshold) return;
    if (!m_impl->instanceBuffer) return;

    id<MTLComputePipelineState> pipeline =
        (__bridge id<MTLComputePipelineState>)m_shaderLibrary->bondFrameComputePipeline();
    if (!pipeline) return;

    m_impl->frameBuffer = ensurePrivateBuffer(m_impl->device, m_impl->frameBuffer,
                                              m_bondCount * kBondFrameStride);
    if (!m_impl->frameBuffer) return;

    id<MTLCommandBuffer> cmdBuffer = (__bridge id<MTLCommandBuffer>)cmdBuf;
    id<MTLComputeCommandEncoder> encoder = [cmdBuffer computeCommandEncoder];

    const uint32_t bondCount = static_cast<uint32_t>(m_bondCount);
    [encoder setComputePipelineState:pipeline];
    [encoder setBytes:&uniforms length:sizeof(SceneUniforms) atIndex:0];
    [encoder setBuffer:m_impl->instanceBuffer offset:0 atIndex:1];
    [encoder setBuffer:m_impl->frameBuffer offset:0 atIndex:2];
    [encoder setBytes:&bondCount length:sizeof(bondCount) atIndex:3];

    const NSUInteger threadsPerGroup =
        std::min<NSUInteger>(pipeline.maxTotalThreadsPerThreadgroup, 256);
    const NSUInteger groups = (m_bondCount + threadsPerGroup - 1) / threadsPerGroup;
    [encoder dispatchThreadgroups:MTLSizeMake(groups, 1, 1)
            threadsPerThreadgroup:MTLSizeMake(threadsPerGroup, 1, 1)];
    [encoder endEncoding];

    m_usePrecomputedFrames = true;
}

void MetalBondRenderer::render(void* encoderPtr, const SceneUniforms& uniforms, int cylinderSegments) {
    if (!m_initialized || m_bondCount == 0) return;
    ensureCylinderGeometry(cylinderSegments);
    if (!m_impl->cylinderVertexBuffer || !m_impl->cylinderIndexBuffer || m_cylinderIndexCount == 0) return;

    const bool usePrecomputed = m_usePrecomputedFrames && m_impl->frameBuffer;

    id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)encoderPtr;
    id<MTLRenderPipelineState> pipeline = (__bridge id<MTLRenderPipelineState>)(
        usePrecomputed ? m_shaderLibrary->bondPipelinePrecomputed()
                       : m_shaderLibrary->bondPipeline());
    id<MTLDepthStencilState> depthState = (__bridge id<MTLDepthStencilState>)m_shaderLibrary->depthLessWriteState();

    [encoder setRenderPipelineState:pipeline];
    [encoder setDepthStencilState:depthState];
    [encoder setFrontFacingWinding:MTLWindingCounterClockwise];
    [encoder setCullMode:MTLCullModeBack];

    [encoder setVertexBytes:&uniforms length:sizeof(SceneUniforms) atIndex:0];
    [encoder setFragmentBytes:&uniforms length:sizeof(SceneUniforms) atIndex:0];

    // buffer(1): shared cylinder mesh
    [encoder setVertexBuffer:m_impl->cylinderVertexBuffer offset:0 atIndex:1];

    // buffer(2): per-bond instance data
    [encoder setVertexBuffer:m_impl->instanceBuffer offset:0 atIndex:2];

    // buffer(3): precomputed per-bond frames (precomputed pipelines only)
    if (usePrecomputed) {
        [encoder setVertexBuffer:m_impl->frameBuffer offset:0 atIndex:3];
    }

    [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                        indexCount:m_cylinderIndexCount
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:m_impl->cylinderIndexBuffer
                 indexBufferOffset:0
                     instanceCount:m_bondCount];

    // Stroke outline pass: re-draw the same instanced mesh inflated by the
    // outline width with front faces culled (inverted hull). Buffers 0-3 are
    // already bound; only pipeline and cull mode change.
    if (uniforms.outlineWidthPx > 0.0f) {
        id<MTLRenderPipelineState> outlinePipeline = (__bridge id<MTLRenderPipelineState>)(
            usePrecomputed ? m_shaderLibrary->bondOutlinePipelinePrecomputed()
                           : m_shaderLibrary->bondOutlinePipeline());
        [encoder setRenderPipelineState:outlinePipeline];
        [encoder setCullMode:MTLCullModeFront];

        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:m_cylinderIndexCount
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:m_impl->cylinderIndexBuffer
                     indexBufferOffset:0
                         instanceCount:m_bondCount];

        [encoder setCullMode:MTLCullModeBack];
    }
}

} // namespace atom::render::metal
