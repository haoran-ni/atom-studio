#include "UnitCellRenderer.h"
#include "ShaderManager.h"
#include "../common/Camera.h"
#include "../common/RenderSettings.h"
#include "../../data/Structure.h"

#include <QOpenGLShaderProgram>
#include <QDebug>
#include <array>

namespace atom::render {

UnitCellRenderer::UnitCellRenderer()
    : m_positionBuffer(QOpenGLBuffer::VertexBuffer)
    , m_colorBuffer(QOpenGLBuffer::VertexBuffer)
    , m_indexBuffer(QOpenGLBuffer::IndexBuffer)
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

    if (!m_vao.create()) {
        qCritical() << "UnitCellRenderer: Failed to create VAO";
        return false;
    }

    if (!m_positionBuffer.create() || !m_colorBuffer.create() || !m_indexBuffer.create()) {
        qCritical() << "UnitCellRenderer: Failed to create buffers";
        return false;
    }

    m_initialized = true;
    return true;
}

void UnitCellRenderer::cleanup() {
    m_indexBuffer.destroy();
    m_colorBuffer.destroy();
    m_positionBuffer.destroy();
    m_vao.destroy();
    m_edgeCount = 0;
    m_initialized = false;
}

void UnitCellRenderer::setUnitCellData(const data::Structure* structure) {
    if (!m_initialized) return;

    if (!structure || !structure->hasLattice()) {
        m_edgeCount = 0;
        return;
    }

    const auto& lattice = structure->lattice();
    const auto& m = lattice.matrix;

    // Origin
    float ox = 0.0f, oy = 0.0f, oz = 0.0f;

    // Three lattice vectors: a = m[0], b = m[1], c = m[2]
    float ax = static_cast<float>(m[0][0]);
    float ay = static_cast<float>(m[0][1]);
    float az = static_cast<float>(m[0][2]);
    float bx = static_cast<float>(m[1][0]);
    float by = static_cast<float>(m[1][1]);
    float bz = static_cast<float>(m[1][2]);
    float cx = static_cast<float>(m[2][0]);
    float cy = static_cast<float>(m[2][1]);
    float cz = static_cast<float>(m[2][2]);

    // 8 corners of the parallelepiped
    //  0: O           = (0,0,0)
    //  1: A           = a
    //  2: B           = b
    //  3: C           = c
    //  4: A+B         = a+b
    //  5: A+C         = a+c
    //  6: B+C         = b+c
    //  7: A+B+C       = a+b+c
    std::array<float, 8 * 3> positions = {{
        ox,             oy,             oz,              // 0: O
        ax,             ay,             az,              // 1: A
        bx,             by,             bz,              // 2: B
        cx,             cy,             cz,              // 3: C
        ax + bx,        ay + by,        az + bz,         // 4: A+B
        ax + cx,        ay + cy,        az + cz,         // 5: A+C
        bx + cx,        by + cy,        bz + cz,         // 6: B+C
        ax + bx + cx,   ay + by + cy,   az + bz + cz     // 7: A+B+C
    }};

    // 12 edges of the parallelepiped (pairs of vertex indices)
    // From origin: O-A, O-B, O-C
    // From A:      A-(A+B), A-(A+C)
    // From B:      B-(A+B), B-(B+C)
    // From C:      C-(A+C), C-(B+C)
    // To far corner: (A+B)-(A+B+C), (A+C)-(A+B+C), (B+C)-(A+B+C)
    std::array<unsigned int, 12 * 2> indices = {{
        0, 1,   // O → A
        0, 2,   // O → B
        0, 3,   // O → C
        1, 4,   // A → A+B
        1, 5,   // A → A+C
        2, 4,   // B → A+B
        2, 6,   // B → B+C
        3, 5,   // C → A+C
        3, 6,   // C → B+C
        4, 7,   // A+B → A+B+C
        5, 7,   // A+C → A+B+C
        6, 7    // B+C → A+B+C
    }};

    m_edgeCount = 12;

    // All vertices share the same color (will be set at render time via uniform
    // or per-vertex; here we bake it per-vertex for shader compatibility)
    // Use white as default; actual color applied from RenderSettings at render time
    std::array<float, 8 * 4> colors;
    for (int i = 0; i < 8; ++i) {
        colors[i * 4 + 0] = 1.0f;
        colors[i * 4 + 1] = 1.0f;
        colors[i * 4 + 2] = 1.0f;
        colors[i * 4 + 3] = 1.0f;
    }

    // Upload to GPU
    m_vao.bind();

    m_positionBuffer.bind();
    m_positionBuffer.allocate(positions.data(), positions.size() * sizeof(float));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

    m_colorBuffer.bind();
    m_colorBuffer.allocate(colors.data(), colors.size() * sizeof(float));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);

    m_indexBuffer.bind();
    m_indexBuffer.allocate(indices.data(), indices.size() * sizeof(unsigned int));

    m_vao.release();
}

void UnitCellRenderer::render(const Camera& camera, const RenderSettings& settings) {
    if (!m_initialized || m_edgeCount == 0 || !settings.showUnitCell) return;

    QOpenGLShaderProgram* shader = m_shaderManager->lineShader();
    if (!shader) return;

    shader->bind();
    shader->setUniformValue("uViewProjectionMatrix", camera.viewProjectionMatrix());

    // Update vertex colors from settings
    const auto& color = settings.unitCellColor;
    float r = color.redF();
    float g = color.greenF();
    float b = color.blueF();
    float a = color.alphaF();

    std::array<float, 8 * 4> colors;
    for (int i = 0; i < 8; ++i) {
        colors[i * 4 + 0] = r;
        colors[i * 4 + 1] = g;
        colors[i * 4 + 2] = b;
        colors[i * 4 + 3] = a;
    }

    m_vao.bind();

    m_colorBuffer.bind();
    m_colorBuffer.allocate(colors.data(), colors.size() * sizeof(float));

    glLineWidth(settings.unitCellLineWidth);
    glDrawElements(GL_LINES, m_edgeCount * 2, GL_UNSIGNED_INT, nullptr);

    m_vao.release();
    shader->release();
}

} // namespace atom::render
