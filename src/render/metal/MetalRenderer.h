#pragma once

#include "../common/Renderer.h"
#include "MetalShaderLibrary.h"
#include "MetalSphereRenderer.h"
#include "MetalBondRenderer.h"
#include "MetalUnitCellRenderer.h"
#include <memory>

namespace atom::render::metal {

/// Raster renderer using Metal. Implements the backend-agnostic Renderer
/// interface and coordinates sphere, bond, and unit cell sub-renderers.
class MetalRenderer : public Renderer {
public:
    MetalRenderer();
    ~MetalRenderer() override;

    /// Must be called before initialize(). Pass the raw id<MTLDevice> as void*.
    void setDevice(void* device);

    // --- Renderer interface ---
    bool initialize() override;
    void cleanup() override;
    void resize(int width, int height) override;
    void setStructure(const data::Structure* structure) override;
    void render(const Camera& camera, const RenderSettings& settings) override;
    void invalidateAtomData() override;
    void invalidateBondData() override;

    /// Returns the offscreen color texture (id<MTLTexture> as void*).
    void* colorTexture() const;

private:
    void createRenderTargets();

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    MetalShaderLibrary m_shaderLibrary;
    MetalSphereRenderer m_sphereRenderer;
    MetalBondRenderer m_bondRenderer;
    MetalUnitCellRenderer m_unitCellRenderer;

    const data::Structure* m_structure = nullptr;
    int m_width = 0;
    int m_height = 0;
    bool m_initialized = false;
    bool m_atomDataDirty = true;
    bool m_bondDataDirty = true;
    bool m_unitCellDataDirty = true;
};

} // namespace atom::render::metal
