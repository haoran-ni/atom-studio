#import <Metal/Metal.h>
#include "MetalRayTracingRenderer.h"
#include "MetalTypes.h"
#include "../common/Camera.h"
#include "../../data/Structure.h"
#include <QDebug>
#include <QMatrix4x4>
#include <array>
#include <algorithm>
#include <cstring>
#include <functional>
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

/// Remap projection matrix from OpenGL depth [-1,1] to Metal depth [0,1].
static simd_float4x4 remapDepthToMetal(const QMatrix4x4& proj) {
    QMatrix4x4 bias;
    bias(2, 2) = 0.5f;
    bias(2, 3) = 0.5f;
    return qMatToSimd(bias * proj);
}

static void createUnitCylinderGeometry(int segments,
                                       std::vector<float>& vertices,
                                       std::vector<uint32_t>& indices) {
    vertices.clear();
    indices.clear();

    const float pi = 3.14159265358979323846f;
    for (int i = 0; i <= segments; ++i) {
        const float angle = (2.0f * pi * i) / segments;
        const float x = std::cos(angle);
        const float y = std::sin(angle);

        // Unit cylinder aligned to +Z from z=0 to z=1.
        vertices.push_back(x);
        vertices.push_back(y);
        vertices.push_back(0.0f);

        vertices.push_back(x);
        vertices.push_back(y);
        vertices.push_back(1.0f);
    }

    for (int i = 0; i < segments; ++i) {
        const int b0 = i * 2;
        const int t0 = i * 2 + 1;
        const int b1 = (i + 1) * 2;
        const int t1 = (i + 1) * 2 + 1;

        indices.push_back(b0);
        indices.push_back(b1);
        indices.push_back(t0);

        indices.push_back(t0);
        indices.push_back(b1);
        indices.push_back(t1);
    }
}

static void createUnitSphereGeometry(int latSegments,
                                     int lonSegments,
                                     std::vector<float>& vertices,
                                     std::vector<uint32_t>& indices) {
    vertices.clear();
    indices.clear();

    const float pi = 3.14159265358979323846f;

    for (int lat = 0; lat <= latSegments; ++lat) {
        const float v = static_cast<float>(lat) / latSegments;
        const float theta = v * pi;
        const float sinTheta = std::sin(theta);
        const float cosTheta = std::cos(theta);

        for (int lon = 0; lon <= lonSegments; ++lon) {
            const float u = static_cast<float>(lon) / lonSegments;
            const float phi = u * (2.0f * pi);
            const float sinPhi = std::sin(phi);
            const float cosPhi = std::cos(phi);

            vertices.push_back(sinTheta * cosPhi);
            vertices.push_back(cosTheta);
            vertices.push_back(sinTheta * sinPhi);
        }
    }

    const int stride = lonSegments + 1;
    for (int lat = 0; lat < latSegments; ++lat) {
        for (int lon = 0; lon < lonSegments; ++lon) {
            const uint32_t i0 = static_cast<uint32_t>(lat * stride + lon);
            const uint32_t i1 = static_cast<uint32_t>((lat + 1) * stride + lon);
            const uint32_t i2 = i0 + 1;
            const uint32_t i3 = i1 + 1;

            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i2);

            indices.push_back(i2);
            indices.push_back(i1);
            indices.push_back(i3);
        }
    }
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
    createUnitCylinderGeometry(48, cylinderVertices, cylinderIndices);
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
    createUnitSphereGeometry(10, 16, sphereVertices, sphereIndices);
    m_impl->unitCellSphereVertexBuffer = [m_impl->device
        newBufferWithBytes:sphereVertices.data()
                    length:sphereVertices.size() * sizeof(float)
                   options:MTLResourceStorageModeShared];
    m_impl->unitCellSphereIndexBuffer = [m_impl->device
        newBufferWithBytes:sphereIndices.data()
                    length:sphereIndices.size() * sizeof(uint32_t)
                   options:MTLResourceStorageModeShared];
    m_unitCellSphereIndexCount = static_cast<int>(sphereIndices.size());

    m_initialized = true;
    qInfo() << "MetalRayTracingRenderer: initialized successfully";
    return true;
}

void MetalRayTracingRenderer::cleanup() {
    m_shaderLibrary.cleanup();

    m_impl->quadVertexBuffer = nil;
    m_impl->accumTexture = nil;
    m_impl->msaaOutputTexture = nil;
    m_impl->outputTexture = nil;
    m_impl->atomPositionBuffer = nil;
    m_impl->atomColorBuffer = nil;
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

    // Store settings for isConverged() and computeStateHash()
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

    if (m_atomCount == 0) {
        if (m_settings.showUnitCell && m_unitCellEdgeCount > 0 && m_unitCellJointCount > 0) {
            renderUnitCellOverlay(camera);
        } else if (m_impl->outputTexture) {
            // Clear output to background
            id<MTLCommandBuffer> cmd = [m_impl->commandQueue commandBuffer];
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = m_impl->outputTexture;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            const auto& bg = m_settings.backgroundColor;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(
                bg.redF(), bg.greenF(), bg.blueF(), 1.0);
            id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:pass];
            [enc endEncoding];
            [cmd commit];
            [cmd waitUntilCompleted];
        }
        return;
    }

    // State change detection
    uint64_t currentHash = computeStateHash(camera);
    if (currentHash != m_lastStateHash) {
        m_lastStateHash = currentHash;
        resetAccumulation();
    }

    // Keep rendering the display/output passes even after convergence so
    // overlays (unit cell) and RT output refresh remain responsive.
    if (!isConverged()) {
        m_sampleCount++;
        renderRTPass(camera);
    }

    renderDisplayPass(camera);
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

    if (m_impl->accumTexture && m_initialized) {
        // Clear accumulation texture to black
        id<MTLCommandBuffer> cmd = [m_impl->commandQueue commandBuffer];
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = m_impl->accumTexture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);

        id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:pass];
        [enc endEncoding];
        [cmd commit];
        [cmd waitUntilCompleted];
    }
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
        m_impl->atomPositionBuffer = nil;
        m_impl->atomColorBuffer = nil;
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

    m_atomDataDirty = false;
}

void MetalRayTracingRenderer::uploadUnitCellData() {
    if (!m_structure || !m_structure->hasLattice()) {
        m_impl->unitCellEdgeInstanceBuffer = nil;
        m_impl->unitCellJointInstanceBuffer = nil;
        m_unitCellEdgeCount = 0;
        m_unitCellJointCount = 0;
        m_unitCellDataDirty = false;
        return;
    }

    const auto& lattice = m_structure->lattice();
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
        simd_make_float3(0.0f, 0.0f, 0.0f),                        // O
        simd_make_float3(ax, ay, az),                              // A
        simd_make_float3(bx, by, bz),                              // B
        simd_make_float3(cx, cy, cz),                              // C
        simd_make_float3(ax + bx, ay + by, az + bz),               // A+B
        simd_make_float3(ax + cx, ay + cy, az + cz),               // A+C
        simd_make_float3(bx + cx, by + cy, bz + cz),               // B+C
        simd_make_float3(ax + bx + cx, ay + by + cy, az + bz + cz) // A+B+C
    }};

    const std::array<uint32_t, 24> edgeIndices = {{
        0, 1,   0, 2,   0, 3,
        1, 4,   1, 5,
        2, 4,   2, 6,
        3, 5,   3, 6,
        4, 7,   5, 7,   6, 7
    }};

    const simd_float4 white = simd_make_float4(1.0f, 1.0f, 1.0f, 1.0f);

    std::array<BondInstance, 12> edgeInstances{};
    for (int i = 0; i < 12; ++i) {
        edgeInstances[i].start = corners[edgeIndices[i * 2 + 0]];
        edgeInstances[i].end = corners[edgeIndices[i * 2 + 1]];
        edgeInstances[i].color = white;
    }

    std::array<SphereInstance, 8> jointInstances{};
    for (int i = 0; i < 8; ++i) {
        jointInstances[i].positionAndRadius = simd_make_float4(corners[i], 1.0f);
        jointInstances[i].color = white;
    }

    m_impl->unitCellEdgeInstanceBuffer = [m_impl->device
        newBufferWithBytes:edgeInstances.data()
                    length:edgeInstances.size() * sizeof(BondInstance)
                   options:MTLResourceStorageModeShared];
    m_impl->unitCellJointInstanceBuffer = [m_impl->device
        newBufferWithBytes:jointInstances.data()
                    length:jointInstances.size() * sizeof(SphereInstance)
                   options:MTLResourceStorageModeShared];

    m_unitCellEdgeCount = 12;
    m_unitCellJointCount = 8;
    m_unitCellDataDirty = false;
}

void MetalRayTracingRenderer::renderRTPass(const Camera& camera) {
    // Build RT uniforms
    RTUniforms rt{};
    rt.invView = qMatToSimd(camera.viewMatrix().inverted());
    rt.invProjection = qMatToSimd(camera.projectionMatrix().inverted());

    QVector3D camPos = camera.position();
    rt.cameraPosition = simd_make_float3(camPos.x(), camPos.y(), camPos.z());
    rt.atomScale = m_settings.atomScale;

    QVector3D lightDir(m_settings.lightDirX, m_settings.lightDirY, m_settings.lightDirZ);
    lightDir.normalize();
    rt.lightDir = simd_make_float3(lightDir.x(), lightDir.y(), lightDir.z());
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

    // Render
    id<MTLCommandBuffer> cmdBuffer = [m_impl->commandQueue commandBuffer];

    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = m_impl->accumTexture;
    pass.colorAttachments[0].loadAction = MTLLoadActionLoad; // keep existing accumulation
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

    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    [encoder endEncoding];

    [cmdBuffer commit];
    [cmdBuffer waitUntilCompleted];
}

void MetalRayTracingRenderer::renderDisplayPass(const Camera& camera) {
    DisplayUniforms disp{};
    disp.sampleCount = static_cast<float>(m_sampleCount);

    id<MTLCommandBuffer> cmdBuffer = [m_impl->commandQueue commandBuffer];

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

    if (m_settings.showUnitCell &&
        m_unitCellEdgeCount > 0 && m_unitCellJointCount > 0 &&
        m_impl->unitCellCylinderVertexBuffer && m_impl->unitCellCylinderIndexBuffer &&
        m_impl->unitCellEdgeInstanceBuffer &&
        m_impl->unitCellSphereVertexBuffer && m_impl->unitCellSphereIndexBuffer &&
        m_impl->unitCellJointInstanceBuffer) {
        RTUnitCellUniforms unitCell{};
        unitCell.viewProjectionMatrix = remapDepthToMetal(camera.viewProjectionMatrix());
        QVector3D camPos = camera.position();
        unitCell.cameraPosition = simd_make_float3(camPos.x(), camPos.y(), camPos.z());
        unitCell.atomScale = m_settings.atomScale;
        unitCell.atomCount = m_atomCount;
        unitCell.occlusionBias = 0.001f;
        unitCell.unitCellRadius = std::max(m_settings.unitCellThickness, 0.001f);
        const QColor color = m_settings.unitCellColor;
        unitCell.unitCellColor = simd_make_float4(color.redF(), color.greenF(), color.blueF(), 1.0f);

        [encoder setDepthStencilState:depthState];
        [encoder setCullMode:MTLCullModeNone];
        [encoder setFragmentBuffer:m_impl->atomPositionBuffer offset:0 atIndex:1];

        // Edge cylinders
        {
            id<MTLRenderPipelineState> overlayPipeline =
                (__bridge id<MTLRenderPipelineState>)m_shaderLibrary.rtUnitCellCylinderPipeline();
            [encoder setRenderPipelineState:overlayPipeline];
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

        // Corner joints
        {
            id<MTLRenderPipelineState> overlayPipeline =
                (__bridge id<MTLRenderPipelineState>)m_shaderLibrary.rtUnitCellSpherePipeline();
            [encoder setRenderPipelineState:overlayPipeline];
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

    [encoder endEncoding];

    [cmdBuffer commit];
    [cmdBuffer waitUntilCompleted];
}

void MetalRayTracingRenderer::renderUnitCellOverlay(const Camera& camera) {
    if (!m_impl->outputTexture ||
        !m_impl->unitCellCylinderVertexBuffer || !m_impl->unitCellCylinderIndexBuffer ||
        !m_impl->unitCellEdgeInstanceBuffer ||
        !m_impl->unitCellSphereVertexBuffer || !m_impl->unitCellSphereIndexBuffer ||
        !m_impl->unitCellJointInstanceBuffer ||
        m_unitCellEdgeCount == 0 || m_unitCellJointCount == 0) {
        return;
    }

    RTUnitCellUniforms unitCell{};
    unitCell.viewProjectionMatrix = remapDepthToMetal(camera.viewProjectionMatrix());
    QVector3D camPos = camera.position();
    unitCell.cameraPosition = simd_make_float3(camPos.x(), camPos.y(), camPos.z());
    unitCell.atomScale = m_settings.atomScale;
    unitCell.atomCount = m_atomCount;
    unitCell.occlusionBias = 0.001f;
    unitCell.unitCellRadius = std::max(m_settings.unitCellThickness, 0.001f);
    const QColor color = m_settings.unitCellColor;
    unitCell.unitCellColor = simd_make_float4(color.redF(), color.greenF(), color.blueF(), 1.0f);

    id<MTLCommandBuffer> cmdBuffer = [m_impl->commandQueue commandBuffer];

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

    id<MTLDepthStencilState> depthState =
        (__bridge id<MTLDepthStencilState>)m_shaderLibrary.depthDisabledState();

    [encoder setDepthStencilState:depthState];
    [encoder setCullMode:MTLCullModeNone];

    [encoder setFragmentBuffer:m_impl->atomPositionBuffer offset:0 atIndex:1];

    // Edge cylinders
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

    // Corner joints
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
    [encoder endEncoding];

    [cmdBuffer commit];
    [cmdBuffer waitUntilCompleted];
}

uint64_t MetalRayTracingRenderer::computeStateHash(const Camera& camera) const {
    std::hash<float> hf;
    std::hash<int> hi;
    std::hash<bool> hb;

    uint64_t h = 0;
    auto combine = [&](uint64_t val) {
        h ^= val + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    };

    combine(hf(camera.azimuth()));
    combine(hf(camera.elevation()));
    combine(hf(camera.distance()));
    combine(hf(camera.target().x()));
    combine(hf(camera.target().y()));
    combine(hf(camera.target().z()));
    combine(hf(camera.fieldOfView()));
    combine(hf(camera.aspectRatio()));
    combine(hb(camera.isPerspective()));
    combine(hf(camera.orthoScale()));

    combine(hf(m_settings.atomScale));
    combine(hb(m_settings.enableShadows));
    combine(hb(m_settings.enableAmbientOcclusion));
    combine(hi(m_settings.aoSamples));
    combine(hf(m_settings.aoRadius));
    combine(hf(m_settings.ambientStrength));
    combine(hf(m_settings.diffuseStrength));
    combine(hf(m_settings.specularStrength));
    combine(hf(m_settings.shininess));
    combine(hf(m_settings.lightDirX));
    combine(hf(m_settings.lightDirY));
    combine(hf(m_settings.lightDirZ));
    combine(hi(m_settings.backgroundColor.red()));
    combine(hi(m_settings.backgroundColor.green()));
    combine(hi(m_settings.backgroundColor.blue()));

    return h;
}

} // namespace atom::render::metal
