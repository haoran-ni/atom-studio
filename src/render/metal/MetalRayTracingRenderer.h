#pragma once

#include "../common/Renderer.h"
#include "MetalShaderLibrary.h"
#include <memory>
#include <cstdint>

namespace atom::render::metal {

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
    void render(const Camera& camera) override;
    void invalidateAtomData() override;
    void invalidateBondData() override;

    // Progressive rendering state
    int sampleCount() const { return m_sampleCount; }
    bool isConverged() const { return m_sampleCount >= m_settings.maxRTSamples; }
    void resetAccumulation();

    /// Returns the display-ready output texture (id<MTLTexture> as void*).
    void* outputTexture() const;

private:
    void createRenderTargets();
    void uploadAtomData();
    void uploadUnitCellData();
    void renderRTPass(const Camera& camera);
    void renderDisplayPass(const Camera& camera);
    void renderUnitCellOverlay(const Camera& camera);
    uint64_t computeStateHash(const Camera& camera) const;

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    MetalShaderLibrary m_shaderLibrary;

    const data::Structure* m_structure = nullptr;
    int m_width = 0;
    int m_height = 0;
    int m_atomCount = 0;
    int m_sampleCount = 0;
    bool m_initialized = false;
    bool m_atomDataDirty = true;
    bool m_unitCellDataDirty = true;
    int m_unitCellEdgeCount = 0;
    int m_unitCellJointCount = 0;
    int m_unitCellCylinderIndexCount = 0;
    int m_unitCellSphereIndexCount = 0;
    uint64_t m_lastStateHash = 0;
};

} // namespace atom::render::metal
