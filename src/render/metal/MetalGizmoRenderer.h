#pragma once

#include <memory>

namespace atom::render {
class Camera;
struct RenderSettings;
}

namespace atom::render::metal {

class MetalShaderLibrary;

/// Draws a fixed-size 3-axis cylinder gizmo at the viewport center.
/// Used to visualize the camera rotation center during interaction.
class MetalGizmoRenderer {
public:
    MetalGizmoRenderer();
    ~MetalGizmoRenderer();

    bool initialize(void* device, MetalShaderLibrary* shaderLibrary);
    void cleanup();

    /// Uses the overlay pass's fresh depth buffer for axis self-occlusion.
    void render(void* encoder, const Camera& camera, const RenderSettings& settings,
                int viewportWidth, int viewportHeight);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    MetalShaderLibrary* m_shaderLibrary = nullptr;
    bool m_initialized = false;
};

} // namespace atom::render::metal
