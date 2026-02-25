#pragma once

#include <memory>

namespace atom::render {
class Camera;
struct RenderSettings;
}

namespace atom::render::metal {

class MetalShaderLibrary;

/// Viewport-corner XYZ axes overlay rendered as real 3D geometry (cylinders + cones).
/// This is an overlay object (not scene geometry / ray tracing geometry).
class MetalViewportAxesRenderer {
public:
    MetalViewportAxesRenderer();
    ~MetalViewportAxesRenderer();

    bool initialize(void* device, MetalShaderLibrary* shaderLibrary);
    void cleanup();

    void render(void* encoder,
                const atom::render::Camera& camera,
                const atom::render::RenderSettings& settings,
                int viewportWidth,
                int viewportHeight);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    MetalShaderLibrary* m_shaderLibrary = nullptr;
    bool m_initialized = false;
};

} // namespace atom::render::metal
