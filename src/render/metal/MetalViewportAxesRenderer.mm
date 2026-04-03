#import <Metal/Metal.h>

#include "MetalViewportAxesRenderer.h"
#include "MetalShaderLibrary.h"
#include "MetalTypes.h"
#include "../common/Camera.h"
#include "../common/RenderSettings.h"

#include <QDebug>
#include <QMatrix4x4>
#include <QVector3D>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace atom::render::metal {

namespace {

constexpr int kAxesCylinderSegments = 32;
constexpr int kAxesConeSegments = 32;
constexpr int kCenterSphereStacks = 12;
constexpr int kCenterSphereSegments = 18;
constexpr float kOverlayDepthRange = 512.0f;

struct AxisDesc {
    QVector3D screenDir;  // x=screen right, y=screen down, z=self-depth
    simd_float4 color;
};

static simd_float4x4 qMatToSimd(const QMatrix4x4& m) {
    simd_float4x4 result;
    std::memcpy(&result, m.constData(), 16 * sizeof(float));
    return result;
}

static simd_float4x4 remapDepthToMetal(const QMatrix4x4& proj) {
    QMatrix4x4 bias;
    bias(2, 2) = 0.5f;
    bias(2, 3) = 0.5f;
    return qMatToSimd(bias * proj);
}

static void buildCappedCylinderMesh(int segments,
                                    std::vector<float>& vertices,
                                    std::vector<uint32_t>& indices) {
    vertices.clear();
    indices.clear();

    const float pi = 3.14159265358979323846f;
    for (int i = 0; i <= segments; ++i) {
        const float angle = (2.0f * pi * i) / static_cast<float>(segments);
        const float x = std::cos(angle);
        const float y = std::sin(angle);

        vertices.push_back(x); vertices.push_back(y); vertices.push_back(0.0f);
        vertices.push_back(x); vertices.push_back(y); vertices.push_back(1.0f);
    }

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

    for (int i = 0; i < segments; ++i) {
        const uint32_t b0 = static_cast<uint32_t>(i * 2);
        const uint32_t t0 = static_cast<uint32_t>(i * 2 + 1);
        const uint32_t b1 = static_cast<uint32_t>((i + 1) * 2);
        const uint32_t t1 = static_cast<uint32_t>((i + 1) * 2 + 1);

        // Bottom cap (z=0): single-sided; the center sphere covers the junction.
        indices.push_back(bottomCenter); indices.push_back(b1); indices.push_back(b0);
        // Top cap faces +Z.
        indices.push_back(topCenter); indices.push_back(t0); indices.push_back(t1);
    }
}

static void buildCappedConeMesh(int segments,
                                std::vector<float>& vertices,
                                std::vector<uint32_t>& indices) {
    vertices.clear();
    indices.clear();

    const float pi = 3.14159265358979323846f;
    for (int i = 0; i <= segments; ++i) {
        const float angle = (2.0f * pi * i) / static_cast<float>(segments);
        vertices.push_back(std::cos(angle));
        vertices.push_back(std::sin(angle));
        vertices.push_back(0.0f);
    }

    const uint32_t tipIndex = static_cast<uint32_t>(vertices.size() / 3);
    vertices.push_back(0.0f); vertices.push_back(0.0f); vertices.push_back(1.0f);
    const uint32_t baseCenterIndex = static_cast<uint32_t>(vertices.size() / 3);
    vertices.push_back(0.0f); vertices.push_back(0.0f); vertices.push_back(0.0f);

    for (int i = 0; i < segments; ++i) {
        const uint32_t r0 = static_cast<uint32_t>(i);
        const uint32_t r1 = static_cast<uint32_t>(i + 1);
        indices.push_back(r0); indices.push_back(r1); indices.push_back(tipIndex);
        indices.push_back(baseCenterIndex); indices.push_back(r1); indices.push_back(r0);
    }
}

static void buildCenterSphereMesh(int stacks, int segments,
                                  std::vector<float>& vertices,
                                  std::vector<uint32_t>& indices) {
    vertices.clear();
    indices.clear();

    vertices.reserve(static_cast<size_t>((stacks + 1) * (segments + 1)) * 3);
    indices.reserve(static_cast<size_t>(stacks * segments) * 6);

    const float pi = 3.14159265358979323846f;
    for (int stack = 0; stack <= stacks; ++stack) {
        const float v = static_cast<float>(stack) / static_cast<float>(stacks);
        const float phi = -0.5f * pi + v * pi;
        const float ringR = std::cos(phi);
        const float zUnit = std::sin(phi);
        const float z01 = 0.5f * (zUnit + 1.0f);

        for (int seg = 0; seg <= segments; ++seg) {
            const float u = static_cast<float>(seg) / static_cast<float>(segments);
            const float theta = u * 2.0f * pi;
            vertices.push_back(std::cos(theta) * ringR);
            vertices.push_back(std::sin(theta) * ringR);
            vertices.push_back(z01);
        }
    }

    const int row = segments + 1;
    for (int stack = 0; stack < stacks; ++stack) {
        for (int seg = 0; seg < segments; ++seg) {
            const uint32_t a = static_cast<uint32_t>(stack * row + seg);
            const uint32_t b = a + 1;
            const uint32_t c = static_cast<uint32_t>((stack + 1) * row + seg);
            const uint32_t d = c + 1;
            indices.push_back(a); indices.push_back(c); indices.push_back(b);
            indices.push_back(b); indices.push_back(c); indices.push_back(d);
        }
    }
}

} // namespace

struct MetalViewportAxesRenderer::Impl {
    id<MTLDevice> device = nil;
    id<MTLBuffer> cylinderVertexBuffer = nil;
    id<MTLBuffer> cylinderIndexBuffer = nil;
    int cylinderIndexCount = 0;
    id<MTLBuffer> coneVertexBuffer = nil;
    id<MTLBuffer> coneIndexBuffer = nil;
    int coneIndexCount = 0;
    id<MTLBuffer> centerSphereVertexBuffer = nil;
    id<MTLBuffer> centerSphereIndexBuffer = nil;
    int centerSphereIndexCount = 0;
};

MetalViewportAxesRenderer::MetalViewportAxesRenderer()
    : m_impl(std::make_unique<Impl>()) {}

MetalViewportAxesRenderer::~MetalViewportAxesRenderer() {
    cleanup();
}

bool MetalViewportAxesRenderer::initialize(void* device, MetalShaderLibrary* shaderLibrary) {
    if (m_initialized) return true;

    m_impl->device = (__bridge id<MTLDevice>)device;
    m_shaderLibrary = shaderLibrary;
    if (!m_impl->device || !m_shaderLibrary || !m_shaderLibrary->isInitialized()) {
        qCritical() << "MetalViewportAxesRenderer: invalid device or shader library";
        return false;
    }

    std::vector<float> vertices;
    std::vector<uint32_t> indices;

    buildCappedCylinderMesh(kAxesCylinderSegments, vertices, indices);
    m_impl->cylinderVertexBuffer = [m_impl->device
        newBufferWithBytes:vertices.data()
                    length:vertices.size() * sizeof(float)
                   options:MTLResourceStorageModeShared];
    m_impl->cylinderIndexBuffer = [m_impl->device
        newBufferWithBytes:indices.data()
                    length:indices.size() * sizeof(uint32_t)
                   options:MTLResourceStorageModeShared];
    m_impl->cylinderIndexCount = static_cast<int>(indices.size());

    buildCappedConeMesh(kAxesConeSegments, vertices, indices);
    m_impl->coneVertexBuffer = [m_impl->device
        newBufferWithBytes:vertices.data()
                    length:vertices.size() * sizeof(float)
                   options:MTLResourceStorageModeShared];
    m_impl->coneIndexBuffer = [m_impl->device
        newBufferWithBytes:indices.data()
                    length:indices.size() * sizeof(uint32_t)
                   options:MTLResourceStorageModeShared];
    m_impl->coneIndexCount = static_cast<int>(indices.size());

    buildCenterSphereMesh(kCenterSphereStacks, kCenterSphereSegments, vertices, indices);
    m_impl->centerSphereVertexBuffer = [m_impl->device
        newBufferWithBytes:vertices.data()
                    length:vertices.size() * sizeof(float)
                   options:MTLResourceStorageModeShared];
    m_impl->centerSphereIndexBuffer = [m_impl->device
        newBufferWithBytes:indices.data()
                    length:indices.size() * sizeof(uint32_t)
                   options:MTLResourceStorageModeShared];
    m_impl->centerSphereIndexCount = static_cast<int>(indices.size());

    if (!m_impl->cylinderVertexBuffer || !m_impl->cylinderIndexBuffer ||
        !m_impl->coneVertexBuffer || !m_impl->coneIndexBuffer ||
        !m_impl->centerSphereVertexBuffer || !m_impl->centerSphereIndexBuffer ||
        m_impl->cylinderIndexCount <= 0 || m_impl->coneIndexCount <= 0 ||
        m_impl->centerSphereIndexCount <= 0) {
        qCritical() << "MetalViewportAxesRenderer: failed to create geometry buffers";
        cleanup();
        return false;
    }

    m_initialized = true;
    return true;
}

void MetalViewportAxesRenderer::cleanup() {
    m_impl->cylinderVertexBuffer = nil;
    m_impl->cylinderIndexBuffer = nil;
    m_impl->cylinderIndexCount = 0;
    m_impl->coneVertexBuffer = nil;
    m_impl->coneIndexBuffer = nil;
    m_impl->coneIndexCount = 0;
    m_impl->centerSphereVertexBuffer = nil;
    m_impl->centerSphereIndexBuffer = nil;
    m_impl->centerSphereIndexCount = 0;
    m_impl->device = nil;
    m_shaderLibrary = nullptr;
    m_initialized = false;
}

void MetalViewportAxesRenderer::render(void* encoderPtr,
                                       const atom::render::Camera& camera,
                                       const atom::render::RenderSettings& settings,
                                       int viewportWidth,
                                       int viewportHeight) {
    if (!m_initialized || !encoderPtr || !settings.showViewportAxes ||
        viewportWidth <= 0 || viewportHeight <= 0) {
        return;
    }

    id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)encoderPtr;
    id<MTLRenderPipelineState> pipeline =
        (__bridge id<MTLRenderPipelineState>)m_shaderLibrary->viewportAxesPipeline();
    id<MTLDepthStencilState> depthState =
        (__bridge id<MTLDepthStencilState>)m_shaderLibrary->depthLessWriteState();
    if (!pipeline || !depthState) {
        return;
    }

    const float scale = std::max(settings.viewportAxesScale, 0.05f);
    const float dpr = std::max(settings.viewportAxesPixelRatio, 1.0f);
    const float pxScale = scale * dpr;
    const float len = 68.0f * pxScale;
    const float headLen = std::min(len * 0.45f, std::max(6.0f * pxScale, 14.0f * pxScale));
    const float shaftLen = std::max(0.0f, len - headLen);
    const float shaftRadius = std::max(3.0f * pxScale, 2.0f * dpr);
    const float headRadius = shaftRadius * 1.9f;

    const QMatrix4x4 viewMat = camera.viewMatrix();
    const std::array<AxisDesc, 3> axes = {{
        { QVector3D(viewMat(0, 0), -viewMat(1, 0), viewMat(2, 0)), simd_make_float4(1.0f, 68.0f / 255.0f, 68.0f / 255.0f, 1.0f) },
        { QVector3D(viewMat(0, 1), -viewMat(1, 1), viewMat(2, 1)), simd_make_float4(68.0f / 255.0f, 1.0f, 68.0f / 255.0f, 1.0f) },
        { QVector3D(viewMat(0, 2), -viewMat(1, 2), viewMat(2, 2)), simd_make_float4(68.0f / 255.0f, 68.0f / 255.0f, 1.0f, 1.0f) }
    }};

    std::array<BondInstance, 3> cylinderInstances{};
    std::array<BondInstance, 3> coneInstances{};
    int cylinderCount = 0;
    int coneCount = 0;

    const simd_float3 origin = simd_make_float3(
        settings.viewportAxesScreenX, settings.viewportAxesScreenY, 0.0f);

    for (const AxisDesc& axis : axes) {
        QVector3D dir = axis.screenDir;
        if (dir.lengthSquared() < 1e-8f) continue;
        dir.normalize();

        const simd_float3 sdir = simd_make_float3(dir.x(), dir.y(), dir.z());

        simd_float3 shaftEnd = origin + sdir * shaftLen;
        if (cylinderCount < static_cast<int>(cylinderInstances.size())) {
            BondInstance& inst = cylinderInstances[static_cast<size_t>(cylinderCount++)];
            inst.start = origin;
            inst.end = shaftEnd;
            inst.startColor = axis.color;
            inst.endColor = axis.color;
        }

        if (headLen > 0.0f && coneCount < static_cast<int>(coneInstances.size())) {
            BondInstance& inst = coneInstances[static_cast<size_t>(coneCount++)];
            inst.start = shaftEnd;
            inst.end = origin + sdir * len;
            inst.startColor = axis.color;
            inst.endColor = axis.color;
        }
    }

    if (cylinderCount == 0 && coneCount == 0) {
        return;
    }

    QMatrix4x4 overlayView;
    overlayView.setToIdentity();
    QMatrix4x4 overlayProj;
    overlayProj.ortho(0.0f, static_cast<float>(viewportWidth),
                      static_cast<float>(viewportHeight), 0.0f,
                      -kOverlayDepthRange, kOverlayDepthRange);

    SceneUniforms uniforms{};
    uniforms.viewMatrix = qMatToSimd(overlayView);
    uniforms.projectionMatrix = remapDepthToMetal(overlayProj);
    uniforms.viewProjectionMatrix = simd_mul(uniforms.projectionMatrix, uniforms.viewMatrix);
    uniforms.lightDir = simd_make_float3(0.0f, 0.0f, 1.0f);
    uniforms.ambient = 1.0f;
    uniforms.diffuse = 0.0f;
    uniforms.specular = 0.0f;
    uniforms.shininess = 1.0f;
    uniforms.atomScale = 1.0f;

    [encoder setRenderPipelineState:pipeline];
    [encoder setDepthStencilState:depthState];
    [encoder setFrontFacingWinding:MTLWindingCounterClockwise];
    [encoder setCullMode:MTLCullModeNone];
    [encoder setVertexBytes:&uniforms length:sizeof(SceneUniforms) atIndex:0];
    [encoder setFragmentBytes:&uniforms length:sizeof(SceneUniforms) atIndex:0];

    if (cylinderCount > 0) {
        uniforms.bondRadius = shaftRadius;
        [encoder setVertexBytes:&uniforms length:sizeof(SceneUniforms) atIndex:0];
        [encoder setFragmentBytes:&uniforms length:sizeof(SceneUniforms) atIndex:0];
        [encoder setVertexBuffer:m_impl->cylinderVertexBuffer offset:0 atIndex:1];
        [encoder setVertexBytes:cylinderInstances.data()
                         length:sizeof(BondInstance) * static_cast<size_t>(cylinderCount)
                        atIndex:2];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:m_impl->cylinderIndexCount
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:m_impl->cylinderIndexBuffer
                     indexBufferOffset:0
                         instanceCount:cylinderCount];
    }

    if (coneCount > 0) {
        uniforms.bondRadius = headRadius;
        [encoder setVertexBytes:&uniforms length:sizeof(SceneUniforms) atIndex:0];
        [encoder setFragmentBytes:&uniforms length:sizeof(SceneUniforms) atIndex:0];
        [encoder setVertexBuffer:m_impl->coneVertexBuffer offset:0 atIndex:1];
        [encoder setVertexBytes:coneInstances.data()
                         length:sizeof(BondInstance) * static_cast<size_t>(coneCount)
                        atIndex:2];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:m_impl->coneIndexCount
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:m_impl->coneIndexBuffer
                     indexBufferOffset:0
                         instanceCount:coneCount];
    }

}

} // namespace atom::render::metal
