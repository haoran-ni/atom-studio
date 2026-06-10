#pragma once

#include "../common/Renderer.h"
#include "MetalShaderLibrary.h"
#include "MetalGizmoRenderer.h"
#include "MetalViewportAxesRenderer.h"
#include <memory>
#include <cstdint>

namespace atom::render::metal {

struct RTUnitCellUniforms;

/// Fragment-shader ray tracing renderer with progressive accumulation.
/// Parallel to the OpenGL RayTracingRenderer — brute-force ray-sphere
/// traversal in a fullscreen fragment shader, additive accumulation into
/// an RGBA32Float texture, tone-mapped display pass.
class MetalRayTracingRenderer : public Renderer {
public:
    MetalRayTracingRenderer();
    ~MetalRayTracingRenderer() override;

    void setDevice(void* device);

    // --- Renderer interface ---
    bool initialize() override;
    void cleanup() override;
    void resize(int width, int height) override;
    void setStructure(const data::Structure* structure) override;
    void render(const Camera& camera, const RenderSettings& settings) override;
    void invalidateAtomData() override;
    void invalidateBondData() override;
    void invalidateAppearance() override;

    // Progressive rendering state
    int sampleCount() const { return m_sampleCount; }
    bool isConverged() const { return m_sampleCount >= m_settings.maxRTSamples; }
    bool needsMoreFrames() const;
    void resetAccumulation();

    /// Returns the display-ready output texture (id<MTLTexture> as void*).
    void* outputTexture();

private:
    void createRenderTargets();
    void uploadSceneData();
    void uploadAppearanceData();
    void uploadUnitCellData();
    void renderRTPass(const Camera& camera, void* cmdBuffer);
    void renderDisplayPass(const Camera& camera, void* cmdBuffer, int outputSlotIndex);
    void renderUnitCellOverlay(const Camera& camera, void* cmdBuffer, int outputSlotIndex);
    bool hasUnitCellOverlayData() const;
    void encodeUnitCellOverlayDraws(void* encoder, const RTUnitCellUniforms& unitCell);
    void trackSubmittedFrame(void* cmdBuffer,
                             int outputSlotIndex,
                             int submittedSampleCount,
                             int batchSampleCount,
                             uint64_t generation);

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    MetalShaderLibrary m_shaderLibrary;
    MetalGizmoRenderer m_gizmoRenderer;
    MetalViewportAxesRenderer m_viewportAxesRenderer;

    RenderSettings m_settings;  // Local copy for isConverged() / state hashing
    const data::Structure* m_structure = nullptr;
    int m_width = 0;
    int m_height = 0;
    int m_atomCount = 0;
    int m_bondCount = 0;
    int m_bvhNodeCount = 0;
    // Scene AABB (including primitive radii) — used to bound outline widths
    float m_sceneBoundsMin[3] = {0.0f, 0.0f, 0.0f};
    float m_sceneBoundsMax[3] = {0.0f, 0.0f, 0.0f};
    int m_sampleCount = 0;
    bool m_initialized = false;
    bool m_atomDataDirty = true;
    bool m_bondDataDirty = true;
    bool m_appearanceDirty = false;
    bool m_accumNeedsClear = true;
    bool m_unitCellDataDirty = true;
    int m_unitCellEdgeCount = 0;
    int m_unitCellJointCount = 0;
    int m_unitCellCylinderIndexCount = 0;
    int m_unitCellSphereIndexCount = 0;
    uint64_t m_lastStateHash = 0;
    uint64_t m_outputGeneration = 1;
    int m_lastPresentedSlot = -1;
};

} // namespace atom::render::metal
