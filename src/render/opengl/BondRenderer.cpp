#include "BondRenderer.h"
#include "ShaderManager.h"
#include "../common/Camera.h"
#include "../common/RenderSettings.h"
#include "../../data/Structure.h"
#include "../../data/BondList.h"

#include <QOpenGLShaderProgram>
#include <QDebug>
#include <cmath>

namespace atom::render {

BondRenderer::BondRenderer()
    : m_cylinderVBO(QOpenGLBuffer::VertexBuffer)
    , m_cylinderIBO(QOpenGLBuffer::IndexBuffer)
    , m_instanceStartBuffer(QOpenGLBuffer::VertexBuffer)
    , m_instanceEndBuffer(QOpenGLBuffer::VertexBuffer)
    , m_instanceColorBuffer(QOpenGLBuffer::VertexBuffer)
{
}

BondRenderer::~BondRenderer() {
    cleanup();
}

bool BondRenderer::initialize(ShaderManager* shaderManager) {
    if (m_initialized) return true;

    initializeOpenGLFunctions();

    m_shaderManager = shaderManager;
    if (!m_shaderManager || !m_shaderManager->isInitialized()) {
        qCritical() << "BondRenderer: ShaderManager not initialized";
        return false;
    }

    if (!m_cylinderVAO.create()) {
        qCritical() << "BondRenderer: Failed to create VAO";
        return false;
    }

    if (!m_cylinderVBO.create() || !m_cylinderIBO.create() ||
        !m_instanceStartBuffer.create() || !m_instanceEndBuffer.create() ||
        !m_instanceColorBuffer.create()) {
        qCritical() << "BondRenderer: Failed to create buffers";
        return false;
    }

    createCylinderGeometry(12);

    m_initialized = true;
    return true;
}

void BondRenderer::cleanup() {
    m_instanceColorBuffer.destroy();
    m_instanceEndBuffer.destroy();
    m_instanceStartBuffer.destroy();
    m_cylinderIBO.destroy();
    m_cylinderVBO.destroy();
    m_cylinderVAO.destroy();
    m_bondCount = 0;
    m_initialized = false;
}

void BondRenderer::createCylinderGeometry(int segments) {
    // Create a unit cylinder along Z axis from 0 to 1
    // Vertices are on unit circle in XY plane
    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    const float PI = 3.14159265358979323846f;

    // Generate vertices for caps and sides
    for (int i = 0; i <= segments; ++i) {
        float angle = (2.0f * PI * i) / segments;
        float x = std::cos(angle);
        float y = std::sin(angle);

        // Bottom ring (z = 0)
        vertices.push_back(x);
        vertices.push_back(y);
        vertices.push_back(0.0f);

        // Top ring (z = 1)
        vertices.push_back(x);
        vertices.push_back(y);
        vertices.push_back(1.0f);
    }

    m_cylinderVertexCount = (segments + 1) * 2;

    // Generate indices for side triangles
    for (int i = 0; i < segments; ++i) {
        int b0 = i * 2;
        int t0 = i * 2 + 1;
        int b1 = (i + 1) * 2;
        int t1 = (i + 1) * 2 + 1;

        // Two triangles per segment
        indices.push_back(b0);
        indices.push_back(b1);
        indices.push_back(t0);

        indices.push_back(t0);
        indices.push_back(b1);
        indices.push_back(t1);
    }

    m_cylinderIndexCount = indices.size();

    // Upload to GPU
    m_cylinderVAO.bind();

    m_cylinderVBO.bind();
    m_cylinderVBO.allocate(vertices.data(), vertices.size() * sizeof(float));

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

    m_cylinderIBO.bind();
    m_cylinderIBO.allocate(indices.data(), indices.size() * sizeof(unsigned int));

    m_cylinderVAO.release();
}

void BondRenderer::setBondData(const data::Structure* structure) {
    if (!m_initialized) return;

    if (!structure || structure->bonds().bondCount() == 0) {
        m_bondCount = 0;
        return;
    }

    const auto& bonds = structure->bonds();
    m_bondCount = bonds.bondCount();

    std::vector<float> startData(m_bondCount * 3);
    std::vector<float> endData(m_bondCount * 3);
    std::vector<float> colorData(m_bondCount * 4);

    const float* px = structure->positionsX();
    const float* py = structure->positionsY();
    const float* pz = structure->positionsZ();
    const float* cr = structure->colorsR();
    const float* cg = structure->colorsG();
    const float* cb = structure->colorsB();

    for (size_t i = 0; i < m_bondCount; ++i) {
        const auto& bond = bonds.bond(i);
        uint32_t a1 = bond.atomIndex1;
        uint32_t a2 = bond.atomIndex2;

        startData[i * 3 + 0] = px[a1];
        startData[i * 3 + 1] = py[a1];
        startData[i * 3 + 2] = pz[a1];

        endData[i * 3 + 0] = px[a2];
        endData[i * 3 + 1] = py[a2];
        endData[i * 3 + 2] = pz[a2];

        // Average color of two atoms
        colorData[i * 4 + 0] = (cr[a1] + cr[a2]) * 0.5f;
        colorData[i * 4 + 1] = (cg[a1] + cg[a2]) * 0.5f;
        colorData[i * 4 + 2] = (cb[a1] + cb[a2]) * 0.5f;
        colorData[i * 4 + 3] = 1.0f;
    }

    // Upload to GPU
    m_cylinderVAO.bind();

    m_instanceStartBuffer.bind();
    m_instanceStartBuffer.allocate(startData.data(), startData.size() * sizeof(float));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glVertexAttribDivisor(1, 1);

    m_instanceEndBuffer.bind();
    m_instanceEndBuffer.allocate(endData.data(), endData.size() * sizeof(float));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glVertexAttribDivisor(2, 1);

    m_instanceColorBuffer.bind();
    m_instanceColorBuffer.allocate(colorData.data(), colorData.size() * sizeof(float));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glVertexAttribDivisor(3, 1);

    m_instanceColorBuffer.release();
    m_cylinderVAO.release();
}

void BondRenderer::render(const Camera& camera, const RenderSettings& settings) {
    if (!m_initialized || m_bondCount == 0 || !settings.showBonds) return;

    QOpenGLShaderProgram* shader = m_shaderManager->bondShader();
    if (!shader) return;

    shader->bind();

    shader->setUniformValue("uViewMatrix", camera.viewMatrix());
    shader->setUniformValue("uProjectionMatrix", camera.projectionMatrix());
    shader->setUniformValue("uBondRadius", settings.bondRadius);

    QVector3D lightDir(settings.lightDirX, settings.lightDirY, settings.lightDirZ);
    lightDir.normalize();
    shader->setUniformValue("uLightDir", lightDir);

    shader->setUniformValue("uAmbient", settings.ambientStrength);
    shader->setUniformValue("uDiffuse", settings.diffuseStrength);
    shader->setUniformValue("uSpecular", settings.specularStrength);
    shader->setUniformValue("uShininess", settings.shininess);

    m_cylinderVAO.bind();
    glDrawElementsInstanced(GL_TRIANGLES, m_cylinderIndexCount, GL_UNSIGNED_INT,
                            nullptr, static_cast<GLsizei>(m_bondCount));
    m_cylinderVAO.release();

    shader->release();
}

} // namespace atom::render
