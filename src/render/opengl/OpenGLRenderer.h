#pragma once

#include "../Renderer.h"
#include "ShaderManager.h"
#include "SphereRenderer.h"
#include "BondRenderer.h"

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
    void setStructure(const data::AtomicStructure* structure) override;
    void render(const Camera& camera) override;
    void invalidateAtomData() override;
    void invalidateBondData() override;

    // Accessors
    ShaderManager* shaderManager() { return &m_shaderManager; }
    SphereRenderer* sphereRenderer() { return &m_sphereRenderer; }
    BondRenderer* bondRenderer() { return &m_bondRenderer; }

    int viewportWidth() const { return m_width; }
    int viewportHeight() const { return m_height; }

private:
    void renderBackground();

    ShaderManager m_shaderManager;
    SphereRenderer m_sphereRenderer;
    BondRenderer m_bondRenderer;

    const data::AtomicStructure* m_structure = nullptr;

    int m_width = 800;
    int m_height = 600;
    bool m_initialized = false;
    bool m_atomDataDirty = true;
    bool m_bondDataDirty = true;
};

} // namespace atom::render
