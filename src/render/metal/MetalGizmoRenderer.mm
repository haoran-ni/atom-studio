#import <Metal/Metal.h>
#include "MetalGizmoRenderer.h"
#include "MetalShaderLibrary.h"
#include "MetalTypes.h"
#include "MetalUnitCellShared.h"
#include "../common/GizmoOverlay.h"
#include "../common/RenderSettings.h"
#include <QDebug>
#include <array>
#include <vector>

namespace atom::render::metal {

namespace {

constexpr int kGizmoCylinderSegments = 16;
constexpr int kGizmoAxisSegmentCount = 6;

simd_float4x4 qMatToSimd(const QMatrix4x4& matrix) {
    simd_float4x4 result;
    const float* values = matrix.constData();
    for (int column = 0; column < 4; ++column) {
        result.columns[column] = simd_make_float4(values[column * 4], values[column * 4 + 1],
                                                 values[column * 4 + 2], values[column * 4 + 3]);
    }
    return result;
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

    std::vector<BondMeshVertex> vertices;
    std::vector<uint32_t> indices;
    buildCappedUnitCylinderMesh(kGizmoCylinderSegments, vertices, indices);

    m_impl->cylinderVertexBuffer = [m_impl->device
        newBufferWithBytes:vertices.data()
                    length:vertices.size() * sizeof(BondMeshVertex)
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

void MetalGizmoRenderer::render(void* encoderPtr, const Camera& camera,
                                const RenderSettings& settings,
                                int viewportWidth, int viewportHeight)
{
    if (!m_initialized || !m_impl->cylinderVertexBuffer || !m_impl->cylinderIndexBuffer ||
        m_impl->cylinderIndexCount == 0 || !encoderPtr ||
        viewportWidth <= 0 || viewportHeight <= 0) {
        return;
    }

    id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)encoderPtr;

    // 6 axis cylinders: ±X (red), ±Y (green), ±Z (blue)
    const simd_float4 colorX = { 1.0f, 0.25f, 0.25f, 1.0f };
    const simd_float4 colorY = { 0.25f, 1.0f, 0.25f, 1.0f };
    const simd_float4 colorZ = { 0.25f, 0.50f, 1.0f, 1.0f };
    const auto overlay = makeGizmoOverlay(camera, viewportWidth, viewportHeight,
                                         settings.viewportAxesPixelRatio);
    const float len = overlay.axisLength;
    constexpr float cx = 0.0f, cy = 0.0f, cz = 0.0f;

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

    QMatrix4x4 depthBias;
    depthBias(2, 2) = 0.5f;
    depthBias(2, 3) = 0.5f;
    SceneUniforms gizmoUniforms{};
    gizmoUniforms.viewMatrix = qMatToSimd(overlay.viewMatrix);
    gizmoUniforms.projectionMatrix = qMatToSimd(depthBias * overlay.projectionMatrix);
    gizmoUniforms.viewProjectionMatrix = simd_mul(gizmoUniforms.projectionMatrix,
                                                 gizmoUniforms.viewMatrix);
    gizmoUniforms.bondRadius = overlay.radius;
    // Keep gizmo colors flat/unchanged regardless of scene lighting settings.
    gizmoUniforms.ambient = 1.0f;
    gizmoUniforms.diffuse = 0.0f;
    gizmoUniforms.specular = 0.0f;
    gizmoUniforms.shininess = 1.0f;

    id<MTLRenderPipelineState> pipeline =
        (__bridge id<MTLRenderPipelineState>)m_shaderLibrary->solidCylinderPipeline();
    id<MTLDepthStencilState> depthState =
        (__bridge id<MTLDepthStencilState>)m_shaderLibrary->depthLessWriteState();

    [encoder setRenderPipelineState:pipeline];
    [encoder setDepthStencilState:depthState];
    [encoder setFrontFacingWinding:MTLWindingCounterClockwise];
    [encoder setCullMode:MTLCullModeBack];
    [encoder setVertexBytes:&gizmoUniforms length:sizeof(SceneUniforms) atIndex:0];
    [encoder setFragmentBytes:&gizmoUniforms length:sizeof(SceneUniforms) atIndex:0];
    [encoder setVertexBuffer:m_impl->cylinderVertexBuffer offset:0 atIndex:1];

    [encoder setVertexBytes:instances.data() length:sizeof(instances) atIndex:2];
    [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                        indexCount:m_impl->cylinderIndexCount
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:m_impl->cylinderIndexBuffer
                 indexBufferOffset:0
                     instanceCount:kGizmoAxisSegmentCount];
}

} // namespace atom::render::metal
