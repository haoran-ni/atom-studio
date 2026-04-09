#include "BondRenderer.h"
#include "CylinderMesh.h"
#include "ShaderManager.h"
#include "../common/BondRenderData.h"
#include "../common/Camera.h"
#include "../common/RenderSettings.h"
#include "../../data/Structure.h"

#include <QOpenGLShaderProgram>
#include <QDebug>

#include <algorithm>
namespace atom::render {

BondRenderer::BondRenderer()
    : m_cylinderVBO(QOpenGLBuffer::VertexBuffer)
    , m_cylinderIBO(QOpenGLBuffer::IndexBuffer)
    , m_instanceStartBuffer(QOpenGLBuffer::VertexBuffer)
    , m_instanceEndBuffer(QOpenGLBuffer::VertexBuffer)
    , m_instanceStartColorBuffer(QOpenGLBuffer::VertexBuffer)
    , m_instanceEndColorBuffer(QOpenGLBuffer::VertexBuffer)
    , m_instanceRadiusBuffer(QOpenGLBuffer::VertexBuffer)
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
        !m_instanceStartColorBuffer.create() || !m_instanceEndColorBuffer.create() ||
        !m_instanceRadiusBuffer.create()) {
        qCritical() << "BondRenderer: Failed to create buffers";
        return false;
    }

    ensureCylinderGeometry(20);

    m_cylinderVAO.bind();

    m_cylinderVBO.bind();
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CylinderMeshVertex), nullptr);
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(CylinderMeshVertex),
                          reinterpret_cast<const void*>(3 * sizeof(float)));

    m_instanceStartBuffer.bind();
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glVertexAttribDivisor(1, 1);

    m_instanceEndBuffer.bind();
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glVertexAttribDivisor(2, 1);

    m_instanceStartColorBuffer.bind();
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glVertexAttribDivisor(3, 1);

    m_instanceEndColorBuffer.bind();
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glVertexAttribDivisor(4, 1);

    m_instanceRadiusBuffer.bind();
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glVertexAttribDivisor(5, 1);

    m_cylinderIBO.bind();
    m_cylinderVAO.release();

    m_initialized = true;
    return true;
}

void BondRenderer::cleanup() {
    m_instanceRadiusBuffer.destroy();
    m_instanceEndColorBuffer.destroy();
    m_instanceStartColorBuffer.destroy();
    m_instanceEndBuffer.destroy();
    m_instanceStartBuffer.destroy();
    m_cylinderIBO.destroy();
    m_cylinderVBO.destroy();
    m_cylinderVAO.destroy();
    m_cylinderIndexCount = 0;
    m_meshSegments = 0;
    m_bondCount = 0;
    m_initialized = false;
}

void BondRenderer::createCylinderGeometry(int segments) {
    std::vector<CylinderMeshVertex> vertices;
    std::vector<unsigned int> indices;
    buildCappedUnitCylinderMesh(segments, vertices, indices);

    m_cylinderIndexCount = static_cast<int>(indices.size());
    m_meshSegments = segments;

    m_cylinderVBO.bind();
    m_cylinderVBO.allocate(vertices.data(),
                           static_cast<int>(vertices.size() * sizeof(CylinderMeshVertex)));

    m_cylinderIBO.bind();
    m_cylinderIBO.allocate(indices.data(),
                           static_cast<int>(indices.size() * sizeof(unsigned int)));
}

void BondRenderer::ensureCylinderGeometry(int segments) {
    const int clampedSegments = std::max(segments, 3);
    if (m_cylinderIndexCount > 0 && m_meshSegments == clampedSegments) {
        return;
    }
    createCylinderGeometry(clampedSegments);
}

void BondRenderer::setBondData(const data::Structure* structure) {
    if (!m_initialized) return;

    if (!structure || structure->bonds().bondCount() == 0) {
        m_bondCount = 0;
        return;
    }

    const auto& bonds = structure->bonds();
    m_bondCount = bonds.bondCount();

    const PackedBondRenderData packed = packBondRenderData(structure, BondPositionPacking::XYZ3);
    std::vector<float> radiusData(m_bondCount * 2);
    for (size_t i = 0; i < m_bondCount; ++i) {
        radiusData[i * 2 + 0] = packed.startRadii[i];
        radiusData[i * 2 + 1] = packed.endRadii[i];
    }

    // Upload to GPU
    m_cylinderVAO.bind();

    m_instanceStartBuffer.bind();
    m_instanceStartBuffer.allocate(packed.startPositions.data(),
                                   static_cast<int>(packed.startPositions.size() * sizeof(float)));

    m_instanceEndBuffer.bind();
    m_instanceEndBuffer.allocate(packed.endPositions.data(),
                                 static_cast<int>(packed.endPositions.size() * sizeof(float)));

    m_instanceStartColorBuffer.bind();
    m_instanceStartColorBuffer.allocate(packed.startColors.data(),
                                        static_cast<int>(packed.startColors.size() * sizeof(float)));

    m_instanceEndColorBuffer.bind();
    m_instanceEndColorBuffer.allocate(packed.endColors.data(),
                                      static_cast<int>(packed.endColors.size() * sizeof(float)));

    m_instanceRadiusBuffer.bind();
    m_instanceRadiusBuffer.allocate(radiusData.data(),
                                    static_cast<int>(radiusData.size() * sizeof(float)));

    m_instanceRadiusBuffer.release();
    m_cylinderVAO.release();
}

void BondRenderer::render(const Camera& camera, const RenderSettings& settings) {
    if (!m_initialized || m_bondCount == 0 || !settings.showBonds) return;

    ensureCylinderGeometry(settings.cylinderSegments);
    if (m_cylinderIndexCount == 0) return;

    QOpenGLShaderProgram* shader = m_shaderManager->bondShader();
    if (!shader) return;

    shader->bind();

    shader->setUniformValue("uViewMatrix", camera.viewMatrix());
    shader->setUniformValue("uProjectionMatrix", camera.projectionMatrix());
    shader->setUniformValue("uBondRadius", settings.bondRadius);
    shader->setUniformValue("uAtomScale", settings.atomScale);
    shader->setUniformValue("uIsPerspective", camera.isPerspective() ? 1 : 0);

    // Light direction: world space → view space for shader
    QVector3D worldLightDir = settings.lightDirWorld();
    QVector3D viewLightDir = (camera.viewMatrix() * QVector4D(worldLightDir, 0.0f)).toVector3D().normalized();
    shader->setUniformValue("uLightDir", viewLightDir);

    shader->setUniformValue("uAmbient", settings.ambientStrength);
    shader->setUniformValue("uDiffuse", settings.diffuseStrength);
    shader->setUniformValue("uSpecular", settings.specularStrength);
    shader->setUniformValue("uShininess", settings.shininess);

    m_cylinderVAO.bind();
    glDrawElementsInstanced(GL_TRIANGLES,
                            m_cylinderIndexCount,
                            GL_UNSIGNED_INT,
                            nullptr,
                            static_cast<GLsizei>(m_bondCount));
    m_cylinderVAO.release();

    shader->release();
}

} // namespace atom::render
