#import <Metal/Metal.h>
#include "MetalUnitCellRenderer.h"
#include "MetalShaderLibrary.h"
#include "MetalTypes.h"
#include "../../data/Structure.h"
#include <QDebug>
#include <array>

namespace atom::render::metal {

struct MetalUnitCellRenderer::Impl {
    id<MTLDevice> device = nil;
    id<MTLBuffer> vertexBuffer = nil;    // 8 × LineVertex
    id<MTLBuffer> indexBuffer = nil;     // 24 × uint32 (12 edges × 2)
};

MetalUnitCellRenderer::MetalUnitCellRenderer()
    : m_impl(std::make_unique<Impl>())
{
}

MetalUnitCellRenderer::~MetalUnitCellRenderer() {
    cleanup();
}

bool MetalUnitCellRenderer::initialize(void* device, MetalShaderLibrary* shaderLibrary) {
    if (m_initialized) return true;

    m_impl->device = (__bridge id<MTLDevice>)device;
    m_shaderLibrary = shaderLibrary;

    if (!m_impl->device || !m_shaderLibrary || !m_shaderLibrary->isInitialized()) {
        qCritical() << "MetalUnitCellRenderer: invalid device or shader library";
        return false;
    }

    // Pre-create index buffer (edges never change)
    std::array<uint32_t, 24> indices = {{
        0, 1,   0, 2,   0, 3,
        1, 4,   1, 5,
        2, 4,   2, 6,
        3, 5,   3, 6,
        4, 7,   5, 7,   6, 7
    }};

    m_impl->indexBuffer = [m_impl->device newBufferWithBytes:indices.data()
                                                      length:indices.size() * sizeof(uint32_t)
                                                     options:MTLResourceStorageModeShared];

    m_initialized = true;
    return true;
}

void MetalUnitCellRenderer::cleanup() {
    m_impl->vertexBuffer = nil;
    m_impl->indexBuffer = nil;
    m_edgeCount = 0;
    m_initialized = false;
}

void MetalUnitCellRenderer::setUnitCellData(const data::Structure* structure) {
    if (!m_initialized) return;

    if (!structure || !structure->hasLattice()) {
        m_edgeCount = 0;
        m_impl->vertexBuffer = nil;
        return;
    }

    const auto& lattice = structure->lattice();
    const auto& mat = lattice.matrix;

    float ax = static_cast<float>(mat[0][0]), ay = static_cast<float>(mat[0][1]), az = static_cast<float>(mat[0][2]);
    float bx = static_cast<float>(mat[1][0]), by = static_cast<float>(mat[1][1]), bz = static_cast<float>(mat[1][2]);
    float cx = static_cast<float>(mat[2][0]), cy = static_cast<float>(mat[2][1]), cz = static_cast<float>(mat[2][2]);

    // 8 corners — color set to white, updated at render time
    simd_float4 white = simd_make_float4(1.0f, 1.0f, 1.0f, 1.0f);
    std::array<LineVertex, 8> vertices = {{
        { simd_make_float3(0, 0, 0),                               0, white },
        { simd_make_float3(ax, ay, az),                             0, white },
        { simd_make_float3(bx, by, bz),                             0, white },
        { simd_make_float3(cx, cy, cz),                             0, white },
        { simd_make_float3(ax + bx, ay + by, az + bz),             0, white },
        { simd_make_float3(ax + cx, ay + cy, az + cz),             0, white },
        { simd_make_float3(bx + cx, by + cy, bz + cz),             0, white },
        { simd_make_float3(ax + bx + cx, ay + by + cy, az + bz + cz), 0, white },
    }};

    m_edgeCount = 12;

    m_impl->vertexBuffer = [m_impl->device newBufferWithBytes:vertices.data()
                                                       length:vertices.size() * sizeof(LineVertex)
                                                      options:MTLResourceStorageModeShared];
}

void MetalUnitCellRenderer::render(void* encoderPtr, const SceneUniforms& uniforms) {
    if (!m_initialized || m_edgeCount == 0 || !m_impl->vertexBuffer) return;

    id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)encoderPtr;
    id<MTLRenderPipelineState> pipeline = (__bridge id<MTLRenderPipelineState>)m_shaderLibrary->linePipeline();
    id<MTLDepthStencilState> depthState = (__bridge id<MTLDepthStencilState>)m_shaderLibrary->depthLessWriteState();

    [encoder setRenderPipelineState:pipeline];
    [encoder setDepthStencilState:depthState];
    [encoder setCullMode:MTLCullModeNone];

    [encoder setVertexBytes:&uniforms length:sizeof(SceneUniforms) atIndex:0];

    // buffer(1): LineVertex array (position + color per vertex)
    [encoder setVertexBuffer:m_impl->vertexBuffer offset:0 atIndex:1];

    [encoder drawIndexedPrimitives:MTLPrimitiveTypeLine
                        indexCount:m_edgeCount * 2
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:m_impl->indexBuffer
                 indexBufferOffset:0];
}

} // namespace atom::render::metal
