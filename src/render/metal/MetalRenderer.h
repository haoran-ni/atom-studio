#pragma once

#include "../common/Renderer.h"
#include "../common/PreparedGeometry.h"
#include "MetalAsyncOutput.h"
#include "MetalShaderLibrary.h"
#include "MetalSphereRenderer.h"
#include "MetalBondRenderer.h"
#include "MetalUnitCellRenderer.h"
#include "MetalGizmoRenderer.h"
#include "MetalViewportAxesRenderer.h"
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
    void releaseStructure();
    void setPreparedStructure(const data::Structure* structure,
                              std::shared_ptr<const PreparedGeometry> geometry);
    void render(const Camera& camera, const RenderSettings& settings) override;
    void invalidateAtomData() override;
    void invalidateBondData() override;
    void invalidateAppearance() override;

    /// Returns the most recently completed color texture (id<MTLTexture> as
    /// void*), or nullptr while no frame has finished yet. Marks the returned
    /// slot as presented so it is not reused while the scene graph samples it.
    /// Also returns the request token attached to that completed texture.
    void* colorTexture(uint64_t& frameRequestToken);

    /// True when render() was called while a frame was still in flight, so
    /// the latest scene state has not been submitted yet. The viewport should
    /// schedule another frame.
    bool hasPendingRender() const;

    /// True while the viewport must keep scheduling frames: a frame is in
    /// flight, a render request was dropped, or a completed frame has not
    /// been presented yet.
    bool needsMoreFrames() const;

private:
    void createRenderTargets();
    void trackSubmittedFrame(void* cmdBuffer, int outputSlotIndex, uint64_t generation);

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    MetalShaderLibrary m_shaderLibrary;
    MetalSphereRenderer m_sphereRenderer;
    MetalBondRenderer m_bondRenderer;
    MetalUnitCellRenderer m_unitCellRenderer;
    MetalGizmoRenderer m_gizmoRenderer;
    MetalViewportAxesRenderer m_viewportAxesRenderer;

    const data::Structure* m_structure = nullptr;
    std::shared_ptr<const PreparedGeometry> m_preparedGeometry;
    int m_width = 0;
    int m_height = 0;
    bool m_initialized = false;
    bool m_atomDataDirty = true;
    bool m_bondDataDirty = true;
    bool m_unitCellDataDirty = true;
    bool m_appearanceDirty = false;
    bool m_pendingRender = false;
    uint64_t m_outputGeneration = 1;
    int m_lastPresentedSlot = -1;
};

} // namespace atom::render::metal
