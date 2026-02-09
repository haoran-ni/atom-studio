#pragma once

#include <cstddef>
#include <memory>

namespace atom::data { class Structure; }

namespace atom::render::metal {

class MetalShaderLibrary;
struct SceneUniforms;

class MetalSphereRenderer {
public:
    MetalSphereRenderer();
    ~MetalSphereRenderer();

    bool initialize(void* device, MetalShaderLibrary* shaderLibrary);
    void cleanup();

    void setAtomData(const data::Structure* structure);
    /// Encode draw commands into an existing render command encoder.
    void render(void* encoder, const SceneUniforms& uniforms);

    size_t atomCount() const { return m_atomCount; }

private:
    void createQuadGeometry();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    MetalShaderLibrary* m_shaderLibrary = nullptr;
    size_t m_atomCount = 0;
    bool m_initialized = false;
};

} // namespace atom::render::metal
