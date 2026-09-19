#pragma once

#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLVertexArrayObject>

namespace atom::render {

class ShaderManager;
class Camera;
struct RenderSettings;

class GizmoRenderer : protected QOpenGLFunctions {
public:
    GizmoRenderer();
    ~GizmoRenderer();

    bool initialize(ShaderManager* shaderManager);
    void cleanup();

    void render(const Camera& camera, const RenderSettings& settings,
                int viewportWidth, int viewportHeight);

private:
    void createCylinderGeometry(int segments);

    ShaderManager* m_shaderManager = nullptr;

    QOpenGLVertexArrayObject m_cylinderVAO;
    QOpenGLBuffer m_cylinderVBO;
    QOpenGLBuffer m_cylinderIBO;
    QOpenGLBuffer m_instanceStartBuffer;
    QOpenGLBuffer m_instanceEndBuffer;
    QOpenGLBuffer m_instanceColorBuffer;

    int m_cylinderIndexCount = 0;
    bool m_initialized = false;
};

} // namespace atom::render
