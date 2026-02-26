#include "ViewportAxesRenderer.h"
#include "ShaderManager.h"
#include "../common/Camera.h"
#include "../common/RenderSettings.h"

#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <QVector3D>
#include <QVector4D>
#include <QColor>
#include <QDebug>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace atom::render {

namespace {

constexpr int kAxesCylinderSegments = 32;
constexpr int kAxesConeSegments = 32;
constexpr int kCenterSphereStacks = 12;
constexpr int kCenterSphereSegments = 18;
constexpr float kOverlayDepthRange = 512.0f;

struct AxisDesc {
    QVector3D screenDir;  // x=screen right, y=screen down, z=self-depth
    QVector4D color;
};

} // namespace

ViewportAxesRenderer::ViewportAxesRenderer()
    : m_cylinderVBO(QOpenGLBuffer::VertexBuffer)
    , m_cylinderIBO(QOpenGLBuffer::IndexBuffer)
    , m_coneVBO(QOpenGLBuffer::VertexBuffer)
    , m_coneIBO(QOpenGLBuffer::IndexBuffer)
    , m_centerSphereVBO(QOpenGLBuffer::VertexBuffer)
    , m_centerSphereIBO(QOpenGLBuffer::IndexBuffer)
    , m_instanceStartBuffer(QOpenGLBuffer::VertexBuffer)
    , m_instanceEndBuffer(QOpenGLBuffer::VertexBuffer)
    , m_instanceColorBuffer(QOpenGLBuffer::VertexBuffer)
{
}

ViewportAxesRenderer::~ViewportAxesRenderer() {
    cleanup();
}

bool ViewportAxesRenderer::initialize(ShaderManager* shaderManager) {
    if (m_initialized) return true;

    initializeOpenGLFunctions();

    m_shaderManager = shaderManager;
    if (!m_shaderManager || !m_shaderManager->isInitialized()) {
        qCritical() << "ViewportAxesRenderer: ShaderManager not initialized";
        return false;
    }

    if (!m_cylinderVAO.create() || !m_coneVAO.create() || !m_centerSphereVAO.create()) {
        qCritical() << "ViewportAxesRenderer: Failed to create VAOs";
        return false;
    }

    if (!m_cylinderVBO.create() || !m_cylinderIBO.create() ||
        !m_coneVBO.create() || !m_coneIBO.create() ||
        !m_centerSphereVBO.create() || !m_centerSphereIBO.create() ||
        !m_instanceStartBuffer.create() || !m_instanceEndBuffer.create() || !m_instanceColorBuffer.create()) {
        qCritical() << "ViewportAxesRenderer: Failed to create buffers";
        return false;
    }

    createCappedCylinderGeometry(kAxesCylinderSegments);
    createCappedConeGeometry(kAxesConeSegments);
    createCenterSphereGeometry(kCenterSphereStacks, kCenterSphereSegments);

    setupMeshVAO(m_cylinderVAO, m_cylinderVBO, m_cylinderIBO);
    setupMeshVAO(m_coneVAO, m_coneVBO, m_coneIBO);
    setupMeshVAO(m_centerSphereVAO, m_centerSphereVBO, m_centerSphereIBO);

    m_initialized = true;
    return true;
}

void ViewportAxesRenderer::cleanup() {
    m_centerSphereIBO.destroy();
    m_centerSphereVBO.destroy();
    m_centerSphereVAO.destroy();

    m_instanceColorBuffer.destroy();
    m_instanceEndBuffer.destroy();
    m_instanceStartBuffer.destroy();

    m_coneIBO.destroy();
    m_coneVBO.destroy();
    m_coneVAO.destroy();

    m_cylinderIBO.destroy();
    m_cylinderVBO.destroy();
    m_cylinderVAO.destroy();

    m_cylinderIndexCount = 0;
    m_coneIndexCount = 0;
    m_centerSphereIndexCount = 0;
    m_shaderManager = nullptr;
    m_initialized = false;
}

void ViewportAxesRenderer::setupMeshVAO(QOpenGLVertexArrayObject& vao,
                                        QOpenGLBuffer& vertexBuffer,
                                        QOpenGLBuffer& indexBuffer) {
    vao.bind();

    vertexBuffer.bind();
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

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

    indexBuffer.bind();
    vao.release();
}

void ViewportAxesRenderer::createCenterSphereGeometry(int stacks, int segments) {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    vertices.reserve(static_cast<size_t>((stacks + 1) * (segments + 1)) * 3);
    indices.reserve(static_cast<size_t>(stacks * segments) * 6);

    const float pi = 3.14159265358979323846f;
    for (int stack = 0; stack <= stacks; ++stack) {
        const float v = static_cast<float>(stack) / static_cast<float>(stacks);
        const float phi = -0.5f * pi + v * pi;
        const float ringR = std::cos(phi);
        const float zUnit = std::sin(phi);   // [-1, 1]
        const float z01 = 0.5f * (zUnit + 1.0f); // [0, 1]

        for (int seg = 0; seg <= segments; ++seg) {
            const float u = static_cast<float>(seg) / static_cast<float>(segments);
            const float theta = u * 2.0f * pi;
            vertices.push_back(std::cos(theta) * ringR);
            vertices.push_back(std::sin(theta) * ringR);
            vertices.push_back(z01);
        }
    }

    const int row = segments + 1;
    for (int stack = 0; stack < stacks; ++stack) {
        for (int seg = 0; seg < segments; ++seg) {
            const unsigned int a = static_cast<unsigned int>(stack * row + seg);
            const unsigned int b = a + 1;
            const unsigned int c = static_cast<unsigned int>((stack + 1) * row + seg);
            const unsigned int d = c + 1;

            indices.push_back(a); indices.push_back(c); indices.push_back(b);
            indices.push_back(b); indices.push_back(c); indices.push_back(d);
        }
    }

    m_centerSphereIndexCount = static_cast<int>(indices.size());
    m_centerSphereVBO.bind();
    m_centerSphereVBO.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(float)));
    m_centerSphereIBO.bind();
    m_centerSphereIBO.allocate(indices.data(), static_cast<int>(indices.size() * sizeof(unsigned int)));
}

void ViewportAxesRenderer::createCappedCylinderGeometry(int segments) {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    vertices.reserve(static_cast<size_t>((segments + 1) * 2 + 2) * 3);
    indices.reserve(static_cast<size_t>(segments) * 15);

    const float pi = 3.14159265358979323846f;
    for (int i = 0; i <= segments; ++i) {
        const float angle = (2.0f * pi * i) / static_cast<float>(segments);
        const float x = std::cos(angle);
        const float y = std::sin(angle);

        vertices.push_back(x); vertices.push_back(y); vertices.push_back(0.0f);
        vertices.push_back(x); vertices.push_back(y); vertices.push_back(1.0f);
    }

    for (int i = 0; i < segments; ++i) {
        const int b0 = i * 2;
        const int t0 = i * 2 + 1;
        const int b1 = (i + 1) * 2;
        const int t1 = (i + 1) * 2 + 1;
        indices.push_back(b0); indices.push_back(b1); indices.push_back(t0);
        indices.push_back(t0); indices.push_back(b1); indices.push_back(t1);
    }

    const unsigned int bottomCenter = static_cast<unsigned int>(vertices.size() / 3);
    vertices.push_back(0.0f); vertices.push_back(0.0f); vertices.push_back(0.0f);
    const unsigned int topCenter = static_cast<unsigned int>(vertices.size() / 3);
    vertices.push_back(0.0f); vertices.push_back(0.0f); vertices.push_back(1.0f);

    for (int i = 0; i < segments; ++i) {
        const unsigned int b0 = static_cast<unsigned int>(i * 2);
        const unsigned int t0 = static_cast<unsigned int>(i * 2 + 1);
        const unsigned int b1 = static_cast<unsigned int>((i + 1) * 2);
        const unsigned int t1 = static_cast<unsigned int>((i + 1) * 2 + 1);
        // Bottom cap (z=0): single-sided; the center sphere covers the junction.
        indices.push_back(bottomCenter); indices.push_back(b1); indices.push_back(b0);
        indices.push_back(topCenter); indices.push_back(t0); indices.push_back(t1);
    }

    m_cylinderIndexCount = static_cast<int>(indices.size());
    m_cylinderVBO.bind();
    m_cylinderVBO.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(float)));
    m_cylinderIBO.bind();
    m_cylinderIBO.allocate(indices.data(), static_cast<int>(indices.size() * sizeof(unsigned int)));
}

void ViewportAxesRenderer::createCappedConeGeometry(int segments) {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    vertices.reserve(static_cast<size_t>(segments + 3) * 3);
    indices.reserve(static_cast<size_t>(segments) * 6);

    const float pi = 3.14159265358979323846f;
    for (int i = 0; i <= segments; ++i) {
        const float angle = (2.0f * pi * i) / static_cast<float>(segments);
        vertices.push_back(std::cos(angle));
        vertices.push_back(std::sin(angle));
        vertices.push_back(0.0f); // base ring, z=0
    }

    const unsigned int tipIndex = static_cast<unsigned int>(vertices.size() / 3);
    vertices.push_back(0.0f); vertices.push_back(0.0f); vertices.push_back(1.0f);
    const unsigned int baseCenterIndex = static_cast<unsigned int>(vertices.size() / 3);
    vertices.push_back(0.0f); vertices.push_back(0.0f); vertices.push_back(0.0f);

    for (int i = 0; i < segments; ++i) {
        const unsigned int r0 = static_cast<unsigned int>(i);
        const unsigned int r1 = static_cast<unsigned int>(i + 1);
        // Side faces.
        indices.push_back(r0); indices.push_back(r1); indices.push_back(tipIndex);
        // Base cap faces -Z.
        indices.push_back(baseCenterIndex); indices.push_back(r1); indices.push_back(r0);
    }

    m_coneIndexCount = static_cast<int>(indices.size());
    m_coneVBO.bind();
    m_coneVBO.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(float)));
    m_coneIBO.bind();
    m_coneIBO.allocate(indices.data(), static_cast<int>(indices.size() * sizeof(unsigned int)));
}

void ViewportAxesRenderer::render(const Camera& camera, const RenderSettings& settings,
                                  int viewportWidth, int viewportHeight) {
    if (!m_initialized || viewportWidth <= 0 || viewportHeight <= 0 || !settings.showViewportAxes) {
        return;
    }

    QOpenGLShaderProgram* axesShader = m_shaderManager ? m_shaderManager->viewportAxesShader() : nullptr;
    if (!axesShader) {
        return;
    }

    const float scale = std::max(settings.viewportAxesScale, 0.05f);
    const float dpr = std::max(settings.viewportAxesPixelRatio, 1.0f);
    const float pxScale = scale * dpr;
    const float len = 68.0f * pxScale;
    const float headLen = std::min(len * 0.45f, std::max(6.0f * pxScale, 14.0f * pxScale));
    const float shaftLen = std::max(0.0f, len - headLen);
    const float shaftRadius = std::max(3.0f * pxScale, 2.0f * dpr);
    const float headRadius = shaftRadius * 1.9f;

    const QMatrix4x4 viewMat = camera.viewMatrix();
    const std::array<AxisDesc, 3> axes = {{
        { QVector3D(viewMat(0, 0), -viewMat(1, 0), viewMat(2, 0)), QVector4D(1.0f, 68.0f / 255.0f, 68.0f / 255.0f, 1.0f) },
        { QVector3D(viewMat(0, 1), -viewMat(1, 1), viewMat(2, 1)), QVector4D(68.0f / 255.0f, 1.0f, 68.0f / 255.0f, 1.0f) },
        { QVector3D(viewMat(0, 2), -viewMat(1, 2), viewMat(2, 2)), QVector4D(68.0f / 255.0f, 68.0f / 255.0f, 1.0f, 1.0f) }
    }};

    std::array<float, 3 * 3> cylStarts{};
    std::array<float, 3 * 3> cylEnds{};
    std::array<float, 3 * 4> cylColors{};
    std::array<float, 3 * 3> coneStarts{};
    std::array<float, 3 * 3> coneEnds{};
    std::array<float, 3 * 4> coneColors{};
    int cylCount = 0;
    int coneCount = 0;

    auto writeInst3 = [](float* dst, int index, const QVector3D& v) {
        dst[index * 3 + 0] = v.x();
        dst[index * 3 + 1] = v.y();
        dst[index * 3 + 2] = v.z();
    };
    auto writeColor4 = [](float* dst, int index, const QVector4D& c) {
        dst[index * 4 + 0] = c.x();
        dst[index * 4 + 1] = c.y();
        dst[index * 4 + 2] = c.z();
        dst[index * 4 + 3] = c.w();
    };

    const QVector3D origin(settings.viewportAxesScreenX, settings.viewportAxesScreenY, 0.0f);
    for (const AxisDesc& ax : axes) {
        QVector3D dir = ax.screenDir;
        if (dir.lengthSquared() < 1e-8f) continue;
        dir.normalize();

        QVector3D shaftEnd = origin + dir * shaftLen;
        writeInst3(cylStarts.data(), cylCount, origin);
        writeInst3(cylEnds.data(), cylCount, shaftEnd);
        writeColor4(cylColors.data(), cylCount, ax.color);
        ++cylCount;

        if (headLen > 0.0f) {
            writeInst3(coneStarts.data(), coneCount, shaftEnd);
            writeInst3(coneEnds.data(), coneCount, origin + dir * len);
            writeColor4(coneColors.data(), coneCount, ax.color);
            ++coneCount;
        }
    }

    if (cylCount == 0 && coneCount == 0) {
        return;
    }

    QMatrix4x4 overlayView;
    overlayView.setToIdentity();
    QMatrix4x4 overlayProj;
    overlayProj.ortho(0.0f, static_cast<float>(viewportWidth),
                      static_cast<float>(viewportHeight), 0.0f,
                      -kOverlayDepthRange, kOverlayDepthRange);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    // Render the axes overlay as closed solid surfaces; disable culling to avoid
    // any cap/face winding artifacts across orientations.
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    axesShader->bind();
    axesShader->setUniformValue("uViewMatrix", overlayView);
    axesShader->setUniformValue("uProjectionMatrix", overlayProj);

    if (cylCount > 0) {
        m_instanceStartBuffer.bind();
        m_instanceStartBuffer.allocate(cylStarts.data(), cylCount * 3 * static_cast<int>(sizeof(float)));
        m_instanceEndBuffer.bind();
        m_instanceEndBuffer.allocate(cylEnds.data(), cylCount * 3 * static_cast<int>(sizeof(float)));
        m_instanceColorBuffer.bind();
        m_instanceColorBuffer.allocate(cylColors.data(), cylCount * 4 * static_cast<int>(sizeof(float)));

        axesShader->setUniformValue("uBondRadius", shaftRadius);
        m_cylinderVAO.bind();
        glDrawElementsInstanced(GL_TRIANGLES, m_cylinderIndexCount, GL_UNSIGNED_INT, nullptr, cylCount);
        m_cylinderVAO.release();
    }

    if (coneCount > 0) {
        m_instanceStartBuffer.bind();
        m_instanceStartBuffer.allocate(coneStarts.data(), coneCount * 3 * static_cast<int>(sizeof(float)));
        m_instanceEndBuffer.bind();
        m_instanceEndBuffer.allocate(coneEnds.data(), coneCount * 3 * static_cast<int>(sizeof(float)));
        m_instanceColorBuffer.bind();
        m_instanceColorBuffer.allocate(coneColors.data(), coneCount * 4 * static_cast<int>(sizeof(float)));

        axesShader->setUniformValue("uBondRadius", headRadius);
        m_coneVAO.bind();
        glDrawElementsInstanced(GL_TRIANGLES, m_coneIndexCount, GL_UNSIGNED_INT, nullptr, coneCount);
        m_coneVAO.release();
    }

    axesShader->release();
}

} // namespace atom::render
