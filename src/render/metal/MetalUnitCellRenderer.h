#pragma once

#include <memory>

namespace atom::data { class Structure; }
namespace atom::render { struct RenderSettings; }

namespace atom::render::metal {

class MetalShaderLibrary;
struct SceneUniforms;

class MetalUnitCellRenderer {
public:
    MetalUnitCellRenderer();
    ~MetalUnitCellRenderer();

    bool initialize(void* device, MetalShaderLibrary* shaderLibrary);
    void cleanup();

    void setUnitCellData(const data::Structure* structure);
    void render(void* encoder, const SceneUniforms& uniforms, const RenderSettings& settings);

    bool hasData() const { return m_edgeCount > 0 && m_jointCount > 0; }

private:
    void createCylinderGeometry(int segments);
    void createJointQuadGeometry();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    MetalShaderLibrary* m_shaderLibrary = nullptr;
    int m_edgeCount = 0;
    int m_jointCount = 0;
    int m_cylinderIndexCount = 0;
    bool m_initialized = false;
};

} // namespace atom::render::metal
