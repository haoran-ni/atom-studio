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
 * @brief Renders bonds as cylinders
 *
 * Uses instanced rendering of cylinder geometry.
 */
class BondRenderer : protected QOpenGLFunctions {
public:
    BondRenderer();
    ~BondRenderer();

    /**
     * @brief Initialize OpenGL resources
     */
    bool initialize(ShaderManager* shaderManager);

    /**
     * @brief Clean up OpenGL resources
     */
    void cleanup();

    /**
     * @brief Upload bond data from structure
     */
    void setBondData(const data::Structure* structure);

    /**
     * @brief Render bonds
     */
    void render(const Camera& camera, const RenderSettings& settings);

    /**
     * @brief Get number of bonds being rendered
     */
    size_t bondCount() const { return m_bondCount; }

private:
    void createCylinderGeometry(int segments);

    ShaderManager* m_shaderManager = nullptr;

    // Cylinder geometry (shared for all bonds)
    QOpenGLVertexArrayObject m_cylinderVAO;
    QOpenGLBuffer m_cylinderVBO;
    QOpenGLBuffer m_cylinderIBO;
    int m_cylinderVertexCount = 0;
    int m_cylinderIndexCount = 0;

    // Instance data
    QOpenGLBuffer m_instanceStartBuffer;  // vec3: start position
    QOpenGLBuffer m_instanceEndBuffer;    // vec3: end position
    QOpenGLBuffer m_instanceStartColorBuffer;  // vec4: rgba
    QOpenGLBuffer m_instanceEndColorBuffer;    // vec4: rgba
    QOpenGLBuffer m_instanceRadiusBuffer;      // vec2: start/end radii

    size_t m_bondCount = 0;
    bool m_initialized = false;
};

} // namespace atom::render
