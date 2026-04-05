#pragma once

#include <cstddef>
#include <memory>

namespace atom::data { class Structure; }

namespace atom::render::metal {

class MetalShaderLibrary;
struct SceneUniforms;

class MetalBondRenderer {
public:
    MetalBondRenderer();
    ~MetalBondRenderer();

    bool initialize(void* device, MetalShaderLibrary* shaderLibrary);
    void cleanup();

    void setBondData(const data::Structure* structure);
    void render(void* encoder, const SceneUniforms& uniforms);

    size_t bondCount() const { return m_bondCount; }

private:
    void createQuadGeometry();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    MetalShaderLibrary* m_shaderLibrary = nullptr;
    size_t m_bondCount = 0;
    bool m_initialized = false;
};

} // namespace atom::render::metal
