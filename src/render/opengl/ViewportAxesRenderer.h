#pragma once

#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLFunctions>

namespace atom::render {

class ShaderManager;
class Camera;
struct RenderSettings;

/// Viewport-corner XYZ axes overlay (backend-rendered 3D geometry, not scene geometry).
class ViewportAxesRenderer : protected QOpenGLFunctions {
public:
    ViewportAxesRenderer();
    ~ViewportAxesRenderer();

    bool initialize(ShaderManager* shaderManager);
    void cleanup();

    void render(const Camera& camera, const RenderSettings& settings,
                int viewportWidth, int viewportHeight);

private:
    void createCappedCylinderGeometry(int segments);
    void createCappedConeGeometry(int segments);
    void createCenterSphereGeometry(int stacks, int segments);
    void setupMeshVAO(QOpenGLVertexArrayObject& vao,
                      QOpenGLBuffer& vertexBuffer,
                      QOpenGLBuffer& indexBuffer);

    ShaderManager* m_shaderManager = nullptr;

    QOpenGLVertexArrayObject m_cylinderVAO;
    QOpenGLBuffer m_cylinderVBO;
    QOpenGLBuffer m_cylinderIBO;
    int m_cylinderIndexCount = 0;

    QOpenGLVertexArrayObject m_coneVAO;
    QOpenGLBuffer m_coneVBO;
    QOpenGLBuffer m_coneIBO;
    int m_coneIndexCount = 0;

    QOpenGLVertexArrayObject m_centerSphereVAO;
    QOpenGLBuffer m_centerSphereVBO;
    QOpenGLBuffer m_centerSphereIBO;
    int m_centerSphereIndexCount = 0;

    // Shared per-instance attributes for both cylinder and cone draws.
    QOpenGLBuffer m_instanceStartBuffer;
    QOpenGLBuffer m_instanceEndBuffer;
    QOpenGLBuffer m_instanceColorBuffer;

    bool m_initialized = false;
};

} // namespace atom::render
