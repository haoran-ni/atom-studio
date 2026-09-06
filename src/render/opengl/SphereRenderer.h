#pragma once

#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLFunctions>
#include <memory>
#include <vector>

namespace atom::data {
class Structure;
}

namespace atom::render {

class ShaderManager;
class Camera;
struct RenderSettings;

/**
 * @brief Renders atoms as impostor spheres
 *
 * Uses instanced rendering of billboard quads with ray-sphere
 * intersection in the fragment shader for high-quality sphere rendering.
 */
class SphereRenderer : protected QOpenGLFunctions {
public:
    SphereRenderer();
    ~SphereRenderer();

    /**
     * @brief Initialize OpenGL resources
     * @param shaderManager Shader manager with compiled shaders
     * @return true if initialization succeeded
     */
    bool initialize(ShaderManager* shaderManager);

    /**
     * @brief Clean up OpenGL resources
     */
    void cleanup();

    /**
     * @brief Upload atom data from structure
     * @param structure Atomic structure (may be null)
     */
    void setAtomData(const data::Structure* structure);

    /**
     * @brief Render atoms
     * @param camera Camera for matrices
     * @param settings Render settings
     */
    void render(const Camera& camera, const RenderSettings& settings);

    /**
     * @brief Get number of atoms being rendered
     */
    size_t atomCount() const { return m_atomCount; }

private:
    void createQuadGeometry();

    ShaderManager* m_shaderManager = nullptr;

    // Quad geometry (shared for all spheres)
    QOpenGLVertexArrayObject m_quadVAO;
    QOpenGLBuffer m_quadVBO;

    // Instance data
    QOpenGLBuffer m_instancePosBuffer;   // vec4: xyz + radius
    QOpenGLBuffer m_instanceColorBuffer; // vec4: rgb, opaque padding

    size_t m_atomCount = 0;
    bool m_initialized = false;
};

} // namespace atom::render
