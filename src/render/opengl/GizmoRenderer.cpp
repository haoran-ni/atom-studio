#include "GizmoRenderer.h"

#include "CylinderMesh.h"
#include "ShaderManager.h"
#include "../common/Camera.h"
#include "../common/GizmoOverlay.h"
#include "../common/RenderSettings.h"

#include <QOpenGLShaderProgram>

#include <array>
#include <vector>
#include <QVector3D>
#include <QVector4D>

namespace atom::render {

namespace {

constexpr int kGizmoCylinderSegments = 16;
constexpr int kGizmoAxisSegmentCount = 6;

} // namespace

GizmoRenderer::GizmoRenderer()
    : m_cylinderVBO(QOpenGLBuffer::VertexBuffer)
    , m_cylinderIBO(QOpenGLBuffer::IndexBuffer)
    , m_instanceStartBuffer(QOpenGLBuffer::VertexBuffer)
    , m_instanceEndBuffer(QOpenGLBuffer::VertexBuffer)
    , m_instanceColorBuffer(QOpenGLBuffer::VertexBuffer)
{
}

GizmoRenderer::~GizmoRenderer() {
    cleanup();
}

bool GizmoRenderer::initialize(ShaderManager* shaderManager) {
    if (m_initialized) return true;

    initializeOpenGLFunctions();

    m_shaderManager = shaderManager;
    if (!m_shaderManager || !m_shaderManager->isInitialized()) {
        return false;
    }

    if (!m_cylinderVAO.create()) {
        return false;
    }

    if (!m_cylinderVBO.create() || !m_cylinderIBO.create() ||
        !m_instanceStartBuffer.create() || !m_instanceEndBuffer.create() ||
        !m_instanceColorBuffer.create()) {
        return false;
    }

    createCylinderGeometry(kGizmoCylinderSegments);

    m_cylinderVAO.bind();

    m_cylinderVBO.bind();
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CylinderMeshVertex), nullptr);

    m_instanceStartBuffer.bind();
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glVertexAttribDivisor(1, 1);

    m_instanceEndBuffer.bind();
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glVertexAttribDivisor(2, 1);

    m_instanceColorBuffer.bind();
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glVertexAttribDivisor(3, 1);

    m_cylinderIBO.bind();
    m_cylinderVAO.release();

    m_initialized = true;
    return true;
}

void GizmoRenderer::cleanup() {
    m_instanceColorBuffer.destroy();
    m_instanceEndBuffer.destroy();
    m_instanceStartBuffer.destroy();
    m_cylinderIBO.destroy();
    m_cylinderVBO.destroy();
    m_cylinderVAO.destroy();

    m_cylinderIndexCount = 0;
    m_initialized = false;
}

void GizmoRenderer::createCylinderGeometry(int segments) {
    std::vector<CylinderMeshVertex> vertices;
    std::vector<unsigned int> indices;
    buildCappedUnitCylinderMesh(segments, vertices, indices);

    m_cylinderIndexCount = static_cast<int>(indices.size());

    m_cylinderVBO.bind();
    m_cylinderVBO.allocate(vertices.data(),
                           static_cast<int>(vertices.size() * sizeof(CylinderMeshVertex)));

    m_cylinderIBO.bind();
    m_cylinderIBO.allocate(indices.data(),
                           static_cast<int>(indices.size() * sizeof(unsigned int)));
}

void GizmoRenderer::render(const Camera& camera, const RenderSettings& settings,
                           int viewportWidth, int viewportHeight) {
    if (!m_initialized || m_cylinderIndexCount == 0 ||
        viewportWidth <= 0 || viewportHeight <= 0) return;

    QOpenGLShaderProgram* shader = m_shaderManager->viewportAxesShader();
    if (!shader) return;

    const auto overlay = makeGizmoOverlay(camera, viewportWidth, viewportHeight,
                                         settings.viewportAxesPixelRatio);
    const float len = overlay.axisLength;
    constexpr float cx = 0.0f, cy = 0.0f, cz = 0.0f;

    std::array<float, kGizmoAxisSegmentCount * 3> starts{};
    std::array<float, kGizmoAxisSegmentCount * 3> ends{};
    std::array<float, kGizmoAxisSegmentCount * 4> colors{};

    const std::array<QVector3D, kGizmoAxisSegmentCount> endPoints = {{
        QVector3D(cx + len, cy, cz),
        QVector3D(cx - len, cy, cz),
        QVector3D(cx, cy + len, cz),
        QVector3D(cx, cy - len, cz),
        QVector3D(cx, cy, cz + len),
        QVector3D(cx, cy, cz - len),
    }};
    const std::array<QVector4D, kGizmoAxisSegmentCount> axisColors = {{
        QVector4D(1.0f, 0.25f, 0.25f, 1.0f),
        QVector4D(1.0f, 0.25f, 0.25f, 1.0f),
        QVector4D(0.25f, 1.0f, 0.25f, 1.0f),
        QVector4D(0.25f, 1.0f, 0.25f, 1.0f),
        QVector4D(0.25f, 0.50f, 1.0f, 1.0f),
        QVector4D(0.25f, 0.50f, 1.0f, 1.0f),
    }};

    for (int i = 0; i < kGizmoAxisSegmentCount; ++i) {
        starts[i * 3 + 0] = cx;
        starts[i * 3 + 1] = cy;
        starts[i * 3 + 2] = cz;
        ends[i * 3 + 0] = endPoints[i].x();
        ends[i * 3 + 1] = endPoints[i].y();
        ends[i * 3 + 2] = endPoints[i].z();
        colors[i * 4 + 0] = axisColors[i].x();
        colors[i * 4 + 1] = axisColors[i].y();
        colors[i * 4 + 2] = axisColors[i].z();
        colors[i * 4 + 3] = axisColors[i].w();
    }

    shader->bind();
    shader->setUniformValue("uViewMatrix", overlay.viewMatrix);
    shader->setUniformValue("uProjectionMatrix", overlay.projectionMatrix);
    shader->setUniformValue("uBondRadius", overlay.radius);

    m_cylinderVAO.bind();
    m_instanceStartBuffer.bind();
    m_instanceStartBuffer.allocate(starts.data(),
                                   static_cast<int>(starts.size() * sizeof(float)));
    m_instanceEndBuffer.bind();
    m_instanceEndBuffer.allocate(ends.data(),
                                 static_cast<int>(ends.size() * sizeof(float)));
    m_instanceColorBuffer.bind();
    m_instanceColorBuffer.allocate(colors.data(),
                                   static_cast<int>(colors.size() * sizeof(float)));

    glDrawElementsInstanced(GL_TRIANGLES,
                            m_cylinderIndexCount,
                            GL_UNSIGNED_INT,
                            nullptr,
                            kGizmoAxisSegmentCount);

    m_cylinderVAO.release();
    shader->release();
}

} // namespace atom::render
