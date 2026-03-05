#include "SphereRenderer.h"
#include "ShaderManager.h"
#include "../common/Camera.h"
#include "../common/RenderSettings.h"
#include "../../data/Structure.h"

#include <QOpenGLShaderProgram>
#include <QDebug>

namespace atom::render {

SphereRenderer::SphereRenderer()
    : m_quadVBO(QOpenGLBuffer::VertexBuffer)
    , m_instancePosBuffer(QOpenGLBuffer::VertexBuffer)
    , m_instanceColorBuffer(QOpenGLBuffer::VertexBuffer)
{
}

SphereRenderer::~SphereRenderer() {
    cleanup();
}

bool SphereRenderer::initialize(ShaderManager* shaderManager) {
    if (m_initialized) return true;

    initializeOpenGLFunctions();

    m_shaderManager = shaderManager;
    if (!m_shaderManager || !m_shaderManager->isInitialized()) {
        qCritical() << "SphereRenderer: ShaderManager not initialized";
        return false;
    }

    // Create VAO
    if (!m_quadVAO.create()) {
        qCritical() << "SphereRenderer: Failed to create VAO";
        return false;
    }

    // Create buffers
    if (!m_quadVBO.create() || !m_instancePosBuffer.create() ||
        !m_instanceColorBuffer.create()) {
        qCritical() << "SphereRenderer: Failed to create buffers";
        return false;
    }

    createQuadGeometry();

    m_initialized = true;
    return true;
}

void SphereRenderer::cleanup() {
    m_instanceColorBuffer.destroy();
    m_instancePosBuffer.destroy();
    m_quadVBO.destroy();
    m_quadVAO.destroy();
    m_atomCount = 0;
    m_initialized = false;
}

void SphereRenderer::createQuadGeometry() {
    // Billboard quad: two triangles forming a square from -1 to 1
    static const float quadVertices[] = {
        // Triangle 1
        -1.0f, -1.0f, 0.0f,
         1.0f, -1.0f, 0.0f,
         1.0f,  1.0f, 0.0f,
        // Triangle 2
        -1.0f, -1.0f, 0.0f,
         1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f,
    };

    m_quadVAO.bind();

    m_quadVBO.bind();
    m_quadVBO.allocate(quadVertices, sizeof(quadVertices));

    // Position attribute (location 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

    m_quadVBO.release();
    m_quadVAO.release();
}

void SphereRenderer::setAtomData(const data::Structure* structure) {
    if (!m_initialized) {
        return;
    }

    if (!structure || structure->atomCount() == 0) {
        m_atomCount = 0;
        return;
    }

    m_atomCount = structure->atomCount();

    // Pack position + radius data
    std::vector<float> posRadiusData(m_atomCount * 4);
    const float* px = structure->positionsX();
    const float* py = structure->positionsY();
    const float* pz = structure->positionsZ();
    const float* radii = structure->radii();

    for (size_t i = 0; i < m_atomCount; ++i) {
        posRadiusData[i * 4 + 0] = px[i];
        posRadiusData[i * 4 + 1] = py[i];
        posRadiusData[i * 4 + 2] = pz[i];
        posRadiusData[i * 4 + 3] = radii[i];
    }

    // Pack color data
    std::vector<float> colorData(m_atomCount * 4);
    const float* cr = structure->colorsR();
    const float* cg = structure->colorsG();
    const float* cb = structure->colorsB();
    const float* ca = structure->colorsA();

    for (size_t i = 0; i < m_atomCount; ++i) {
        colorData[i * 4 + 0] = cr[i];
        colorData[i * 4 + 1] = cg[i];
        colorData[i * 4 + 2] = cb[i];
        colorData[i * 4 + 3] = ca[i];
    }

    // Upload to GPU
    m_quadVAO.bind();

    m_instancePosBuffer.bind();
    m_instancePosBuffer.allocate(posRadiusData.data(),
                                  posRadiusData.size() * sizeof(float));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glVertexAttribDivisor(1, 1);  // Instance attribute

    m_instanceColorBuffer.bind();
    m_instanceColorBuffer.allocate(colorData.data(),
                                    colorData.size() * sizeof(float));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glVertexAttribDivisor(2, 1);  // Instance attribute

    m_instanceColorBuffer.release();
    m_quadVAO.release();
}

void SphereRenderer::render(const Camera& camera, const RenderSettings& settings) {
    if (!m_initialized || m_atomCount == 0 || !settings.showAtoms) {
        return;
    }

    QOpenGLShaderProgram* shader = m_shaderManager->sphereShader();
    if (!shader) return;

    shader->bind();

    // Set uniforms
    shader->setUniformValue("uViewMatrix", camera.viewMatrix());
    shader->setUniformValue("uProjectionMatrix", camera.projectionMatrix());

    // Light direction: world space → view space for shader
    QVector3D worldLightDir = settings.lightDirWorld();
    QVector3D viewLightDir = (camera.viewMatrix() * QVector4D(worldLightDir, 0.0f)).toVector3D().normalized();
    shader->setUniformValue("uLightDir", viewLightDir);

    shader->setUniformValue("uAtomScale", settings.atomScale);
    shader->setUniformValue("uAmbient", settings.ambientStrength);
    shader->setUniformValue("uDiffuse", settings.diffuseStrength);
    shader->setUniformValue("uSpecular", settings.specularStrength);
    shader->setUniformValue("uShininess", settings.shininess);

    // Draw instanced quads
    m_quadVAO.bind();
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(m_atomCount));
    m_quadVAO.release();

    shader->release();
}

} // namespace atom::render
