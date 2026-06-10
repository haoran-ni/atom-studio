#import <Metal/Metal.h>
#include "MetalUnitCellRenderer.h"
#include "MetalShaderLibrary.h"
#include "MetalUnitCellShared.h"
#include "MetalTypes.h"
#include "../common/RenderSettings.h"
#include "../../data/Structure.h"

#include <QDebug>

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

    createCylinderGeometry(48);
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

    UnitCellInstanceData instances;
    if (!buildUnitCellInstances(structure, instances)) {
        m_edgeCount = 0;
        m_jointCount = 0;
        m_impl->edgeInstanceBuffer = nil;
        m_impl->jointInstanceBuffer = nil;
        return;
    }

    m_impl->edgeInstanceBuffer = [m_impl->device
        newBufferWithBytes:instances.edges.data()
                    length:instances.edges.size() * sizeof(BondInstance)
                   options:MTLResourceStorageModeShared];

    m_impl->jointInstanceBuffer = [m_impl->device
        newBufferWithBytes:instances.joints.data()
                    length:instances.joints.size() * sizeof(SphereInstance)
                   options:MTLResourceStorageModeShared];

    m_edgeCount = kUnitCellEdgeCount;
    m_jointCount = kUnitCellJointCount;
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

    const UnitCellStyle style = makeUnitCellStyle(settings);
    const simd_float4 unitCellColor = style.color;

    // Keep colors dynamic so UI RGB sliders update without geometry rebuild.
    BondInstance* edgeData = static_cast<BondInstance*>([m_impl->edgeInstanceBuffer contents]);
    for (int i = 0; i < m_edgeCount; ++i) {
        edgeData[i].startColor = unitCellColor;
        edgeData[i].endColor = unitCellColor;
    }

    SphereInstance* jointData = static_cast<SphereInstance*>([m_impl->jointInstanceBuffer contents]);
    for (int i = 0; i < m_jointCount; ++i) {
        jointData[i].color = unitCellColor;
    }

    SceneUniforms unitCellUniforms = uniforms;
    unitCellUniforms.atomScale = style.radius;
    unitCellUniforms.bondRadius = style.radius;

    // No stroke outlines on the unit-cell object — the corner joints share the
    // sphere impostor pipeline with atoms and would otherwise grow shells.
    unitCellUniforms.outlineWidthPx = 0.0f;

    // No early-Z placement either: the atom-based gate does not cover the
    // tiny joint spheres, and this pass uses the depth(any) pipeline.
    unitCellUniforms.sphereEarlyZ = 0;

    // Flat color: no shading terms for the unit-cell object.
    unitCellUniforms.ambient = 1.0f;
    unitCellUniforms.diffuse = 0.0f;
    unitCellUniforms.specular = 0.0f;
    unitCellUniforms.shininess = 1.0f;

    // Draw edge cylinders.
    {
        id<MTLRenderPipelineState> pipeline =
            (__bridge id<MTLRenderPipelineState>)m_shaderLibrary->solidCylinderPipeline();
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
