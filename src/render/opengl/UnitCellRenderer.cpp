#include "UnitCellRenderer.h"
#include "CylinderMesh.h"
#include "ShaderManager.h"
#include "../common/Camera.h"
#include "../common/RenderSettings.h"
#include "../../data/Structure.h"

#include <QOpenGLShaderProgram>
#include <QDebug>
#include <QVector3D>

#include <algorithm>
#include <array>
#include <vector>

namespace atom::render {

UnitCellRenderer::UnitCellRenderer()
    : m_cylinderVBO(QOpenGLBuffer::VertexBuffer)
    , m_cylinderIBO(QOpenGLBuffer::IndexBuffer)
    , m_edgeStartBuffer(QOpenGLBuffer::VertexBuffer)
    , m_edgeEndBuffer(QOpenGLBuffer::VertexBuffer)
    , m_edgeColorBuffer(QOpenGLBuffer::VertexBuffer)
    , m_edgeRadiusBuffer(QOpenGLBuffer::VertexBuffer)
    , m_jointQuadVBO(QOpenGLBuffer::VertexBuffer)
    , m_jointPosRadiusBuffer(QOpenGLBuffer::VertexBuffer)
    , m_jointColorBuffer(QOpenGLBuffer::VertexBuffer)
{
}

UnitCellRenderer::~UnitCellRenderer() {
    cleanup();
}

bool UnitCellRenderer::initialize(ShaderManager* shaderManager) {
    if (m_initialized) return true;

    initializeOpenGLFunctions();

    m_shaderManager = shaderManager;
    if (!m_shaderManager || !m_shaderManager->isInitialized()) {
        qCritical() << "UnitCellRenderer: ShaderManager not initialized";
        return false;
    }

    if (!m_cylinderVAO.create() || !m_jointVAO.create()) {
        qCritical() << "UnitCellRenderer: Failed to create VAOs";
        return false;
    }

    if (!m_cylinderVBO.create() || !m_cylinderIBO.create() ||
        !m_edgeStartBuffer.create() || !m_edgeEndBuffer.create() ||
        !m_edgeColorBuffer.create() || !m_edgeRadiusBuffer.create() ||
        !m_jointQuadVBO.create() || !m_jointPosRadiusBuffer.create() || !m_jointColorBuffer.create()) {
        qCritical() << "UnitCellRenderer: Failed to create buffers";
        return false;
    }

    createCylinderGeometry(48);
    createJointBillboardGeometry();

    // Cylinder-edge VAO layout:
    // location 0: cylinder vertex position (vec3)
    // location 1: edge start (vec3), instanced
    // location 2: edge end (vec3), instanced
    // location 3: edge start color (vec4), instanced
    // location 4: edge end color (vec4), instanced
    // location 5: edge radii (vec2), instanced
    // location 6: local normal (vec3)
    m_cylinderVAO.bind();

    m_cylinderVBO.bind();
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CylinderMeshVertex), nullptr);
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(CylinderMeshVertex),
                          reinterpret_cast<const void*>(3 * sizeof(float)));

    m_edgeStartBuffer.bind();
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glVertexAttribDivisor(1, 1);

    m_edgeEndBuffer.bind();
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glVertexAttribDivisor(2, 1);

    m_edgeColorBuffer.bind();
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glVertexAttribDivisor(3, 1);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glVertexAttribDivisor(4, 1);

    m_edgeRadiusBuffer.bind();
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glVertexAttribDivisor(5, 1);

    m_cylinderIBO.bind();
    m_cylinderVAO.release();

    // Corner-joint VAO layout (sphere impostor instances):
    // location 0: billboard quad vertex (vec3)
    // location 1: center + base radius (vec4), instanced
    // location 2: color (vec4), instanced
    m_jointVAO.bind();

    m_jointQuadVBO.bind();
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

    m_jointPosRadiusBuffer.bind();
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glVertexAttribDivisor(1, 1);

    m_jointColorBuffer.bind();
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glVertexAttribDivisor(2, 1);

    m_jointVAO.release();

    m_initialized = true;
    return true;
}

void UnitCellRenderer::cleanup() {
    m_jointColorBuffer.destroy();
    m_jointPosRadiusBuffer.destroy();
    m_jointQuadVBO.destroy();
    m_jointVAO.destroy();

    m_edgeColorBuffer.destroy();
    m_edgeRadiusBuffer.destroy();
    m_edgeEndBuffer.destroy();
    m_edgeStartBuffer.destroy();
    m_cylinderIBO.destroy();
    m_cylinderVBO.destroy();
    m_cylinderVAO.destroy();

    m_edgeCount = 0;
    m_jointCount = 0;
    m_cylinderIndexCount = 0;
    m_initialized = false;
}

void UnitCellRenderer::createCylinderGeometry(int segments) {
    std::vector<CylinderMeshVertex> vertices;
    std::vector<unsigned int> indices;
    buildCappedUnitCylinderMesh(segments, vertices, indices);

    m_cylinderIndexCount = static_cast<int>(indices.size());

    m_cylinderVBO.bind();
    m_cylinderVBO.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(CylinderMeshVertex)));

    m_cylinderIBO.bind();
    m_cylinderIBO.allocate(indices.data(), static_cast<int>(indices.size() * sizeof(unsigned int)));
}

void UnitCellRenderer::createJointBillboardGeometry() {
    static const float quadVertices[] = {
        -1.0f, -1.0f, 0.0f,
         1.0f, -1.0f, 0.0f,
         1.0f,  1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f,
         1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f,
    };

    m_jointQuadVBO.bind();
    m_jointQuadVBO.allocate(quadVertices, sizeof(quadVertices));
}

void UnitCellRenderer::setUnitCellData(const data::Structure* structure) {
    if (!m_initialized) return;

    if (!structure || !structure->hasLattice()) {
        m_edgeCount = 0;
        m_jointCount = 0;
        return;
    }

    const auto& lattice = structure->lattice();
    const auto& m = lattice.matrix;

    const float ax = static_cast<float>(m[0][0]);
    const float ay = static_cast<float>(m[0][1]);
    const float az = static_cast<float>(m[0][2]);
    const float bx = static_cast<float>(m[1][0]);
    const float by = static_cast<float>(m[1][1]);
    const float bz = static_cast<float>(m[1][2]);
    const float cx = static_cast<float>(m[2][0]);
    const float cy = static_cast<float>(m[2][1]);
    const float cz = static_cast<float>(m[2][2]);

    const std::array<std::array<float, 3>, 8> corners = {{
        {{0.0f, 0.0f, 0.0f}},
        {{ax, ay, az}},
        {{bx, by, bz}},
        {{cx, cy, cz}},
        {{ax + bx, ay + by, az + bz}},
        {{ax + cx, ay + cy, az + cz}},
        {{bx + cx, by + cy, bz + cz}},
        {{ax + bx + cx, ay + by + cy, az + bz + cz}},
    }};

    const std::array<unsigned int, 24> edgeIndices = {{
        0, 1,   0, 2,   0, 3,
        1, 4,   1, 5,
        2, 4,   2, 6,
        3, 5,   3, 6,
        4, 7,   5, 7,   6, 7
    }};

    m_edgeCount = 12;
    for (int i = 0; i < m_edgeCount; ++i) {
        const auto& s = corners[edgeIndices[i * 2 + 0]];
        const auto& e = corners[edgeIndices[i * 2 + 1]];

        m_edgeStarts[i * 3 + 0] = s[0];
        m_edgeStarts[i * 3 + 1] = s[1];
        m_edgeStarts[i * 3 + 2] = s[2];

        m_edgeEnds[i * 3 + 0] = e[0];
        m_edgeEnds[i * 3 + 1] = e[1];
        m_edgeEnds[i * 3 + 2] = e[2];
    }

    m_jointCount = 8;
    for (int i = 0; i < m_jointCount; ++i) {
        m_jointPosRadius[i * 4 + 0] = corners[i][0];
        m_jointPosRadius[i * 4 + 1] = corners[i][1];
        m_jointPosRadius[i * 4 + 2] = corners[i][2];
        m_jointPosRadius[i * 4 + 3] = 1.0f; // Scaled by uAtomScale at render time.
    }

    std::array<float, 12 * 4> edgeColors;
    std::array<float, 12 * 2> edgeRadii;
    for (int i = 0; i < m_edgeCount; ++i) {
        edgeColors[i * 4 + 0] = 1.0f;
        edgeColors[i * 4 + 1] = 1.0f;
        edgeColors[i * 4 + 2] = 1.0f;
        edgeColors[i * 4 + 3] = 1.0f;
        edgeRadii[i * 2 + 0] = 1.0f;
        edgeRadii[i * 2 + 1] = 1.0f;
    }

    std::array<float, 8 * 4> jointColors;
    for (int i = 0; i < m_jointCount; ++i) {
        jointColors[i * 4 + 0] = 1.0f;
        jointColors[i * 4 + 1] = 1.0f;
        jointColors[i * 4 + 2] = 1.0f;
        jointColors[i * 4 + 3] = 1.0f;
    }

    m_cylinderVAO.bind();
    m_edgeStartBuffer.bind();
    m_edgeStartBuffer.allocate(m_edgeStarts.data(), m_edgeCount * 3 * static_cast<int>(sizeof(float)));
    m_edgeEndBuffer.bind();
    m_edgeEndBuffer.allocate(m_edgeEnds.data(), m_edgeCount * 3 * static_cast<int>(sizeof(float)));
    m_edgeColorBuffer.bind();
    m_edgeColorBuffer.allocate(edgeColors.data(), m_edgeCount * 4 * static_cast<int>(sizeof(float)));
    m_edgeRadiusBuffer.bind();
    m_edgeRadiusBuffer.allocate(edgeRadii.data(), m_edgeCount * 2 * static_cast<int>(sizeof(float)));
    m_cylinderVAO.release();

    m_jointVAO.bind();
    m_jointPosRadiusBuffer.bind();
    m_jointPosRadiusBuffer.allocate(m_jointPosRadius.data(), m_jointCount * 4 * static_cast<int>(sizeof(float)));
    m_jointColorBuffer.bind();
    m_jointColorBuffer.allocate(jointColors.data(), m_jointCount * 4 * static_cast<int>(sizeof(float)));
    m_jointVAO.release();
}

void UnitCellRenderer::render(const Camera& camera, const RenderSettings& settings) {
    if (!m_initialized || m_edgeCount == 0 || m_jointCount == 0 || !settings.showUnitCell) return;

    const float radius = std::max(settings.unitCellThickness, 0.001f);
    const QColor color = settings.unitCellColor;
    const float r = color.redF();
    const float g = color.greenF();
    const float b = color.blueF();

    std::array<float, 12 * 4> edgeColors;
    for (int i = 0; i < m_edgeCount; ++i) {
        edgeColors[i * 4 + 0] = r;
        edgeColors[i * 4 + 1] = g;
        edgeColors[i * 4 + 2] = b;
        edgeColors[i * 4 + 3] = 1.0f;
    }

    std::array<float, 8 * 4> jointColors;
    for (int i = 0; i < m_jointCount; ++i) {
        jointColors[i * 4 + 0] = r;
        jointColors[i * 4 + 1] = g;
        jointColors[i * 4 + 2] = b;
        jointColors[i * 4 + 3] = 1.0f;
    }

    // Light direction: world space → view space for shader
    QVector3D worldLightDir = settings.lightDirWorld();
    QVector3D lightDir = (camera.viewMatrix() * QVector4D(worldLightDir, 0.0f)).toVector3D().normalized();

    // Draw edge cylinders.
    if (QOpenGLShaderProgram* bondShader = m_shaderManager->bondShader()) {
        bondShader->bind();
        bondShader->setUniformValue("uViewMatrix", camera.viewMatrix());
        bondShader->setUniformValue("uProjectionMatrix", camera.projectionMatrix());
        bondShader->setUniformValue("uBondRadius", radius);
        bondShader->setUniformValue("uLightDir", lightDir);

        // Flat, unlit color for unit-cell object.
        bondShader->setUniformValue("uAmbient", 1.0f);
        bondShader->setUniformValue("uDiffuse", 0.0f);
        bondShader->setUniformValue("uSpecular", 0.0f);
        bondShader->setUniformValue("uShininess", 1.0f);

        m_cylinderVAO.bind();
        m_edgeColorBuffer.bind();
        m_edgeColorBuffer.allocate(edgeColors.data(), m_edgeCount * 4 * static_cast<int>(sizeof(float)));

        glDrawElementsInstanced(GL_TRIANGLES,
                                m_cylinderIndexCount,
                                GL_UNSIGNED_INT,
                                nullptr,
                                m_edgeCount);
        m_cylinderVAO.release();
        bondShader->release();
    }

    // Draw corner joints as sphere impostors so edge intersections are smooth.
    if (QOpenGLShaderProgram* sphereShader = m_shaderManager->sphereShader()) {
        sphereShader->bind();
        sphereShader->setUniformValue("uViewMatrix", camera.viewMatrix());
        sphereShader->setUniformValue("uProjectionMatrix", camera.projectionMatrix());
        sphereShader->setUniformValue("uAtomScale", radius);
        sphereShader->setUniformValue("uIsPerspective", camera.isPerspective() ? 1 : 0);
        sphereShader->setUniformValue("uLightDir", lightDir);

        // Flat, unlit color for unit-cell object.
        sphereShader->setUniformValue("uAmbient", 1.0f);
        sphereShader->setUniformValue("uDiffuse", 0.0f);
        sphereShader->setUniformValue("uSpecular", 0.0f);
        sphereShader->setUniformValue("uShininess", 1.0f);

        m_jointVAO.bind();
        m_jointColorBuffer.bind();
        m_jointColorBuffer.allocate(jointColors.data(), m_jointCount * 4 * static_cast<int>(sizeof(float)));

        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, m_jointCount);
        m_jointVAO.release();
        sphereShader->release();
    }
}

} // namespace atom::render
