#import <Metal/Metal.h>
#include "MetalSphereRenderer.h"
#include "MetalBufferUtil.h"
#include "MetalShaderLibrary.h"
#include "MetalTypes.h"
#include "../common/BondRenderData.h"
#include "../common/Camera.h"
#include "../../data/Structure.h"
#include <QDebug>
#include <algorithm>
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
    m_hasBounds = false;
    m_maxBaseRadius = 0.0f;
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
        m_hasBounds = false;
        m_maxBaseRadius = 0.0f;
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

    float boundsMin[3] = {px[0], py[0], pz[0]};
    float boundsMax[3] = {px[0], py[0], pz[0]};
    float maxRadius = 0.0f;

    for (size_t i = 0; i < m_atomCount; ++i) {
        instances[i].positionAndRadius = simd_make_float4(px[i], py[i], pz[i], radii[i]);
        instances[i].color = simd_make_float4(colorData[i * 4 + 0],
                                              colorData[i * 4 + 1],
                                              colorData[i * 4 + 2],
                                              colorData[i * 4 + 3]);
        instances[i].selected = structure->atomSelected(i) ? 1.0f : 0.0f;

        boundsMin[0] = std::min(boundsMin[0], px[i]);
        boundsMin[1] = std::min(boundsMin[1], py[i]);
        boundsMin[2] = std::min(boundsMin[2], pz[i]);
        boundsMax[0] = std::max(boundsMax[0], px[i]);
        boundsMax[1] = std::max(boundsMax[1], py[i]);
        boundsMax[2] = std::max(boundsMax[2], pz[i]);
        maxRadius = std::max(maxRadius, radii[i]);
    }

    for (int axis = 0; axis < 3; ++axis) {
        m_boundsMin[axis] = boundsMin[axis];
        m_boundsMax[axis] = boundsMax[axis];
    }
    m_maxBaseRadius = maxRadius;
    m_hasBounds = true;

    // Reuse safe: setAtomData only runs from render() after a free output
    // slot was acquired, i.e. no command buffer is in flight.
    m_impl->instanceBuffer = fillSharedBuffer(m_impl->device, m_impl->instanceBuffer,
                                              instances.data(),
                                              m_atomCount * sizeof(SphereInstance));
}

bool MetalSphereRenderer::canUseEarlyZ(const Camera& camera, float atomScale,
                                       float outlineWidthPx, float outlinePixelScale) const {
    if (!m_initialized || m_atomCount == 0 || !m_hasBounds) return false;

    // Min/max view depth of the atom-center AABB along the camera forward axis.
    const QVector3D camPos = camera.position();
    const QVector3D forward = camera.forwardVector();
    const float cam[3] = {camPos.x(), camPos.y(), camPos.z()};
    const float fwd[3] = {forward.x(), forward.y(), forward.z()};

    float minDepth = 0.0f;
    float maxDepth = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        const float lo = fwd[axis] * (m_boundsMin[axis] - cam[axis]);
        const float hi = fwd[axis] * (m_boundsMax[axis] - cam[axis]);
        minDepth += std::min(lo, hi);
        maxDepth += std::max(lo, hi);
    }

    const float maxScaledRadius = m_maxBaseRadius * atomScale;

    // Conservative outline shell width at the deepest possible atom.
    float maxShellWidth = 0.0f;
    if (outlineWidthPx > 0.0f) {
        maxShellWidth = outlineWidthPx * outlinePixelScale;
        if (camera.isPerspective()) {
            maxShellWidth *= std::max(maxDepth + maxScaledRadius, 0.0f);
        }
    }
    const float maxOuterRadius = maxScaledRadius + maxShellWidth;

    // Every sphere's near-tangent plane must stay safely beyond the near
    // plane; otherwise near-plane clipping could differ from the default
    // billboard placement and we fall back to the depth(any) path.
    const float nearLimit = std::max(camera.nearPlane() * 2.0f, 1e-3f);
    return (minDepth - maxOuterRadius) > nearLimit;
}

void MetalSphereRenderer::render(void* encoderPtr, const SceneUniforms& uniforms) {
    if (!m_initialized || m_atomCount == 0) return;

    id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)encoderPtr;
    id<MTLRenderPipelineState> pipeline = (__bridge id<MTLRenderPipelineState>)(
        uniforms.sphereEarlyZ != 0 ? m_shaderLibrary->spherePipelineEarlyZ()
                                   : m_shaderLibrary->spherePipeline());
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
