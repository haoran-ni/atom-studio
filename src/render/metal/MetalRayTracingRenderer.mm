#import <Metal/Metal.h>
#include "MetalRayTracingRenderer.h"
#include "MetalTypes.h"
#include "MetalUnitCellShared.h"
#include "../common/Camera.h"
#include "../common/BVH.h"
#include "../common/RenderStateHash.h"
#include "../../data/Structure.h"
#include "../../data/BondList.h"
#include <QDebug>
#include <QMatrix4x4>
#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#include <memory>
#include <vector>

namespace atom::render::metal {

static constexpr NSUInteger kPreferredOverlaySampleCount = 4;
static constexpr int kOutputSlotCount = 3;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static simd_float4x4 qMatToSimd(const QMatrix4x4& m) {
    simd_float4x4 result;
    std::memcpy(&result, m.constData(), 16 * sizeof(float));
    return result;
}

enum class OutputSlotState : uint8_t {
    Free = 0,
    InFlight = 1,
    Ready = 2
};

struct AsyncFrameState {
    std::atomic<uint64_t> generation{1};
    std::atomic<int> inFlightSlot{-1};
    std::atomic<int> latestReadySlot{-1};
    std::atomic<int> latestReadySampleCount{0};
    std::array<std::atomic<uint8_t>, kOutputSlotCount> slotStates{};

    AsyncFrameState() {
        for (auto& state : slotStates) {
            state.store(static_cast<uint8_t>(OutputSlotState::Free));
        }
    }
};

// ---------------------------------------------------------------------------
// PIMPL
// ---------------------------------------------------------------------------

struct MetalRayTracingRenderer::Impl {
    struct OutputSlot {
        id<MTLTexture> msaaOutputTexture = nil;
        id<MTLTexture> outputTexture = nil;
        id<MTLTexture> msaaOverlayDepthTexture = nil;
        id<MTLTexture> overlayDepthTexture = nil;
    };

    id<MTLDevice> device = nil;
    id<MTLCommandQueue> commandQueue = nil;
    NSUInteger overlaySampleCount = 1;
    std::array<OutputSlot, kOutputSlotCount> outputSlots{};
    std::shared_ptr<AsyncFrameState> asyncState = std::make_shared<AsyncFrameState>();

    // Full-screen quad (6 × float2)
    id<MTLBuffer> quadVertexBuffer = nil;

    // Accumulation texture (RGBA32Float)
    id<MTLTexture> accumTexture = nil;

    // Atom data buffers
    id<MTLBuffer> atomPositionBuffer = nil;  // float4(x,y,z,radius) per atom
    id<MTLBuffer> atomColorBuffer = nil;     // float4(r,g,b,a) per atom
    id<MTLBuffer> bvhNodeMinBuffer = nil;    // float4(min.xyz, maxRadius) per node
    id<MTLBuffer> bvhNodeMaxBuffer = nil;    // float4(max.xyz, pad) per node
    id<MTLBuffer> bvhNodeMetaBuffer = nil;   // uint4(left,right,first,count) per node
    id<MTLBuffer> bvhPrimIndexBuffer = nil;  // uint primitive indices

    // Bond data buffers
    id<MTLBuffer> bondStartBuffer = nil;     // float4(x,y,z,0) per bond
    id<MTLBuffer> bondEndBuffer = nil;       // float4(x,y,z,0) per bond
    id<MTLBuffer> bondColorBuffer = nil;     // float4(r,g,b,a) per bond

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
    if (!m_viewportAxesRenderer.initialize((__bridge void*)m_impl->device, &m_shaderLibrary)) return false;

    m_initialized = true;
    qInfo() << "MetalRayTracingRenderer: initialized successfully";
    return true;
}

void MetalRayTracingRenderer::cleanup() {
    m_gizmoRenderer.cleanup();
    m_viewportAxesRenderer.cleanup();
    m_shaderLibrary.cleanup();

    m_impl->asyncState->generation.fetch_add(1, std::memory_order_relaxed);
    m_impl->asyncState->inFlightSlot.store(-1, std::memory_order_relaxed);
    m_impl->asyncState->latestReadySlot.store(-1, std::memory_order_relaxed);
    m_impl->asyncState->latestReadySampleCount.store(0, std::memory_order_relaxed);
    for (auto& state : m_impl->asyncState->slotStates) {
        state.store(static_cast<uint8_t>(OutputSlotState::Free), std::memory_order_relaxed);
    }

    m_impl->quadVertexBuffer = nil;
    m_impl->accumTexture = nil;
    for (auto& slot : m_impl->outputSlots) {
        slot.msaaOutputTexture = nil;
        slot.outputTexture = nil;
        slot.msaaOverlayDepthTexture = nil;
        slot.overlayDepthTexture = nil;
    }
    m_impl->atomPositionBuffer = nil;
    m_impl->atomColorBuffer = nil;
    m_impl->bvhNodeMinBuffer = nil;
    m_impl->bvhNodeMaxBuffer = nil;
    m_impl->bvhNodeMetaBuffer = nil;
    m_impl->bvhPrimIndexBuffer = nil;
    m_impl->bondStartBuffer = nil;
    m_impl->bondEndBuffer = nil;
    m_impl->bondColorBuffer = nil;
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
    m_bondCount = 0;
    m_bvhNodeCount = 0;
    m_unitCellEdgeCount = 0;
    m_unitCellJointCount = 0;
    m_unitCellCylinderIndexCount = 0;
    m_unitCellSphereIndexCount = 0;
    m_lastStateHash = 0;
    m_outputGeneration = m_impl->asyncState->generation.load(std::memory_order_relaxed);
    m_lastPresentedSlot = -1;
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
    m_bondDataDirty = true;
    m_unitCellDataDirty = true;
    resetAccumulation();
}

void MetalRayTracingRenderer::render(const Camera& camera, const RenderSettings& settings) {
    if (!m_initialized || m_width == 0 || m_height == 0) return;

    // Store settings for isConverged() and state hashing.
    m_settings = settings;

    // Submit at most one GPU frame at a time. Skip all CPU-side work
    // (scene packing, BVH build, buffer allocation) while a frame is
    // in-flight — dirty flags stay set and are processed once it drains.
    if (m_impl->asyncState->inFlightSlot.load(std::memory_order_acquire) >= 0) {
        return;
    }

    if (m_atomDataDirty || m_bondDataDirty) {
        uploadSceneData();
    }
    if (m_unitCellDataDirty) {
        uploadUnitCellData();
    }

    // Ensure render targets exist
    if (!m_impl->accumTexture || !m_impl->outputSlots[0].outputTexture ||
        !m_impl->outputSlots[0].overlayDepthTexture ||
        (m_impl->overlaySampleCount > 1 &&
         (!m_impl->outputSlots[0].msaaOutputTexture ||
          !m_impl->outputSlots[0].msaaOverlayDepthTexture))) {
        createRenderTargets();
        resetAccumulation();
    }

    const int outputSlotIndex = acquireOutputSlot();
    if (outputSlotIndex < 0) {
        return;
    }

    // Single command buffer for the entire frame — sub-passes encode into it
    // as separate render command encoders, committed once at the end.
    id<MTLCommandBuffer> cmdBuffer = [m_impl->commandQueue commandBuffer];

    if (m_atomCount == 0) {
        const bool hasUnitCellOverlay =
            m_settings.showUnitCell && m_unitCellEdgeCount > 0 && m_unitCellJointCount > 0;
        const bool hasAnyOverlay =
            hasUnitCellOverlay || m_settings.showViewportAxes || m_settings.showRotationCenter;

        if (hasAnyOverlay) {
            renderUnitCellOverlay(camera, (__bridge void*)cmdBuffer, outputSlotIndex);
        } else if (m_impl->outputSlots[outputSlotIndex].outputTexture) {
            // Clear output to background
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = m_impl->outputSlots[outputSlotIndex].outputTexture;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            const auto& bg = m_settings.backgroundColor;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(
                bg.redF() * bg.alphaF(), bg.greenF() * bg.alphaF(), bg.blueF() * bg.alphaF(), bg.alphaF());
            id<MTLRenderCommandEncoder> enc = [cmdBuffer renderCommandEncoderWithDescriptor:pass];
            [enc endEncoding];
        }
        trackSubmittedFrame((__bridge void*)cmdBuffer, outputSlotIndex, 0, m_outputGeneration);
        [cmdBuffer commit];
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

    renderDisplayPass(camera, (__bridge void*)cmdBuffer, outputSlotIndex);

    trackSubmittedFrame((__bridge void*)cmdBuffer, outputSlotIndex, m_sampleCount, m_outputGeneration);
    [cmdBuffer commit];
}

void MetalRayTracingRenderer::invalidateAtomData() {
    m_atomDataDirty = true;
    resetAccumulation();
}

void MetalRayTracingRenderer::invalidateBondData() {
    m_bondDataDirty = true;
    resetAccumulation();
}

bool MetalRayTracingRenderer::needsMoreFrames() const {
    if (m_impl->asyncState->inFlightSlot.load(std::memory_order_acquire) >= 0) {
        return true;
    }
    return m_atomCount > 0 && !isConverged();
}

void MetalRayTracingRenderer::resetAccumulation() {
    m_sampleCount = 0;
    m_accumNeedsClear = true;
    m_outputGeneration = m_impl->asyncState->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
}

void* MetalRayTracingRenderer::outputTexture() {
    const int readySlot = m_impl->asyncState->latestReadySlot.load(std::memory_order_acquire);
    if (readySlot < 0 || readySlot >= kOutputSlotCount) {
        return nullptr;
    }

    m_lastPresentedSlot = readySlot;
    return (__bridge void*)m_impl->outputSlots[readySlot].outputTexture;
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

    for (auto& slot : m_impl->outputSlots) {
        // Display output: BGRA8Unorm
        MTLTextureDescriptor* outputDesc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                        width:m_width
                                       height:m_height
                                    mipmapped:NO];
        outputDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        outputDesc.storageMode = MTLStorageModePrivate;
        slot.outputTexture = [m_impl->device newTextureWithDescriptor:outputDesc];

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
            slot.msaaOutputTexture = [m_impl->device newTextureWithDescriptor:msaaOutputDesc];

            MTLTextureDescriptor* msaaDepthDesc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                            width:m_width
                                           height:m_height
                                        mipmapped:NO];
            msaaDepthDesc.textureType = MTLTextureType2DMultisample;
            msaaDepthDesc.sampleCount = m_impl->overlaySampleCount;
            msaaDepthDesc.usage = MTLTextureUsageRenderTarget;
            msaaDepthDesc.storageMode = MTLStorageModePrivate;
            slot.msaaOverlayDepthTexture = [m_impl->device newTextureWithDescriptor:msaaDepthDesc];
        } else {
            slot.msaaOutputTexture = nil;
            slot.msaaOverlayDepthTexture = nil;
        }

        MTLTextureDescriptor* overlayDepthDesc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                        width:m_width
                                       height:m_height
                                    mipmapped:NO];
        overlayDepthDesc.usage = MTLTextureUsageRenderTarget;
        overlayDepthDesc.storageMode = MTLStorageModePrivate;
        slot.overlayDepthTexture = [m_impl->device newTextureWithDescriptor:overlayDepthDesc];
    }

    // Do NOT reset inFlightSlot here — let the old completion handler drain
    // it naturally.  Forcing it to -1 would allow render() to submit a new
    // frame before the old handler fires, and the old handler's CAS would
    // then incorrectly clear the *new* submission's inFlightSlot.
    m_impl->asyncState->latestReadySlot.store(-1, std::memory_order_relaxed);
    m_impl->asyncState->latestReadySampleCount.store(0, std::memory_order_relaxed);
    for (auto& state : m_impl->asyncState->slotStates) {
        state.store(static_cast<uint8_t>(OutputSlotState::Free), std::memory_order_relaxed);
    }
    m_lastPresentedSlot = -1;
}

void MetalRayTracingRenderer::uploadSceneData() {
    if (!m_structure || m_structure->atomCount() == 0) {
        m_atomCount = 0;
        m_bondCount = 0;
        m_bvhNodeCount = 0;
        m_impl->atomPositionBuffer = nil;
        m_impl->atomColorBuffer = nil;
        m_impl->bvhNodeMinBuffer = nil;
        m_impl->bvhNodeMaxBuffer = nil;
        m_impl->bvhNodeMetaBuffer = nil;
        m_impl->bvhPrimIndexBuffer = nil;
        m_impl->bondStartBuffer = nil;
        m_impl->bondEndBuffer = nil;
        m_impl->bondColorBuffer = nil;
        m_atomDataDirty = false;
        m_bondDataDirty = false;
        return;
    }

    m_atomCount = static_cast<int>(m_structure->atomCount());

    // ── Atom buffers ──────────────────────────────────────────

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

    // ── Bond buffers ──────────────────────────────────────────

    const auto& bonds = m_structure->bonds();
    m_bondCount = static_cast<int>(bonds.bondCount());

    std::vector<float> bondStartData;
    std::vector<float> bondEndData;

    if (m_bondCount > 0) {
        bondStartData.resize(static_cast<size_t>(m_bondCount) * 4);
        bondEndData.resize(static_cast<size_t>(m_bondCount) * 4);
        std::vector<float> bondColorData(static_cast<size_t>(m_bondCount) * 4);

        const float* px = m_structure->positionsX();
        const float* py = m_structure->positionsY();
        const float* pz = m_structure->positionsZ();
        const float* cr = m_structure->colorsR();
        const float* cg = m_structure->colorsG();
        const float* cb = m_structure->colorsB();
        const auto& lattice = m_structure->lattice();
        const auto& mat = lattice.matrix;

        for (int i = 0; i < m_bondCount; ++i) {
            const auto& bond = bonds.bond(static_cast<size_t>(i));
            uint32_t a1 = bond.atomIndex1;
            uint32_t a2 = bond.atomIndex2;
            size_t idx = static_cast<size_t>(i);

            bondStartData[idx * 4 + 0] = px[a1];
            bondStartData[idx * 4 + 1] = py[a1];
            bondStartData[idx * 4 + 2] = pz[a1];
            bondStartData[idx * 4 + 3] = 0.0f;

            float ex = px[a2];
            float ey = py[a2];
            float ez = pz[a2];
            if (bond.imageX != 0 || bond.imageY != 0 || bond.imageZ != 0) {
                ex += static_cast<float>(bond.imageX * mat[0][0] + bond.imageY * mat[1][0] + bond.imageZ * mat[2][0]);
                ey += static_cast<float>(bond.imageX * mat[0][1] + bond.imageY * mat[1][1] + bond.imageZ * mat[2][1]);
                ez += static_cast<float>(bond.imageX * mat[0][2] + bond.imageY * mat[1][2] + bond.imageZ * mat[2][2]);
            }
            bondEndData[idx * 4 + 0] = ex;
            bondEndData[idx * 4 + 1] = ey;
            bondEndData[idx * 4 + 2] = ez;
            bondEndData[idx * 4 + 3] = 0.0f;

            bondColorData[idx * 4 + 0] = (cr[a1] + cr[a2]) * 0.5f;
            bondColorData[idx * 4 + 1] = (cg[a1] + cg[a2]) * 0.5f;
            bondColorData[idx * 4 + 2] = (cb[a1] + cb[a2]) * 0.5f;
            bondColorData[idx * 4 + 3] = 1.0f;
        }

        m_impl->bondStartBuffer = [m_impl->device
            newBufferWithBytes:bondStartData.data()
                        length:bondStartData.size() * sizeof(float)
                       options:MTLResourceStorageModeShared];
        m_impl->bondEndBuffer = [m_impl->device
            newBufferWithBytes:bondEndData.data()
                        length:bondEndData.size() * sizeof(float)
                       options:MTLResourceStorageModeShared];
        m_impl->bondColorBuffer = [m_impl->device
            newBufferWithBytes:bondColorData.data()
                        length:bondColorData.size() * sizeof(float)
                       options:MTLResourceStorageModeShared];
    } else {
        m_impl->bondStartBuffer = nil;
        m_impl->bondEndBuffer = nil;
        m_impl->bondColorBuffer = nil;
    }

    // ── Unified BVH (atoms + bonds) ──────────────────────────

    size_t totalPrims = static_cast<size_t>(m_atomCount) + static_cast<size_t>(m_bondCount);
    std::vector<PrimitiveBounds> primBounds(totalPrims);

    const float* ax = m_structure->positionsX();
    const float* ay = m_structure->positionsY();
    const float* az = m_structure->positionsZ();
    const float* ar = m_structure->radii();
    for (size_t i = 0; i < static_cast<size_t>(m_atomCount); ++i) {
        float r = ar[i];
        primBounds[i] = {
            ax[i] - r, ay[i] - r, az[i] - r,
            ax[i] + r, ay[i] + r, az[i] + r,
            ax[i], ay[i], az[i],
            r
        };
    }

    float bondR = m_settings.bondRadius;
    for (size_t i = 0; i < static_cast<size_t>(m_bondCount); ++i) {
        float sx = bondStartData[i * 4 + 0];
        float sy = bondStartData[i * 4 + 1];
        float sz = bondStartData[i * 4 + 2];
        float ex = bondEndData[i * 4 + 0];
        float ey = bondEndData[i * 4 + 1];
        float ez = bondEndData[i * 4 + 2];
        size_t pi = static_cast<size_t>(m_atomCount) + i;
        primBounds[pi] = {
            std::min(sx, ex) - bondR, std::min(sy, ey) - bondR, std::min(sz, ez) - bondR,
            std::max(sx, ex) + bondR, std::max(sy, ey) + bondR, std::max(sz, ez) + bondR,
            (sx + ex) * 0.5f, (sy + ey) * 0.5f, (sz + ez) * 0.5f,
            0.0f
        };
    }

    BVHData bvh = buildBVH(primBounds.data(), totalPrims);
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
    m_bondDataDirty = false;
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

    // Light direction is already in world space
    QVector3D worldLightDir = m_settings.lightDirWorld();
    rt.lightDir = simd_make_float3(worldLightDir.x(), worldLightDir.y(), worldLightDir.z());
    rt.ambient = m_settings.ambientStrength;

    QColor bg = m_settings.backgroundColor;
    rt.backgroundColor = simd_make_float4(bg.redF(), bg.greenF(), bg.blueF(), bg.alphaF());
    rt.diffuse = m_settings.diffuseStrength;
    rt.specular = m_settings.specularStrength;
    rt.shininess = m_settings.shininess;
    rt.atomCount = m_atomCount;
    rt.width = m_width;
    rt.height = m_height;
    rt.frameCount = static_cast<uint32_t>(m_sampleCount);
    rt.enableShadows = m_settings.enableShadows ? 1 : 0;
    rt.shadowOpacity = m_settings.shadowOpacity;
    rt.enableAO = m_settings.enableAmbientOcclusion ? 1 : 0;
    rt.aoSamples = m_settings.aoSamples;
    rt.aoRadius = m_settings.aoRadius;
    rt.maxSamples = m_settings.maxRTSamples;
    rt.bvhNodeCount = m_bvhNodeCount;
    rt.bondCount = m_bondCount;
    rt.bondRadius = m_settings.bondRadius;
    rt.showBonds = m_settings.showBonds ? 1 : 0;
    rt.isPerspective = camera.isPerspective() ? 1 : 0;

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

    // buffer(1)-(6): atom + BVH data (fragment stage)
    [encoder setFragmentBuffer:m_impl->atomPositionBuffer offset:0 atIndex:1];
    [encoder setFragmentBuffer:m_impl->atomColorBuffer offset:0 atIndex:2];
    [encoder setFragmentBuffer:m_impl->bvhNodeMinBuffer offset:0 atIndex:3];
    [encoder setFragmentBuffer:m_impl->bvhNodeMaxBuffer offset:0 atIndex:4];
    [encoder setFragmentBuffer:m_impl->bvhNodeMetaBuffer offset:0 atIndex:5];
    [encoder setFragmentBuffer:m_impl->bvhPrimIndexBuffer offset:0 atIndex:6];

    // buffer(7)-(9): bond data (fragment stage)
    if (m_impl->bondStartBuffer) {
        [encoder setFragmentBuffer:m_impl->bondStartBuffer offset:0 atIndex:7];
        [encoder setFragmentBuffer:m_impl->bondEndBuffer offset:0 atIndex:8];
        [encoder setFragmentBuffer:m_impl->bondColorBuffer offset:0 atIndex:9];
    }

    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    [encoder endEncoding];
}

void MetalRayTracingRenderer::renderDisplayPass(const Camera& camera,
                                                void* cmdBuf,
                                                int outputSlotIndex) {
    DisplayUniforms disp{};
    disp.sampleCount = static_cast<float>(m_sampleCount);

    id<MTLCommandBuffer> cmdBuffer = (__bridge id<MTLCommandBuffer>)cmdBuf;
    const auto& outputSlot = m_impl->outputSlots[static_cast<size_t>(outputSlotIndex)];

    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    if (outputSlot.msaaOutputTexture) {
        pass.colorAttachments[0].texture = outputSlot.msaaOutputTexture;
        pass.colorAttachments[0].resolveTexture = outputSlot.outputTexture;
        pass.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
    } else {
        pass.colorAttachments[0].texture = outputSlot.outputTexture;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    }
    pass.depthAttachment.texture = outputSlot.msaaOutputTexture ? outputSlot.msaaOverlayDepthTexture
                                                                : outputSlot.overlayDepthTexture;
    pass.depthAttachment.loadAction = MTLLoadActionClear;
    pass.depthAttachment.storeAction = MTLStoreActionDontCare;
    pass.depthAttachment.clearDepth = 1.0;

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
        gizmoUniforms.isPerspective = camera.isPerspective() ? 1 : 0;
        float len = camera.viewScale() * 0.03f;
        m_gizmoRenderer.render((__bridge void*)encoder, gizmoUniforms,
                               m_settings.rotationCenterX, m_settings.rotationCenterY,
                               m_settings.rotationCenterZ, len, /*depthTest=*/false);
    }

    if (m_settings.showViewportAxes) {
        m_viewportAxesRenderer.render((__bridge void*)encoder, camera, m_settings, m_width, m_height);
    }

    [encoder endEncoding];
}

void MetalRayTracingRenderer::renderUnitCellOverlay(const Camera& camera,
                                                    void* cmdBuf,
                                                    int outputSlotIndex) {
    const auto& outputSlot = m_impl->outputSlots[static_cast<size_t>(outputSlotIndex)];
    if (!outputSlot.outputTexture) {
        return;
    }

    const bool drawUnitCell = m_settings.showUnitCell && hasUnitCellOverlayData();
    const bool drawViewportAxes = m_settings.showViewportAxes;
    const bool drawRotationCenter = m_settings.showRotationCenter;
    if (!drawUnitCell && !drawViewportAxes && !drawRotationCenter) {
        return;
    }

    RTUnitCellUniforms unitCell{};
    if (drawUnitCell) {
        unitCell = makeRTUnitCellUniforms(camera, m_settings, m_atomCount, m_bvhNodeCount);
    }

    id<MTLCommandBuffer> cmdBuffer = (__bridge id<MTLCommandBuffer>)cmdBuf;

    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    if (outputSlot.msaaOutputTexture) {
        pass.colorAttachments[0].texture = outputSlot.msaaOutputTexture;
        pass.colorAttachments[0].resolveTexture = outputSlot.outputTexture;
        pass.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
    } else {
        pass.colorAttachments[0].texture = outputSlot.outputTexture;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    }
    pass.depthAttachment.texture = outputSlot.msaaOutputTexture ? outputSlot.msaaOverlayDepthTexture
                                                                : outputSlot.overlayDepthTexture;
    pass.depthAttachment.loadAction = MTLLoadActionClear;
    pass.depthAttachment.storeAction = MTLStoreActionDontCare;
    pass.depthAttachment.clearDepth = 1.0;
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

    if (drawUnitCell) {
        encodeUnitCellOverlayDraws((__bridge void*)encoder, unitCell);
    }

    if (drawRotationCenter) {
        QMatrix4x4 bias;
        bias(2, 2) = 0.5f;
        bias(2, 3) = 0.5f;
        SceneUniforms gizmoUniforms{};
        gizmoUniforms.viewMatrix = qMatToSimd(camera.viewMatrix());
        gizmoUniforms.projectionMatrix = qMatToSimd(bias * camera.projectionMatrix());
        gizmoUniforms.viewProjectionMatrix = qMatToSimd(bias * camera.viewProjectionMatrix());
        gizmoUniforms.isPerspective = camera.isPerspective() ? 1 : 0;
        float len = camera.viewScale() * 0.03f;
        m_gizmoRenderer.render((__bridge void*)encoder, gizmoUniforms,
                               m_settings.rotationCenterX, m_settings.rotationCenterY,
                               m_settings.rotationCenterZ, len, /*depthTest=*/false);
    }

    if (drawViewportAxes) {
        m_viewportAxesRenderer.render((__bridge void*)encoder, camera, m_settings, m_width, m_height);
    }

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

int MetalRayTracingRenderer::acquireOutputSlot() const {
    const int inFlightSlot = m_impl->asyncState->inFlightSlot.load(std::memory_order_acquire);
    if (inFlightSlot >= 0) {
        return -1;
    }

    const int latestReadySlot =
        m_impl->asyncState->latestReadySlot.load(std::memory_order_acquire);
    const auto isReusable = [&](int slotIndex) {
        return slotIndex != latestReadySlot && slotIndex != m_lastPresentedSlot;
    };

    for (int slotIndex = 0; slotIndex < kOutputSlotCount; ++slotIndex) {
        const auto state = static_cast<OutputSlotState>(
            m_impl->asyncState->slotStates[static_cast<size_t>(slotIndex)].load(std::memory_order_acquire));
        if (state == OutputSlotState::Free && isReusable(slotIndex)) {
            return slotIndex;
        }
    }

    for (int slotIndex = 0; slotIndex < kOutputSlotCount; ++slotIndex) {
        const auto state = static_cast<OutputSlotState>(
            m_impl->asyncState->slotStates[static_cast<size_t>(slotIndex)].load(std::memory_order_acquire));
        if (state == OutputSlotState::Ready && isReusable(slotIndex)) {
            return slotIndex;
        }
    }

    return -1;
}

void MetalRayTracingRenderer::trackSubmittedFrame(void* cmdBuf,
                                                  int outputSlotIndex,
                                                  int submittedSampleCount,
                                                  uint64_t generation) {
    auto asyncState = m_impl->asyncState;
    asyncState->slotStates[static_cast<size_t>(outputSlotIndex)].store(
        static_cast<uint8_t>(OutputSlotState::InFlight), std::memory_order_release);
    asyncState->inFlightSlot.store(outputSlotIndex, std::memory_order_release);

    id<MTLCommandBuffer> cmdBuffer = (__bridge id<MTLCommandBuffer>)cmdBuf;
    [cmdBuffer addCompletedHandler:^(id<MTLCommandBuffer> completedBuffer) {
        const auto freeState = static_cast<uint8_t>(OutputSlotState::Free);
        const auto readyState = static_cast<uint8_t>(OutputSlotState::Ready);

        int expectedSlot = outputSlotIndex;
        asyncState->inFlightSlot.compare_exchange_strong(expectedSlot, -1, std::memory_order_acq_rel);

        if (completedBuffer.status != MTLCommandBufferStatusCompleted ||
            asyncState->generation.load(std::memory_order_acquire) != generation) {
            asyncState->slotStates[static_cast<size_t>(outputSlotIndex)].store(
                freeState, std::memory_order_release);
            return;
        }

        asyncState->slotStates[static_cast<size_t>(outputSlotIndex)].store(
            readyState, std::memory_order_release);
        asyncState->latestReadySampleCount.store(submittedSampleCount, std::memory_order_release);
        asyncState->latestReadySlot.store(outputSlotIndex, std::memory_order_release);
    }];
}

} // namespace atom::render::metal
