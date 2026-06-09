#include "OpenGLRenderer.h"
#include "../common/Camera.h"
#include "../../data/Structure.h"

#include <QDebug>

namespace atom::render {

OpenGLRenderer::OpenGLRenderer() = default;

OpenGLRenderer::~OpenGLRenderer() {
    cleanup();
}

bool OpenGLRenderer::initialize() {
    if (m_initialized) return true;

    initializeOpenGLFunctions();

    // Initialize shader manager
    if (!m_shaderManager.initialize()) {
        qCritical() << "OpenGLRenderer: Failed to initialize shaders";
        return false;
    }

    // Initialize renderers
    if (!m_sphereRenderer.initialize(&m_shaderManager)) {
        qCritical() << "OpenGLRenderer: Failed to initialize sphere renderer";
        return false;
    }

    if (!m_bondRenderer.initialize(&m_shaderManager)) {
        qCritical() << "OpenGLRenderer: Failed to initialize bond renderer";
        return false;
    }

    if (!m_unitCellRenderer.initialize(&m_shaderManager)) {
        qCritical() << "OpenGLRenderer: Failed to initialize unit cell renderer";
        return false;
    }

    if (!m_gizmoRenderer.initialize(&m_shaderManager)) {
        qCritical() << "OpenGLRenderer: Failed to initialize gizmo renderer";
        return false;
    }

    if (!m_viewportAxesRenderer.initialize(&m_shaderManager)) {
        qCritical() << "OpenGLRenderer: Failed to initialize viewport axes renderer";
        return false;
    }

    // Set up OpenGL state
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_initialized = true;
    qInfo() << "OpenGLRenderer initialized successfully";
    return true;
}

void OpenGLRenderer::cleanup() {
    m_viewportAxesRenderer.cleanup();
    m_gizmoRenderer.cleanup();
    m_unitCellRenderer.cleanup();
    m_bondRenderer.cleanup();
    m_sphereRenderer.cleanup();
    m_shaderManager.cleanup();
    m_structure = nullptr;
    m_initialized = false;
}

void OpenGLRenderer::resize(int width, int height) {
    m_width = width;
    m_height = height;
    glViewport(0, 0, width, height);
}

void OpenGLRenderer::setStructure(const data::Structure* structure) {
    m_structure = structure;
    m_atomDataDirty = true;
    m_bondDataDirty = true;
    m_unitCellDataDirty = true;
}

void OpenGLRenderer::invalidateAtomData() {
    m_atomDataDirty = true;
}

void OpenGLRenderer::invalidateBondData() {
    m_bondDataDirty = true;
}

void OpenGLRenderer::renderBackground(const RenderSettings& settings) {
    const auto& bg = settings.backgroundColor;
    glClearColor(bg.redF() * bg.alphaF(), bg.greenF() * bg.alphaF(), bg.blueF() * bg.alphaF(), bg.alphaF());
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGLRenderer::render(const Camera& camera, const RenderSettings& settings) {
    if (!m_initialized) return;

    // Update GPU data if needed
    if (m_atomDataDirty) {
        m_sphereRenderer.setAtomData(m_structure);
        m_atomDataDirty = false;
    }

    if (m_bondDataDirty) {
        m_bondRenderer.setBondData(m_structure);
        m_bondDataDirty = false;
    }

    if (m_unitCellDataDirty) {
        m_unitCellRenderer.setUnitCellData(m_structure);
        m_unitCellDataDirty = false;
    }

    // Clear
    renderBackground(settings);

    // Enable depth testing
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Render unit-cell object (thick wireframe cuboid built from lattice)
    m_unitCellRenderer.render(camera, settings);

    // Render bonds
    m_bondRenderer.render(camera, settings);

    // Render atoms
    m_sphereRenderer.render(camera, settings);

    // Overlay pass: clear scene depth so overlays stay on top while preserving
    // correct self-occlusion inside overlay geometry.
    if (settings.showRotationCenter || settings.showViewportAxes) {
        glClear(GL_DEPTH_BUFFER_BIT);
        if (settings.showRotationCenter) {
            const float len = camera.viewScale() * 0.03f;
            m_gizmoRenderer.render(camera, settings,
                                   settings.rotationCenterX,
                                   settings.rotationCenterY,
                                   settings.rotationCenterZ,
                                   len);
        }
    }

    if (settings.showViewportAxes) {
        m_viewportAxesRenderer.render(camera, settings, m_width, m_height);
    }
}

} // namespace atom::render
