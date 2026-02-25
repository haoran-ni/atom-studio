#pragma once

#include <memory>

namespace atom::render::metal {

class MetalShaderLibrary;
struct SceneUniforms;

/// Draws a 3-axis (X/Y/Z) line gizmo at a given world-space position.
/// Used to visualize the camera rotation center during interaction.
class MetalGizmoRenderer {
public:
    MetalGizmoRenderer();
    ~MetalGizmoRenderer();

    bool initialize(void* device, MetalShaderLibrary* shaderLibrary);
    void cleanup();

    /// Render ±X (red), ±Y (green), ±Z (blue) line segments through (cx, cy, cz)
    /// with half-length axisLength.
    /// @param depthTest  true = depth-tested (raster pass); false = always on top (RT display pass).
    void render(void* encoder, const SceneUniforms& uniforms,
                float cx, float cy, float cz, float axisLength,
                bool depthTest = true);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    MetalShaderLibrary* m_shaderLibrary = nullptr;
    bool m_initialized = false;
};

} // namespace atom::render::metal
