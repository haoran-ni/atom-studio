#pragma once

#include <cstddef>
#include <memory>
#include "../common/PreparedGeometry.h"

namespace atom::data { class Structure; }

namespace atom::render {
class Camera;
}

namespace atom::render::metal {

class MetalShaderLibrary;
struct SceneUniforms;

class MetalSphereRenderer {
public:
    MetalSphereRenderer();
    ~MetalSphereRenderer();

    bool initialize(void* device, MetalShaderLibrary* shaderLibrary);
    void cleanup();

    void setAtomData(const data::Structure* structure, const PreparedGeometry* geometry = nullptr);
    void updateAppearance(const data::Structure* structure);
    bool hasSelection() const { return m_hasSelection; }
    /// Encode draw commands into an existing render command encoder.
    /// Uses the early-Z pipeline when uniforms.sphereEarlyZ is set.
    void render(void* encoder, const SceneUniforms& uniforms);

    /// True when the early-Z sphere variant is safe for the current view:
    /// every atom's near-tangent plane (including outline shell) stays
    /// safely beyond the camera near plane, so the near-tangent billboard
    /// placement renders pixel-identically to the default placement.
    bool canUseEarlyZ(const Camera& camera, float atomScale,
                      float outlineWidthPx, float outlinePixelScale) const;

    size_t atomCount() const { return m_atomCount; }

private:
    void createQuadGeometry();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    MetalShaderLibrary* m_shaderLibrary = nullptr;
    size_t m_atomCount = 0;
    bool m_initialized = false;
    bool m_hasSelection = false;

    // Atom-center bounds + max base radius for the early-Z gate.
    bool m_hasBounds = false;
    float m_boundsMin[3] = {0.0f, 0.0f, 0.0f};
    float m_boundsMax[3] = {0.0f, 0.0f, 0.0f};
    float m_maxBaseRadius = 0.0f;
};

} // namespace atom::render::metal
