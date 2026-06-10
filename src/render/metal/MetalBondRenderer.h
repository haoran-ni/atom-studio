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

    /// Encodes the per-bond frame precompute pass (own compute encoder) into
    /// the command buffer. Must run before the render pass that draws bonds.
    /// Engages only above a bond-count threshold; render() falls back to the
    /// inline vertex path otherwise. Math is bit-identical either way.
    void encodeFramePrecompute(void* cmdBuffer, const SceneUniforms& uniforms);

    void render(void* encoder, const SceneUniforms& uniforms, int cylinderSegments);

    size_t bondCount() const { return m_bondCount; }

private:
    void createCylinderGeometry(int segments);
    void ensureCylinderGeometry(int segments);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    MetalShaderLibrary* m_shaderLibrary = nullptr;
    int m_cylinderIndexCount = 0;
    int m_meshSegments = 0;
    size_t m_bondCount = 0;
    bool m_initialized = false;
    bool m_usePrecomputedFrames = false;
};

} // namespace atom::render::metal
