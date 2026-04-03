#import <Metal/Metal.h>
#include "MetalGizmoRenderer.h"
#include "MetalShaderLibrary.h"
#include "MetalTypes.h"
#include <QDebug>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace atom::render::metal {

namespace {

constexpr int kGizmoCylinderSegments = 16;
constexpr int kGizmoAxisSegmentCount = 6;
constexpr float kGizmoRadiusToLength = 0.05f;

void buildCappedUnitCylinderMesh(int segments,
                                 std::vector<float>& vertices,
                                 std::vector<uint32_t>& indices) {
    vertices.clear();
    indices.clear();

    const float pi = 3.14159265358979323846f;
    for (int i = 0; i <= segments; ++i) {
        const float angle = (2.0f * pi * i) / segments;
        const float x = std::cos(angle);
        const float y = std::sin(angle);

        // Side wall ring vertices (unit cylinder aligned to +Z from 0 to 1).
        vertices.push_back(x); vertices.push_back(y); vertices.push_back(0.0f); // bottom
        vertices.push_back(x); vertices.push_back(y); vertices.push_back(1.0f); // top
    }

    // Side triangles.
    for (int i = 0; i < segments; ++i) {
        const int b0 = i * 2;
        const int t0 = i * 2 + 1;
        const int b1 = (i + 1) * 2;
        const int t1 = (i + 1) * 2 + 1;

        indices.push_back(b0); indices.push_back(b1); indices.push_back(t0);
        indices.push_back(t0); indices.push_back(b1); indices.push_back(t1);
    }

    const uint32_t bottomCenter = static_cast<uint32_t>(vertices.size() / 3);
    vertices.push_back(0.0f); vertices.push_back(0.0f); vertices.push_back(0.0f);
    const uint32_t topCenter = static_cast<uint32_t>(vertices.size() / 3);
    vertices.push_back(0.0f); vertices.push_back(0.0f); vertices.push_back(1.0f);

    // Cap triangles (CCW front faces with outward normals in local space).
    for (int i = 0; i < segments; ++i) {
        const uint32_t b0 = static_cast<uint32_t>(i * 2);
        const uint32_t t0 = static_cast<uint32_t>(i * 2 + 1);
        const uint32_t b1 = static_cast<uint32_t>((i + 1) * 2);
        const uint32_t t1 = static_cast<uint32_t>((i + 1) * 2 + 1);

        // Bottom cap faces -Z (reverse order when viewed from +Z).
        indices.push_back(bottomCenter); indices.push_back(b1); indices.push_back(b0);
        // Top cap faces +Z.
        indices.push_back(topCenter); indices.push_back(t0); indices.push_back(t1);
    }
}

} // namespace

struct MetalGizmoRenderer::Impl {
    id<MTLDevice> device = nil;
    id<MTLBuffer> cylinderVertexBuffer = nil;
    id<MTLBuffer> cylinderIndexBuffer = nil;
    int cylinderIndexCount = 0;
};

MetalGizmoRenderer::MetalGizmoRenderer()
    : m_impl(std::make_unique<Impl>())
{
}

MetalGizmoRenderer::~MetalGizmoRenderer() {
    cleanup();
}

bool MetalGizmoRenderer::initialize(void* device, MetalShaderLibrary* shaderLibrary) {
    if (m_initialized) return true;

    m_impl->device = (__bridge id<MTLDevice>)device;
    m_shaderLibrary = shaderLibrary;

    if (!m_impl->device || !m_shaderLibrary || !m_shaderLibrary->isInitialized()) {
        qCritical() << "MetalGizmoRenderer: invalid device or shader library";
        return false;
    }

    std::vector<float> vertices;
    std::vector<uint32_t> indices;
    buildCappedUnitCylinderMesh(kGizmoCylinderSegments, vertices, indices);

    m_impl->cylinderVertexBuffer = [m_impl->device
        newBufferWithBytes:vertices.data()
                    length:vertices.size() * sizeof(float)
                   options:MTLResourceStorageModeShared];

    m_impl->cylinderIndexBuffer = [m_impl->device
        newBufferWithBytes:indices.data()
                    length:indices.size() * sizeof(uint32_t)
                   options:MTLResourceStorageModeShared];

    if (!m_impl->cylinderVertexBuffer || !m_impl->cylinderIndexBuffer) {
        qCritical() << "MetalGizmoRenderer: failed to create cylinder buffers";
        cleanup();
        return false;
    }

    m_impl->cylinderIndexCount = static_cast<int>(indices.size());
    m_initialized = true;
    return true;
}

void MetalGizmoRenderer::cleanup() {
    m_impl->cylinderVertexBuffer = nil;
    m_impl->cylinderIndexBuffer = nil;
    m_impl->cylinderIndexCount = 0;
    m_impl->device = nil;
    m_shaderLibrary = nullptr;
    m_initialized = false;
}

void MetalGizmoRenderer::render(void* encoderPtr, const SceneUniforms& uniforms,
                                float cx, float cy, float cz, float axisLength,
                                bool depthTest)
{
    if (!m_initialized || !m_impl->cylinderVertexBuffer || !m_impl->cylinderIndexBuffer ||
        m_impl->cylinderIndexCount == 0) {
        return;
    }

    id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)encoderPtr;

    // 6 axis cylinders: ±X (red), ±Y (green), ±Z (blue)
    const simd_float4 colorX = { 1.0f, 0.25f, 0.25f, 1.0f };
    const simd_float4 colorY = { 0.25f, 1.0f, 0.25f, 1.0f };
    const simd_float4 colorZ = { 0.25f, 0.50f, 1.0f, 1.0f };
    const float len = axisLength;

    std::array<BondInstance, kGizmoAxisSegmentCount> instances{};
    instances[0].start = simd_make_float3(cx, cy, cz);
    instances[0].end   = simd_make_float3(cx + len, cy, cz);
    instances[0].startColor = colorX;
    instances[0].endColor = colorX;
    instances[1].start = simd_make_float3(cx, cy, cz);
    instances[1].end   = simd_make_float3(cx - len, cy, cz);
    instances[1].startColor = colorX;
    instances[1].endColor = colorX;
    instances[2].start = simd_make_float3(cx, cy, cz);
    instances[2].end   = simd_make_float3(cx, cy + len, cz);
    instances[2].startColor = colorY;
    instances[2].endColor = colorY;
    instances[3].start = simd_make_float3(cx, cy, cz);
    instances[3].end   = simd_make_float3(cx, cy - len, cz);
    instances[3].startColor = colorY;
    instances[3].endColor = colorY;
    instances[4].start = simd_make_float3(cx, cy, cz);
    instances[4].end   = simd_make_float3(cx, cy, cz + len);
    instances[4].startColor = colorZ;
    instances[4].endColor = colorZ;
    instances[5].start = simd_make_float3(cx, cy, cz);
    instances[5].end   = simd_make_float3(cx, cy, cz - len);
    instances[5].startColor = colorZ;
    instances[5].endColor = colorZ;

    SceneUniforms gizmoUniforms = uniforms;
    gizmoUniforms.bondRadius = std::max(len * kGizmoRadiusToLength, 0.001f);
    // Keep gizmo colors flat/unchanged regardless of scene lighting settings.
    gizmoUniforms.ambient = 1.0f;
    gizmoUniforms.diffuse = 0.0f;
    gizmoUniforms.specular = 0.0f;
    gizmoUniforms.shininess = 1.0f;

    id<MTLRenderPipelineState> pipeline =
        (__bridge id<MTLRenderPipelineState>)m_shaderLibrary->bondPipeline();
    id<MTLDepthStencilState> depthState = depthTest
        ? (__bridge id<MTLDepthStencilState>)m_shaderLibrary->depthLessWriteState()
        : (__bridge id<MTLDepthStencilState>)m_shaderLibrary->depthDisabledState();

    [encoder setRenderPipelineState:pipeline];
    [encoder setDepthStencilState:depthState];
    [encoder setFrontFacingWinding:MTLWindingCounterClockwise];
    [encoder setCullMode:MTLCullModeBack];
    [encoder setVertexBytes:&gizmoUniforms length:sizeof(SceneUniforms) atIndex:0];
    [encoder setFragmentBytes:&gizmoUniforms length:sizeof(SceneUniforms) atIndex:0];
    [encoder setVertexBuffer:m_impl->cylinderVertexBuffer offset:0 atIndex:1];

    if (!depthTest) {
        // Overlay mode ignores scene depth. Sort back-to-front in view space so the
        // gizmo's own axis depth relationships still read correctly.
        auto viewMidZ = [&uniforms](const BondInstance& inst) {
            const simd_float3 mid = 0.5f * (inst.start + inst.end);
            const simd_float4 viewPos = simd_mul(
                uniforms.viewMatrix,
                simd_make_float4(mid.x, mid.y, mid.z, 1.0f));
            return viewPos.z;
        };

        std::sort(instances.begin(), instances.end(),
                  [&viewMidZ](const BondInstance& a, const BondInstance& b) {
                      // OpenGL-style view space: more negative z is farther away.
                      return viewMidZ(a) < viewMidZ(b);
                  });

        for (const BondInstance& inst : instances) {
            [encoder setVertexBytes:&inst length:sizeof(BondInstance) atIndex:2];
            [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:m_impl->cylinderIndexCount
                                 indexType:MTLIndexTypeUInt32
                               indexBuffer:m_impl->cylinderIndexBuffer
                         indexBufferOffset:0
                             instanceCount:1];
        }
    } else {
        [encoder setVertexBytes:instances.data() length:sizeof(instances) atIndex:2];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:m_impl->cylinderIndexCount
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:m_impl->cylinderIndexBuffer
                     indexBufferOffset:0
                         instanceCount:kGizmoAxisSegmentCount];
    }
}

} // namespace atom::render::metal
