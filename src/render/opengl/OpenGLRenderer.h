#pragma once

#include "../common/Renderer.h"
#include "ShaderManager.h"
#include "SphereRenderer.h"
#include "BondRenderer.h"
#include "UnitCellRenderer.h"

#include <QOpenGLFunctions>
#include <memory>

namespace atom::render {

/**
 * @brief OpenGL-based renderer for atomic structures
 *
 * Main renderer class that coordinates sphere and bond rendering
 * using modern OpenGL (3.3+ core profile).
 */
class OpenGLRenderer : public Renderer, protected QOpenGLFunctions {
public:
    OpenGLRenderer();
    ~OpenGLRenderer() override;

    // Renderer interface
    bool initialize() override;
    void cleanup() override;
    void resize(int width, int height) override;
    void setStructure(const data::Structure* structure) override;
    void render(const Camera& camera, const RenderSettings& settings) override;
    void invalidateAtomData() override;
    void invalidateBondData() override;

    // Accessors
    ShaderManager* shaderManager() { return &m_shaderManager; }
    SphereRenderer* sphereRenderer() { return &m_sphereRenderer; }
    BondRenderer* bondRenderer() { return &m_bondRenderer; }
    UnitCellRenderer* unitCellRenderer() { return &m_unitCellRenderer; }

    int viewportWidth() const { return m_width; }
    int viewportHeight() const { return m_height; }

private:
    void renderBackground(const RenderSettings& settings);

    ShaderManager m_shaderManager;
    SphereRenderer m_sphereRenderer;
    BondRenderer m_bondRenderer;
    UnitCellRenderer m_unitCellRenderer;

    const data::Structure* m_structure = nullptr;

    int m_width = 800;
    int m_height = 600;
    bool m_initialized = false;
    bool m_atomDataDirty = true;
    bool m_bondDataDirty = true;
    bool m_unitCellDataDirty = true;
};

} // namespace atom::render
