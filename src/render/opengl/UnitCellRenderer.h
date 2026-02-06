#pragma once

#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLFunctions>

namespace atom::data {
class Structure;
}

namespace atom::render {

class ShaderManager;
class Camera;
struct RenderSettings;

/**
 * @brief Renders the unit cell as wireframe lines
 *
 * Draws the 12 edges of the parallelepiped defined by the lattice vectors.
 * Uses simple GL_LINES with the line shader — no lighting, no ray tracing.
 */
class UnitCellRenderer : protected QOpenGLFunctions {
public:
    UnitCellRenderer();
    ~UnitCellRenderer();

    bool initialize(ShaderManager* shaderManager);
    void cleanup();

    /**
     * @brief Upload unit cell edge data from structure's lattice
     * @param structure Atomic structure (may be null or have no lattice)
     */
    void setUnitCellData(const data::Structure* structure);

    /**
     * @brief Render unit cell edges
     */
    void render(const Camera& camera, const RenderSettings& settings);

    bool hasData() const { return m_edgeCount > 0; }

private:
    ShaderManager* m_shaderManager = nullptr;

    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_positionBuffer;  // vec3 per vertex
    QOpenGLBuffer m_colorBuffer;     // vec4 per vertex
    QOpenGLBuffer m_indexBuffer;     // line indices

    int m_edgeCount = 0;  // number of line segments (12 for a full cell)
    bool m_initialized = false;
};

} // namespace atom::render
