#pragma once

#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLFunctions>
#include <array>
#include <vector>

namespace atom::data {
class Structure;
}

namespace atom::render {

class ShaderManager;
class Camera;
struct RenderSettings;

/**
 * @brief Renders the unit-cell object as a thick wireframe cuboid
 *
 * Draws 12 cylindrical edges plus 8 corner joints (sphere impostors) so
 * intersections are smooth. The object is rendered in raster passes only.
 */
class UnitCellRenderer : protected QOpenGLFunctions {
public:
    UnitCellRenderer();
    ~UnitCellRenderer();

    bool initialize(ShaderManager* shaderManager);
    void cleanup();

    /**
     * @brief Upload unit-cell object instance data from structure lattice
     * @param structure Atomic structure (may be null or have no lattice)
     */
    void setUnitCellData(const data::Structure* structure);

    /**
     * @brief Render unit-cell object
     */
    void render(const Camera& camera, const RenderSettings& settings);

    bool hasData() const { return m_edgeCount > 0; }

private:
    void createCylinderGeometry(int segments);
    void createJointBillboardGeometry();

    ShaderManager* m_shaderManager = nullptr;

    // Edge cylinders (12 instances)
    QOpenGLVertexArrayObject m_cylinderVAO;
    QOpenGLBuffer m_cylinderVBO;       // unit cylinder vertices (vec3)
    QOpenGLBuffer m_cylinderIBO;       // cylinder triangle indices
    QOpenGLBuffer m_edgeStartBuffer;   // instance start (vec3)
    QOpenGLBuffer m_edgeEndBuffer;     // instance end (vec3)
    QOpenGLBuffer m_edgeColorBuffer;   // instance color (vec4)
    QOpenGLBuffer m_edgeRadiusBuffer;  // instance radii (vec2)
    int m_cylinderIndexCount = 0;

    // Corner joints (8 billboard-sphere instances)
    QOpenGLVertexArrayObject m_jointVAO;
    QOpenGLBuffer m_jointQuadVBO;           // billboard quad (vec3)
    QOpenGLBuffer m_jointPosRadiusBuffer;   // instance center + base radius (vec4)
    QOpenGLBuffer m_jointColorBuffer;       // instance color (vec4)

    std::array<float, 12 * 3> m_edgeStarts{};
    std::array<float, 12 * 3> m_edgeEnds{};
    std::array<float, 8 * 4> m_jointPosRadius{};

    int m_edgeCount = 0;   // number of edge-cylinder instances
    int m_jointCount = 0;  // number of corner-joint instances
    bool m_initialized = false;
};

} // namespace atom::render
