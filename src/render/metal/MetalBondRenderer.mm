#import <Metal/Metal.h>
#include "MetalBondRenderer.h"
#include "MetalShaderLibrary.h"
#include "MetalTypes.h"
#include "../common/BondRenderData.h"
#include "../../data/Structure.h"
#include <QDebug>
#include <vector>
#include <cmath>

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
    // Unit cylinder along Z from 0 to 1, XY on unit circle
    std::vector<float> vertices;
    std::vector<uint32_t> indices;

    const float PI = 3.14159265358979323846f;

    for (int i = 0; i <= segments; ++i) {
        float angle = (2.0f * PI * i) / segments;
        float x = std::cos(angle);
        float y = std::sin(angle);

        // Bottom ring (z = 0)
        vertices.push_back(x);
        vertices.push_back(y);
        vertices.push_back(0.0f);

        // Top ring (z = 1)
        vertices.push_back(x);
        vertices.push_back(y);
        vertices.push_back(1.0f);
    }

    for (int i = 0; i < segments; ++i) {
        int b0 = i * 2;
        int t0 = i * 2 + 1;
        int b1 = (i + 1) * 2;
        int t1 = (i + 1) * 2 + 1;

        indices.push_back(b0);
        indices.push_back(b1);
        indices.push_back(t0);
        indices.push_back(t0);
        indices.push_back(b1);
        indices.push_back(t1);
    }

    m_cylinderIndexCount = static_cast<int>(indices.size());

    m_impl->cylinderVertexBuffer = [m_impl->device
        newBufferWithBytes:vertices.data()
                    length:vertices.size() * sizeof(float)
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
