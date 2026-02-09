#import <Metal/Metal.h>
#include "MetalUnitCellRenderer.h"
#include "MetalShaderLibrary.h"
#include "MetalTypes.h"
#include "../common/RenderSettings.h"
#include "../../data/Structure.h"

#include <QDebug>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace atom::render::metal {

struct MetalUnitCellRenderer::Impl {
    id<MTLDevice> device = nil;

    // Edge cylinders
    id<MTLBuffer> cylinderVertexBuffer = nil;
    id<MTLBuffer> cylinderIndexBuffer = nil;
    id<MTLBuffer> edgeInstanceBuffer = nil;    // 12 × BondInstance

    // Corner joints (sphere impostor quads)
    id<MTLBuffer> jointQuadVertexBuffer = nil; // 6 × float3
    id<MTLBuffer> jointInstanceBuffer = nil;   // 8 × SphereInstance
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

    createCylinderGeometry(20);
    createJointQuadGeometry();

    m_initialized = true;
    return true;
}

void MetalUnitCellRenderer::cleanup() {
    m_impl->cylinderVertexBuffer = nil;
    m_impl->cylinderIndexBuffer = nil;
    m_impl->edgeInstanceBuffer = nil;
    m_impl->jointQuadVertexBuffer = nil;
    m_impl->jointInstanceBuffer = nil;

    m_edgeCount = 0;
    m_jointCount = 0;
    m_cylinderIndexCount = 0;
    m_initialized = false;
}

void MetalUnitCellRenderer::createCylinderGeometry(int segments) {
    std::vector<float> vertices;
    std::vector<uint32_t> indices;

    const float pi = 3.14159265358979323846f;
    for (int i = 0; i <= segments; ++i) {
        float angle = (2.0f * pi * i) / segments;
        float x = std::cos(angle);
        float y = std::sin(angle);

        vertices.push_back(x);
        vertices.push_back(y);
        vertices.push_back(0.0f);

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

void MetalUnitCellRenderer::createJointQuadGeometry() {
    const float quadVertices[] = {
        -1.0f, -1.0f, 0.0f,
         1.0f, -1.0f, 0.0f,
         1.0f,  1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f,
         1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f,
    };

    m_impl->jointQuadVertexBuffer = [m_impl->device
        newBufferWithBytes:quadVertices
                    length:sizeof(quadVertices)
                   options:MTLResourceStorageModeShared];
}

void MetalUnitCellRenderer::setUnitCellData(const data::Structure* structure) {
    if (!m_initialized) return;

    if (!structure || !structure->hasLattice()) {
        m_edgeCount = 0;
        m_jointCount = 0;
        m_impl->edgeInstanceBuffer = nil;
        m_impl->jointInstanceBuffer = nil;
        return;
    }

    const auto& lattice = structure->lattice();
    const auto& mat = lattice.matrix;

    const float ax = static_cast<float>(mat[0][0]);
    const float ay = static_cast<float>(mat[0][1]);
    const float az = static_cast<float>(mat[0][2]);
    const float bx = static_cast<float>(mat[1][0]);
    const float by = static_cast<float>(mat[1][1]);
    const float bz = static_cast<float>(mat[1][2]);
    const float cx = static_cast<float>(mat[2][0]);
    const float cy = static_cast<float>(mat[2][1]);
    const float cz = static_cast<float>(mat[2][2]);

    const std::array<simd_float3, 8> corners = {{
        simd_make_float3(0.0f, 0.0f, 0.0f),
        simd_make_float3(ax, ay, az),
        simd_make_float3(bx, by, bz),
        simd_make_float3(cx, cy, cz),
        simd_make_float3(ax + bx, ay + by, az + bz),
        simd_make_float3(ax + cx, ay + cy, az + cz),
        simd_make_float3(bx + cx, by + cy, bz + cz),
        simd_make_float3(ax + bx + cx, ay + by + cy, az + bz + cz),
    }};

    const std::array<uint32_t, 24> edgeIndices = {{
        0, 1,   0, 2,   0, 3,
        1, 4,   1, 5,
        2, 4,   2, 6,
        3, 5,   3, 6,
        4, 7,   5, 7,   6, 7
    }};

    const simd_float4 white = simd_make_float4(1.0f, 1.0f, 1.0f, 1.0f);

    std::array<BondInstance, 12> edges{};
    for (int i = 0; i < 12; ++i) {
        edges[i].start = corners[edgeIndices[i * 2 + 0]];
        edges[i].end = corners[edgeIndices[i * 2 + 1]];
        edges[i].color = white;
    }

    std::array<SphereInstance, 8> joints{};
    for (int i = 0; i < 8; ++i) {
        joints[i].positionAndRadius = simd_make_float4(corners[i], 1.0f);
        joints[i].color = white;
    }

    m_impl->edgeInstanceBuffer = [m_impl->device
        newBufferWithBytes:edges.data()
                    length:edges.size() * sizeof(BondInstance)
                   options:MTLResourceStorageModeShared];

    m_impl->jointInstanceBuffer = [m_impl->device
        newBufferWithBytes:joints.data()
                    length:joints.size() * sizeof(SphereInstance)
                   options:MTLResourceStorageModeShared];

    m_edgeCount = 12;
    m_jointCount = 8;
}

void MetalUnitCellRenderer::render(void* encoderPtr,
                                   const SceneUniforms& uniforms,
                                   const RenderSettings& settings) {
    if (!m_initialized || m_edgeCount == 0 || m_jointCount == 0 ||
        !m_impl->edgeInstanceBuffer || !m_impl->jointInstanceBuffer) {
        return;
    }

    id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)encoderPtr;
    id<MTLDepthStencilState> depthState = (__bridge id<MTLDepthStencilState>)m_shaderLibrary->depthLessWriteState();

    const float radius = std::max(settings.unitCellThickness, 0.001f);
    const QColor color = settings.unitCellColor;
    const simd_float4 unitCellColor = simd_make_float4(color.redF(), color.greenF(), color.blueF(), 1.0f);

    // Keep colors dynamic so UI RGB sliders update without geometry rebuild.
    BondInstance* edgeData = static_cast<BondInstance*>([m_impl->edgeInstanceBuffer contents]);
    for (int i = 0; i < m_edgeCount; ++i) {
        edgeData[i].color = unitCellColor;
    }

    SphereInstance* jointData = static_cast<SphereInstance*>([m_impl->jointInstanceBuffer contents]);
    for (int i = 0; i < m_jointCount; ++i) {
        jointData[i].color = unitCellColor;
    }

    SceneUniforms unitCellUniforms = uniforms;
    unitCellUniforms.atomScale = radius;
    unitCellUniforms.bondRadius = radius;

    // Flat color: no shading terms for the unit-cell object.
    unitCellUniforms.ambient = 1.0f;
    unitCellUniforms.diffuse = 0.0f;
    unitCellUniforms.specular = 0.0f;
    unitCellUniforms.shininess = 1.0f;

    // Draw edge cylinders.
    {
        id<MTLRenderPipelineState> pipeline = (__bridge id<MTLRenderPipelineState>)m_shaderLibrary->bondPipeline();
        [encoder setRenderPipelineState:pipeline];
        [encoder setDepthStencilState:depthState];
        // Mesh indices are authored CCW; make winding explicit so back-face culling
        // consistently removes the far shell instead of the near shell.
        [encoder setFrontFacingWinding:MTLWindingCounterClockwise];
        [encoder setCullMode:MTLCullModeBack];

        [encoder setVertexBytes:&unitCellUniforms length:sizeof(SceneUniforms) atIndex:0];
        [encoder setFragmentBytes:&unitCellUniforms length:sizeof(SceneUniforms) atIndex:0];
        [encoder setVertexBuffer:m_impl->cylinderVertexBuffer offset:0 atIndex:1];
        [encoder setVertexBuffer:m_impl->edgeInstanceBuffer offset:0 atIndex:2];

        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:m_cylinderIndexCount
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:m_impl->cylinderIndexBuffer
                     indexBufferOffset:0
                         instanceCount:m_edgeCount];
    }

    // Draw corner joints as sphere impostors.
    {
        id<MTLRenderPipelineState> pipeline = (__bridge id<MTLRenderPipelineState>)m_shaderLibrary->spherePipeline();
        [encoder setRenderPipelineState:pipeline];
        [encoder setDepthStencilState:depthState];
        [encoder setCullMode:MTLCullModeNone];

        [encoder setVertexBytes:&unitCellUniforms length:sizeof(SceneUniforms) atIndex:0];
        [encoder setFragmentBytes:&unitCellUniforms length:sizeof(SceneUniforms) atIndex:0];
        [encoder setVertexBuffer:m_impl->jointQuadVertexBuffer offset:0 atIndex:1];
        [encoder setVertexBuffer:m_impl->jointInstanceBuffer offset:0 atIndex:2];

        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:0
                    vertexCount:6
                  instanceCount:m_jointCount];
    }
}

} // namespace atom::render::metal
