#import <Metal/Metal.h>
#include "MetalRayTracingRenderer.h"
#include "MetalTypes.h"
#include "MetalUnitCellShared.h"
#include "../common/Camera.h"
#include "../common/BVH.h"
#include "../common/RenderStateHash.h"
#include "../../data/Structure.h"
#include <QDebug>
#include <QMatrix4x4>
#include <cassert>
#include <cstring>
#include <vector>

namespace atom::render::metal {

static constexpr NSUInteger kPreferredOverlaySampleCount = 4;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static simd_float4x4 qMatToSimd(const QMatrix4x4& m) {
    simd_float4x4 result;
    std::memcpy(&result, m.constData(), 16 * sizeof(float));
    return result;
}

// ---------------------------------------------------------------------------
// PIMPL
// ---------------------------------------------------------------------------

struct MetalRayTracingRenderer::Impl {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> commandQueue = nil;
    NSUInteger overlaySampleCount = 1;

    // Full-screen quad (6 × float2)
    id<MTLBuffer> quadVertexBuffer = nil;

    // Accumulation texture (RGBA32Float)
    id<MTLTexture> accumTexture = nil;

    // Display output texture (BGRA8Unorm) — what the viewport shows
    id<MTLTexture> msaaOutputTexture = nil;
    id<MTLTexture> outputTexture = nil;

    // Atom data buffers
    id<MTLBuffer> atomPositionBuffer = nil;  // float4(x,y,z,radius) per atom
    id<MTLBuffer> atomColorBuffer = nil;     // float4(r,g,b,a) per atom
    id<MTLBuffer> bvhNodeMinBuffer = nil;    // float4(min.xyz, maxRadius) per node
    id<MTLBuffer> bvhNodeMaxBuffer = nil;    // float4(max.xyz, pad) per node
    id<MTLBuffer> bvhNodeMetaBuffer = nil;   // uint4(left,right,first,count) per node
    id<MTLBuffer> bvhPrimIndexBuffer = nil;  // uint primitive indices

    // Unit-cell overlay geometry + instance buffers
    id<MTLBuffer> unitCellCylinderVertexBuffer = nil;   // unit cylinder mesh (packed_float3)
    id<MTLBuffer> unitCellCylinderIndexBuffer = nil;
    id<MTLBuffer> unitCellEdgeInstanceBuffer = nil;     // 12 × BondInstance

    id<MTLBuffer> unitCellSphereVertexBuffer = nil;     // unit sphere mesh (packed_float3)
    id<MTLBuffer> unitCellSphereIndexBuffer = nil;
    id<MTLBuffer> unitCellJointInstanceBuffer = nil;    // 8 × SphereInstance
};

MetalRayTracingRenderer::MetalRayTracingRenderer()
    : m_impl(std::make_unique<Impl>())
{
}

MetalRayTracingRenderer::~MetalRayTracingRenderer() {
    cleanup();
}

void MetalRayTracingRenderer::setDevice(void* device) {
    m_impl->device = (__bridge id<MTLDevice>)device;
}

bool MetalRayTracingRenderer::initialize() {
    if (m_initialized) return true;

    if (!m_impl->device) {
        qCritical() << "MetalRayTracingRenderer: device not set";
        return false;
    }

    m_impl->commandQueue = [m_impl->device newCommandQueue];

    m_impl->overlaySampleCount =
        [m_impl->device supportsTextureSampleCount:kPreferredOverlaySampleCount]
            ? kPreferredOverlaySampleCount
            : 1;
    if (m_impl->overlaySampleCount == 1) {
        qWarning() << "MetalRayTracingRenderer: 4x MSAA not supported for display/overlay; using single-sample";
    }

    // Compile shaders
    if (!m_shaderLibrary.initialize((__bridge void*)m_impl->device,
                                    static_cast<int>(m_impl->overlaySampleCount))) {
        return false;
    }

    // Full-screen quad: 6 × packed_float2
    const float quadVerts[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f,
    };
    m_impl->quadVertexBuffer = [m_impl->device newBufferWithBytes:quadVerts
                                                           length:sizeof(quadVerts)
                                                          options:MTLResourceStorageModeShared];

    // Unit-cell overlay meshes (constant topology, instance-transformed).
    std::vector<float> cylinderVertices;
    std::vector<uint32_t> cylinderIndices;
    buildUnitCylinderMesh(48, cylinderVertices, cylinderIndices);
    m_impl->unitCellCylinderVertexBuffer = [m_impl->device
        newBufferWithBytes:cylinderVertices.data()
                    length:cylinderVertices.size() * sizeof(float)
                   options:MTLResourceStorageModeShared];
    m_impl->unitCellCylinderIndexBuffer = [m_impl->device
        newBufferWithBytes:cylinderIndices.data()
                    length:cylinderIndices.size() * sizeof(uint32_t)
                   options:MTLResourceStorageModeShared];
    m_unitCellCylinderIndexCount = static_cast<int>(cylinderIndices.size());

    std::vector<float> sphereVertices;
    std::vector<uint32_t> sphereIndices;
    buildUnitSphereMesh(10, 16, sphereVertices, sphereIndices);
    m_impl->unitCellSphereVertexBuffer = [m_impl->device
        newBufferWithBytes:sphereVertices.data()
                    length:sphereVertices.size() * sizeof(float)
                   options:MTLResourceStorageModeShared];
    m_impl->unitCellSphereIndexBuffer = [m_impl->device
        newBufferWithBytes:sphereIndices.data()
                    length:sphereIndices.size() * sizeof(uint32_t)
                   options:MTLResourceStorageModeShared];
    m_unitCellSphereIndexCount = static_cast<int>(sphereIndices.size());

    if (!m_gizmoRenderer.initialize((__bridge void*)m_impl->device, &m_shaderLibrary)) return false;

    m_initialized = true;
    qInfo() << "MetalRayTracingRenderer: initialized successfully";
    return true;
}

void MetalRayTracingRenderer::cleanup() {
    m_gizmoRenderer.cleanup();
    m_shaderLibrary.cleanup();

    m_impl->quadVertexBuffer = nil;
    m_impl->accumTexture = nil;
    m_impl->msaaOutputTexture = nil;
    m_impl->outputTexture = nil;
    m_impl->atomPositionBuffer = nil;
    m_impl->atomColorBuffer = nil;
    m_impl->bvhNodeMinBuffer = nil;
    m_impl->bvhNodeMaxBuffer = nil;
    m_impl->bvhNodeMetaBuffer = nil;
    m_impl->bvhPrimIndexBuffer = nil;
    m_impl->unitCellCylinderVertexBuffer = nil;
    m_impl->unitCellCylinderIndexBuffer = nil;
    m_impl->unitCellEdgeInstanceBuffer = nil;
    m_impl->unitCellSphereVertexBuffer = nil;
    m_impl->unitCellSphereIndexBuffer = nil;
    m_impl->unitCellJointInstanceBuffer = nil;
    m_impl->commandQueue = nil;
    m_impl->overlaySampleCount = 1;

    m_structure = nullptr;
    m_atomCount = 0;
    m_bvhNodeCount = 0;
    m_unitCellEdgeCount = 0;
    m_unitCellJointCount = 0;
    m_unitCellCylinderIndexCount = 0;
    m_unitCellSphereIndexCount = 0;
    m_initialized = false;
}

void MetalRayTracingRenderer::resize(int width, int height) {
    if (width == m_width && height == m_height) return;
    if (width <= 0 || height <= 0) return;

    m_width = width;
    m_height = height;
    createRenderTargets();
    resetAccumulation();
}

void MetalRayTracingRenderer::setStructure(const data::Structure* structure) {
    m_structure = structure;
    m_atomDataDirty = true;
    m_unitCellDataDirty = true;
    resetAccumulation();
}

void MetalRayTracingRenderer::render(const Camera& camera, const RenderSettings& settings) {
    if (!m_initialized || m_width == 0 || m_height == 0) return;

    // Store settings for isConverged() and state hashing.
    m_settings = settings;

    if (m_atomDataDirty) {
        uploadAtomData();
    }
    if (m_unitCellDataDirty) {
        uploadUnitCellData();
    }

    // Ensure render targets exist
    if (!m_impl->accumTexture || !m_impl->outputTexture ||
        (m_impl->overlaySampleCount > 1 && !m_impl->msaaOutputTexture)) {
        createRenderTargets();
        resetAccumulation();
    }

    // Single command buffer for the entire frame — sub-passes encode into it
    // as separate render command encoders, committed once at the end.
    id<MTLCommandBuffer> cmdBuffer = [m_impl->commandQueue commandBuffer];

    if (m_atomCount == 0) {
        if (m_settings.showUnitCell && m_unitCellEdgeCount > 0 && m_unitCellJointCount > 0) {
            renderUnitCellOverlay(camera, (__bridge void*)cmdBuffer);
        } else if (m_impl->outputTexture) {
            // Clear output to background
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = m_impl->outputTexture;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            const auto& bg = m_settings.backgroundColor;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(
                bg.redF(), bg.greenF(), bg.blueF(), 1.0);
            id<MTLRenderCommandEncoder> enc = [cmdBuffer renderCommandEncoderWithDescriptor:pass];
            [enc endEncoding];
        }
        [cmdBuffer commit];
        [cmdBuffer waitUntilCompleted];
        return;
    }

    // State change detection
    uint64_t currentHash = computeRenderStateHash(camera, m_settings);
    if (currentHash != m_lastStateHash) {
        m_lastStateHash = currentHash;
        resetAccumulation();
    }

    // Keep rendering the display/output passes even after convergence so
    // overlays (unit cell) and RT output refresh remain responsive.
    if (!isConverged()) {
        m_sampleCount++;
        renderRTPass(camera, (__bridge void*)cmdBuffer);
    }

    renderDisplayPass(camera, (__bridge void*)cmdBuffer);

    [cmdBuffer commit];
    [cmdBuffer waitUntilCompleted];
}

void MetalRayTracingRenderer::invalidateAtomData() {
    m_atomDataDirty = true;
    resetAccumulation();
}

void MetalRayTracingRenderer::invalidateBondData() {
    resetAccumulation();
}

void MetalRayTracingRenderer::resetAccumulation() {
    m_sampleCount = 0;
    m_accumNeedsClear = true;
}

void* MetalRayTracingRenderer::outputTexture() const {
    return (__bridge void*)m_impl->outputTexture;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void MetalRayTracingRenderer::createRenderTargets() {
    // Accumulation: RGBA32Float
    MTLTextureDescriptor* accumDesc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                    width:m_width
                                   height:m_height
                                mipmapped:NO];
    accumDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    accumDesc.storageMode = MTLStorageModePrivate;
    m_impl->accumTexture = [m_impl->device newTextureWithDescriptor:accumDesc];

    // Display output: BGRA8Unorm
    MTLTextureDescriptor* outputDesc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                    width:m_width
                                   height:m_height
                                mipmapped:NO];
    outputDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    outputDesc.storageMode = MTLStorageModePrivate;
    m_impl->outputTexture = [m_impl->device newTextureWithDescriptor:outputDesc];

    if (m_impl->overlaySampleCount > 1) {
        MTLTextureDescriptor* msaaOutputDesc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                        width:m_width
                                       height:m_height
                                    mipmapped:NO];
        msaaOutputDesc.textureType = MTLTextureType2DMultisample;
        msaaOutputDesc.sampleCount = m_impl->overlaySampleCount;
        msaaOutputDesc.usage = MTLTextureUsageRenderTarget;
        msaaOutputDesc.storageMode = MTLStorageModePrivate;
        m_impl->msaaOutputTexture = [m_impl->device newTextureWithDescriptor:msaaOutputDesc];
    } else {
        m_impl->msaaOutputTexture = nil;
    }
}

void MetalRayTracingRenderer::uploadAtomData() {
    if (!m_structure || m_structure->atomCount() == 0) {
        m_atomCount = 0;
        m_bvhNodeCount = 0;
        m_impl->atomPositionBuffer = nil;
        m_impl->atomColorBuffer = nil;
        m_impl->bvhNodeMinBuffer = nil;
        m_impl->bvhNodeMaxBuffer = nil;
        m_impl->bvhNodeMetaBuffer = nil;
        m_impl->bvhPrimIndexBuffer = nil;
        m_atomDataDirty = false;
        return;
    }

    m_atomCount = static_cast<int>(m_structure->atomCount());

    auto posData = m_structure->packPositionsAndRadii();
    m_impl->atomPositionBuffer = [m_impl->device
        newBufferWithBytes:posData.data()
                    length:posData.size() * sizeof(float)
                   options:MTLResourceStorageModeShared];

    auto colorData = m_structure->packColors();
    m_impl->atomColorBuffer = [m_impl->device
        newBufferWithBytes:colorData.data()
                    length:colorData.size() * sizeof(float)
                   options:MTLResourceStorageModeShared];

    BVHData bvh = buildSphereBVH(m_structure->positionsX(),
                                 m_structure->positionsY(),
                                 m_structure->positionsZ(),
                                 m_structure->radii(),
                                 m_structure->atomCount());
    assert(!bvh.nodes.empty() && "Expected non-empty BVH for non-empty structure");
    assert(!bvh.primitiveIndices.empty() && "Expected non-empty BVH primitive index list");
    m_bvhNodeCount = static_cast<int>(bvh.nodes.size());

    std::vector<simd_float4> nodeMins(static_cast<size_t>(m_bvhNodeCount));
    std::vector<simd_float4> nodeMaxs(static_cast<size_t>(m_bvhNodeCount));
    std::vector<simd_uint4> nodeMeta(static_cast<size_t>(m_bvhNodeCount));
    for (int i = 0; i < m_bvhNodeCount; ++i) {
        const BVHNodeGPU& node = bvh.nodes[static_cast<size_t>(i)];
        nodeMins[static_cast<size_t>(i)] = simd_make_float4(
            node.minAndMaxRadius[0],
            node.minAndMaxRadius[1],
            node.minAndMaxRadius[2],
            node.minAndMaxRadius[3]);
        nodeMaxs[static_cast<size_t>(i)] = simd_make_float4(
            node.maxAndPad[0],
            node.maxAndPad[1],
            node.maxAndPad[2],
            node.maxAndPad[3]);
        nodeMeta[static_cast<size_t>(i)] = simd_make_uint4(
            node.meta[0], node.meta[1], node.meta[2], node.meta[3]);
    }

    m_impl->bvhNodeMinBuffer = [m_impl->device
        newBufferWithBytes:nodeMins.data()
                    length:nodeMins.size() * sizeof(simd_float4)
                   options:MTLResourceStorageModeShared];
    m_impl->bvhNodeMaxBuffer = [m_impl->device
        newBufferWithBytes:nodeMaxs.data()
                    length:nodeMaxs.size() * sizeof(simd_float4)
                   options:MTLResourceStorageModeShared];
    m_impl->bvhNodeMetaBuffer = [m_impl->device
        newBufferWithBytes:nodeMeta.data()
                    length:nodeMeta.size() * sizeof(simd_uint4)
                   options:MTLResourceStorageModeShared];
    m_impl->bvhPrimIndexBuffer = [m_impl->device
        newBufferWithBytes:bvh.primitiveIndices.data()
                    length:bvh.primitiveIndices.size() * sizeof(uint32_t)
                   options:MTLResourceStorageModeShared];

    m_atomDataDirty = false;
}

void MetalRayTracingRenderer::uploadUnitCellData() {
    UnitCellInstanceData instances;
    if (!buildUnitCellInstances(m_structure, instances)) {
        m_impl->unitCellEdgeInstanceBuffer = nil;
        m_impl->unitCellJointInstanceBuffer = nil;
        m_unitCellEdgeCount = 0;
        m_unitCellJointCount = 0;
        m_unitCellDataDirty = false;
        return;
    }

    m_impl->unitCellEdgeInstanceBuffer = [m_impl->device
        newBufferWithBytes:instances.edges.data()
                    length:instances.edges.size() * sizeof(BondInstance)
                   options:MTLResourceStorageModeShared];
    m_impl->unitCellJointInstanceBuffer = [m_impl->device
        newBufferWithBytes:instances.joints.data()
                    length:instances.joints.size() * sizeof(SphereInstance)
                   options:MTLResourceStorageModeShared];

    m_unitCellEdgeCount = kUnitCellEdgeCount;
    m_unitCellJointCount = kUnitCellJointCount;
    m_unitCellDataDirty = false;
}

void MetalRayTracingRenderer::renderRTPass(const Camera& camera, void* cmdBuf) {
    // Build RT uniforms
    RTUniforms rt{};
    rt.invView = qMatToSimd(camera.viewMatrix().inverted());
    rt.invProjection = qMatToSimd(camera.projectionMatrix().inverted());

    QVector3D camPos = camera.position();
    rt.cameraPosition = simd_make_float3(camPos.x(), camPos.y(), camPos.z());
    rt.atomScale = m_settings.atomScale;

    // Light direction sliders are in view space (camera-relative); transform to world space for RT
    QVector3D viewLightDir(m_settings.lightDirX, m_settings.lightDirY, m_settings.lightDirZ);
    viewLightDir.normalize();
    QVector3D worldLightDir = camera.rightVector()    * viewLightDir.x()
                            + camera.upVector()       * viewLightDir.y()
                            + (-camera.forwardVector()) * viewLightDir.z();
    worldLightDir.normalize();
    rt.lightDir = simd_make_float3(worldLightDir.x(), worldLightDir.y(), worldLightDir.z());
    rt.ambient = m_settings.ambientStrength;

    QColor bg = m_settings.backgroundColor;
    rt.backgroundColor = simd_make_float3(bg.redF(), bg.greenF(), bg.blueF());
    rt.diffuse = m_settings.diffuseStrength;
    rt.specular = m_settings.specularStrength;
    rt.shininess = m_settings.shininess;
    rt.atomCount = m_atomCount;
    rt.width = m_width;
    rt.height = m_height;
    rt.frameCount = static_cast<uint32_t>(m_sampleCount);
    rt.enableShadows = m_settings.enableShadows ? 1 : 0;
    rt.enableAO = m_settings.enableAmbientOcclusion ? 1 : 0;
    rt.aoSamples = m_settings.aoSamples;
    rt.aoRadius = m_settings.aoRadius;
    rt.maxSamples = m_settings.maxRTSamples;
    rt.bvhNodeCount = m_bvhNodeCount;

    id<MTLCommandBuffer> cmdBuffer = (__bridge id<MTLCommandBuffer>)cmdBuf;

    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = m_impl->accumTexture;
    // Deferred accumulation clear: use Clear on first sample after reset,
    // then Load for subsequent samples to preserve existing accumulation.
    if (m_accumNeedsClear) {
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
        m_accumNeedsClear = false;
    } else {
        pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
    }
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> encoder = [cmdBuffer renderCommandEncoderWithDescriptor:pass];

    [encoder setViewport:(MTLViewport){0, 0,
        static_cast<double>(m_width), static_cast<double>(m_height),
        0.0, 1.0}];

    id<MTLRenderPipelineState> pipeline = (__bridge id<MTLRenderPipelineState>)m_shaderLibrary.rtPipeline();
    id<MTLDepthStencilState> depthState = (__bridge id<MTLDepthStencilState>)m_shaderLibrary.depthDisabledState();

    [encoder setRenderPipelineState:pipeline];
    [encoder setDepthStencilState:depthState];

    // buffer(0): quad vertices (vertex stage)
    [encoder setVertexBuffer:m_impl->quadVertexBuffer offset:0 atIndex:0];

    // buffer(0): RT uniforms (fragment stage)
    [encoder setFragmentBytes:&rt length:sizeof(RTUniforms) atIndex:0];

    // buffer(1),(2): atom data (fragment stage)
    [encoder setFragmentBuffer:m_impl->atomPositionBuffer offset:0 atIndex:1];
    [encoder setFragmentBuffer:m_impl->atomColorBuffer offset:0 atIndex:2];
    [encoder setFragmentBuffer:m_impl->bvhNodeMinBuffer offset:0 atIndex:3];
    [encoder setFragmentBuffer:m_impl->bvhNodeMaxBuffer offset:0 atIndex:4];
    [encoder setFragmentBuffer:m_impl->bvhNodeMetaBuffer offset:0 atIndex:5];
    [encoder setFragmentBuffer:m_impl->bvhPrimIndexBuffer offset:0 atIndex:6];

    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    [encoder endEncoding];
}

void MetalRayTracingRenderer::renderDisplayPass(const Camera& camera, void* cmdBuf) {
    DisplayUniforms disp{};
    disp.sampleCount = static_cast<float>(m_sampleCount);

    id<MTLCommandBuffer> cmdBuffer = (__bridge id<MTLCommandBuffer>)cmdBuf;

    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    if (m_impl->msaaOutputTexture) {
        pass.colorAttachments[0].texture = m_impl->msaaOutputTexture;
        pass.colorAttachments[0].resolveTexture = m_impl->outputTexture;
        pass.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
    } else {
        pass.colorAttachments[0].texture = m_impl->outputTexture;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    }

    id<MTLRenderCommandEncoder> encoder = [cmdBuffer renderCommandEncoderWithDescriptor:pass];

    [encoder setViewport:(MTLViewport){0, 0,
        static_cast<double>(m_width), static_cast<double>(m_height),
        0.0, 1.0}];

    id<MTLRenderPipelineState> pipeline = (__bridge id<MTLRenderPipelineState>)m_shaderLibrary.displayPipeline();
    id<MTLDepthStencilState> depthState = (__bridge id<MTLDepthStencilState>)m_shaderLibrary.depthDisabledState();

    [encoder setRenderPipelineState:pipeline];
    [encoder setDepthStencilState:depthState];

    [encoder setVertexBuffer:m_impl->quadVertexBuffer offset:0 atIndex:0];
    [encoder setFragmentBytes:&disp length:sizeof(DisplayUniforms) atIndex:0];
    [encoder setFragmentTexture:m_impl->accumTexture atIndex:0];

    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];

    if (m_settings.showUnitCell && hasUnitCellOverlayData()) {
        const RTUnitCellUniforms unitCell = makeRTUnitCellUniforms(
            camera, m_settings, m_atomCount, m_bvhNodeCount);
        encodeUnitCellOverlayDraws((__bridge void*)encoder, unitCell);
    }

    if (m_settings.showRotationCenter) {
        // Gizmo now uses the bond/cylinder shader path, which needs view + projection.
        QMatrix4x4 bias;
        bias(2, 2) = 0.5f;
        bias(2, 3) = 0.5f;
        SceneUniforms gizmoUniforms{};
        gizmoUniforms.viewMatrix = qMatToSimd(camera.viewMatrix());
        gizmoUniforms.projectionMatrix = qMatToSimd(bias * camera.projectionMatrix());
        gizmoUniforms.viewProjectionMatrix = qMatToSimd(bias * camera.viewProjectionMatrix());
        float len = camera.distance() * 0.03f;
        m_gizmoRenderer.render((__bridge void*)encoder, gizmoUniforms,
                               m_settings.rotationCenterX, m_settings.rotationCenterY,
                               m_settings.rotationCenterZ, len, /*depthTest=*/false);
    }

    [encoder endEncoding];
}

void MetalRayTracingRenderer::renderUnitCellOverlay(const Camera& camera, void* cmdBuf) {
    if (!m_impl->outputTexture || !hasUnitCellOverlayData()) {
        return;
    }

    const RTUnitCellUniforms unitCell = makeRTUnitCellUniforms(
        camera, m_settings, m_atomCount, m_bvhNodeCount);

    id<MTLCommandBuffer> cmdBuffer = (__bridge id<MTLCommandBuffer>)cmdBuf;

    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    if (m_impl->msaaOutputTexture) {
        pass.colorAttachments[0].texture = m_impl->msaaOutputTexture;
        pass.colorAttachments[0].resolveTexture = m_impl->outputTexture;
        pass.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
    } else {
        pass.colorAttachments[0].texture = m_impl->outputTexture;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    }
    if (m_atomCount == 0) {
        const auto& bg = m_settings.backgroundColor;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(
            bg.redF(), bg.greenF(), bg.blueF(), 1.0);
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    } else {
        pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
    }

    id<MTLRenderCommandEncoder> encoder = [cmdBuffer renderCommandEncoderWithDescriptor:pass];

    [encoder setViewport:(MTLViewport){0, 0,
        static_cast<double>(m_width), static_cast<double>(m_height),
        0.0, 1.0}];

    encodeUnitCellOverlayDraws((__bridge void*)encoder, unitCell);
    [encoder endEncoding];
}

bool MetalRayTracingRenderer::hasUnitCellOverlayData() const {
    return m_unitCellEdgeCount > 0 && m_unitCellJointCount > 0 &&
           m_impl->unitCellCylinderVertexBuffer && m_impl->unitCellCylinderIndexBuffer &&
           m_impl->unitCellEdgeInstanceBuffer &&
           m_impl->unitCellSphereVertexBuffer && m_impl->unitCellSphereIndexBuffer &&
           m_impl->unitCellJointInstanceBuffer;
}

void MetalRayTracingRenderer::encodeUnitCellOverlayDraws(
    void* encoderPtr, const RTUnitCellUniforms& unitCell) {
    id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)encoderPtr;
    id<MTLDepthStencilState> depthState =
        (__bridge id<MTLDepthStencilState>)m_shaderLibrary.depthDisabledState();

    [encoder setDepthStencilState:depthState];
    [encoder setCullMode:MTLCullModeNone];
    [encoder setFragmentBuffer:m_impl->atomPositionBuffer offset:0 atIndex:1];
    [encoder setFragmentBuffer:m_impl->bvhNodeMinBuffer offset:0 atIndex:3];
    [encoder setFragmentBuffer:m_impl->bvhNodeMaxBuffer offset:0 atIndex:4];
    [encoder setFragmentBuffer:m_impl->bvhNodeMetaBuffer offset:0 atIndex:5];
    [encoder setFragmentBuffer:m_impl->bvhPrimIndexBuffer offset:0 atIndex:6];

    // Edge cylinders.
    {
        id<MTLRenderPipelineState> pipeline =
            (__bridge id<MTLRenderPipelineState>)m_shaderLibrary.rtUnitCellCylinderPipeline();
        [encoder setRenderPipelineState:pipeline];
        [encoder setVertexBytes:&unitCell length:sizeof(RTUnitCellUniforms) atIndex:0];
        [encoder setVertexBuffer:m_impl->unitCellCylinderVertexBuffer offset:0 atIndex:1];
        [encoder setVertexBuffer:m_impl->unitCellEdgeInstanceBuffer offset:0 atIndex:2];
        [encoder setFragmentBytes:&unitCell length:sizeof(RTUnitCellUniforms) atIndex:0];

        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:m_unitCellCylinderIndexCount
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:m_impl->unitCellCylinderIndexBuffer
                     indexBufferOffset:0
                         instanceCount:m_unitCellEdgeCount];
    }

    // Corner joints.
    {
        id<MTLRenderPipelineState> pipeline =
            (__bridge id<MTLRenderPipelineState>)m_shaderLibrary.rtUnitCellSpherePipeline();
        [encoder setRenderPipelineState:pipeline];
        [encoder setVertexBytes:&unitCell length:sizeof(RTUnitCellUniforms) atIndex:0];
        [encoder setVertexBuffer:m_impl->unitCellSphereVertexBuffer offset:0 atIndex:1];
        [encoder setVertexBuffer:m_impl->unitCellJointInstanceBuffer offset:0 atIndex:2];
        [encoder setFragmentBytes:&unitCell length:sizeof(RTUnitCellUniforms) atIndex:0];

        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:m_unitCellSphereIndexCount
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:m_impl->unitCellSphereIndexBuffer
                     indexBufferOffset:0
                         instanceCount:m_unitCellJointCount];
    }
}

} // namespace atom::render::metal
