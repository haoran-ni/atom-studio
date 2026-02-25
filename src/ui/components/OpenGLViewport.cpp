#include "OpenGLViewport.h"
#include "StructureModel.h"
#include "../../render/common/Camera.h"
#include "../../render/opengl/OpenGLRenderer.h"
#include "../../render/opengl/RayTracingRenderer.h"
#include "../../data/Structure.h"
#include "../../data/BondList.h"
#include "../../data/NeighborList.h"

#include <QQuickWindow>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QNativeGestureEvent>
#include <QDateTime>
#include <QTimer>
#include <QOpenGLFramebufferObjectFormat>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QDebug>
#include <algorithm>

namespace atom::ui {

/**
 * @brief Renderer implementation for QQuickFramebufferObject
 *
 * Holds both rasterization and ray tracing renderers.
 * Raster renderer is initialized eagerly; RT renderer lazily on first use.
 */
class OpenGLViewport::RendererImpl : public QQuickFramebufferObject::Renderer {
public:
    RendererImpl(OpenGLViewport* viewport)
        : m_viewport(viewport)
        , m_rasterRenderer(std::make_unique<render::OpenGLRenderer>())
    {
    }

    void render() override {
        if (!m_rasterInitialized) {
            return;
        }

        m_activeRenderer->render(*m_viewport->m_camera, m_viewport->m_renderSettings);

        // For RT mode: update sample count and keep rendering until converged
        if (m_currentMode == 1 && m_rtRenderer) {
            m_viewport->m_sampleCount = m_rtRenderer->sampleCount();
            emit m_viewport->sampleCountChanged();

            if (!m_rtRenderer->isConverged()) {
                update();  // Request next frame for progressive refinement
            }
        } else {
            update();  // Raster mode: always redraw
        }
    }

    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);

        // RT mode doesn't need MSAA (progressive accumulation handles AA)
        if (m_currentMode != 1) {
            format.setSamples(4);
        }

        return new QOpenGLFramebufferObject(size, format);
    }

    void synchronize(QQuickFramebufferObject* item) override {
        auto* viewport = static_cast<OpenGLViewport*>(item);

        // Initialize raster renderer if not done yet
        if (!m_rasterInitialized) {
            if (!m_rasterRenderer->initialize()) {
                qCritical() << "Failed to initialize OpenGL raster renderer";
                return;
            }
            m_rasterInitialized = true;
            m_activeRenderer = m_rasterRenderer.get();
        }

        // Handle renderer mode switch
        int requestedMode = viewport->m_rendererMode;
        if (requestedMode != m_currentMode) {
            m_currentMode = requestedMode;

            if (m_currentMode == 1) {
                // Switch to ray tracing
                if (!m_rtRenderer) {
                    m_rtRenderer = std::make_unique<render::RayTracingRenderer>();
                    if (!m_rtRenderer->initialize()) {
                        qCritical() << "Failed to initialize ray tracing renderer";
                        m_currentMode = 0;
                        viewport->m_rendererMode = 0;
                        emit viewport->rendererModeChanged();
                    }
                }
                if (m_rtRenderer) {
                    m_activeRenderer = m_rtRenderer.get();
                    // Upload current data to RT renderer (use physical pixels)
                    QSize size = viewport->size().toSize();
                    qreal switchDpr = viewport->window() ? viewport->window()->devicePixelRatio() : 1.0;
                    int pw = static_cast<int>(size.width() * switchDpr);
                    int ph = static_cast<int>(size.height() * switchDpr);
                    if (pw > 0 && ph > 0) {
                        m_rtRenderer->resize(pw, ph);
                    }
                    if (viewport->m_structure) {
                        m_rtRenderer->setStructure(viewport->m_structure.get());
                    }
                }
            } else {
                // Switch to rasterization
                m_activeRenderer = m_rasterRenderer.get();
                viewport->m_sampleCount = 0;
                emit viewport->sampleCountChanged();
            }

            // Force FBO recreation (MSAA on/off change)
            invalidateFramebufferObject();
        }

        // Update viewport size (use physical pixels for rendering)
        QSize logicalSize = viewport->size().toSize();
        qreal dpr = viewport->window() ? viewport->window()->devicePixelRatio() : 1.0;
        int pixelWidth = static_cast<int>(logicalSize.width() * dpr);
        int pixelHeight = static_cast<int>(logicalSize.height() * dpr);
        if (pixelWidth > 0 && pixelHeight > 0) {
            m_activeRenderer->resize(pixelWidth, pixelHeight);
            viewport->m_camera->setAspectRatio(
                static_cast<float>(logicalSize.width()) / logicalSize.height());
        }

        // Update structure if needed
        if (viewport->m_needsStructureUpdate) {
            m_activeRenderer->setStructure(viewport->m_structure.get());
            // Also update the other renderer so it's ready for instant switching
            if (m_currentMode == 0 && m_rtRenderer) {
                m_rtRenderer->setStructure(viewport->m_structure.get());
            } else if (m_currentMode == 1) {
                m_rasterRenderer->setStructure(viewport->m_structure.get());
            }
            viewport->m_needsStructureUpdate = false;
        }

        // Build render settings from viewport properties
        viewport->m_renderSettings.backgroundColor = viewport->m_backgroundColor;
        viewport->m_renderSettings.showBonds = viewport->m_showBonds;
        viewport->m_renderSettings.showUnitCell = viewport->m_showUnitCell;
        viewport->m_renderSettings.unitCellThickness = viewport->m_unitCellThickness;
        viewport->m_renderSettings.unitCellColor = viewport->m_unitCellColor;
        viewport->m_renderSettings.atomScale = viewport->m_atomScale;
        viewport->m_renderSettings.maxRTSamples = viewport->m_maxRTSamples;
        viewport->m_renderSettings.enableAmbientOcclusion = viewport->m_enableAO;
        viewport->m_renderSettings.enableShadows = viewport->m_enableShadows;
        viewport->m_renderSettings.shadowOpacity = viewport->m_shadowOpacity;
        viewport->m_renderSettings.aoSamples = viewport->m_aoSamples;
        viewport->m_renderSettings.aoRadius = viewport->m_aoRadius;
        viewport->m_renderSettings.ambientStrength = viewport->m_ambientStrength;
        viewport->m_renderSettings.diffuseStrength = viewport->m_diffuseStrength;
        viewport->m_renderSettings.specularStrength = viewport->m_specularStrength;
        viewport->m_renderSettings.shininess = viewport->m_shininess;
        viewport->m_renderSettings.lightDirX = viewport->m_lightDirX;
        viewport->m_renderSettings.lightDirY = viewport->m_lightDirY;
        viewport->m_renderSettings.lightDirZ = viewport->m_lightDirZ;

        // Update FPS
        qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
        viewport->m_frameCount++;
        if (currentTime - viewport->m_lastFrameTime >= 1000) {
            viewport->m_fps = viewport->m_frameCount * 1000.0f /
                             (currentTime - viewport->m_lastFrameTime);
            viewport->m_lastFrameTime = currentTime;
            viewport->m_frameCount = 0;
            emit viewport->fpsChanged();
        }
    }

private:
    OpenGLViewport* m_viewport;
    std::unique_ptr<render::OpenGLRenderer> m_rasterRenderer;
    std::unique_ptr<render::RayTracingRenderer> m_rtRenderer;
    render::Renderer* m_activeRenderer = nullptr;
    int m_currentMode = 0;  // 0 = Raster, 1 = RayTracing
    bool m_rasterInitialized = false;
};

OpenGLViewport::OpenGLViewport(QQuickItem* parent)
    : QQuickFramebufferObject(parent)
    , m_camera(std::make_unique<render::Camera>())
{
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    setMirrorVertically(true);

    m_lastFrameTime = QDateTime::currentMSecsSinceEpoch();

    // Connect to StructureModel to receive structure updates
    QTimer::singleShot(0, this, [this]() {
        if (auto* model = StructureModel::instance()) {
            connect(model, &StructureModel::structureUpdated,
                    this, &OpenGLViewport::setStructure);
        }
    });
}

OpenGLViewport::~OpenGLViewport() = default;

QQuickFramebufferObject::Renderer* OpenGLViewport::createRenderer() const {
    return new RendererImpl(const_cast<OpenGLViewport*>(this));
}

int OpenGLViewport::atomCount() const {
    return m_structure ? static_cast<int>(m_structure->atomCount()) : 0;
}

int OpenGLViewport::bondCount() const {
    return m_structure ? static_cast<int>(m_structure->bonds().bondCount()) : 0;
}

float OpenGLViewport::fps() const {
    return m_fps;
}

bool OpenGLViewport::showBonds() const {
    return m_showBonds;
}

QColor OpenGLViewport::backgroundColor() const {
    return m_backgroundColor;
}

bool OpenGLViewport::showUnitCell() const {
    return m_showUnitCell;
}

float OpenGLViewport::unitCellThickness() const {
    return m_unitCellThickness;
}

QColor OpenGLViewport::unitCellColor() const {
    return m_unitCellColor;
}

float OpenGLViewport::atomScale() const {
    return m_atomScale;
}

int OpenGLViewport::rendererMode() const {
    return m_rendererMode;
}

int OpenGLViewport::sampleCount() const {
    return m_sampleCount;
}

int OpenGLViewport::maxRTSamples() const {
    return m_maxRTSamples;
}

bool OpenGLViewport::enableAO() const {
    return m_enableAO;
}

bool OpenGLViewport::enableShadows() const {
    return m_enableShadows;
}

float OpenGLViewport::shadowOpacity() const {
    return m_shadowOpacity;
}

int OpenGLViewport::aoSamples() const {
    return m_aoSamples;
}

float OpenGLViewport::aoRadius() const {
    return m_aoRadius;
}

float OpenGLViewport::ambientStrength() const {
    return m_ambientStrength;
}

float OpenGLViewport::diffuseStrength() const {
    return m_diffuseStrength;
}

float OpenGLViewport::specularStrength() const {
    return m_specularStrength;
}

float OpenGLViewport::shininess() const {
    return m_shininess;
}

float OpenGLViewport::lightDirX() const {
    return m_lightDirX;
}

float OpenGLViewport::lightDirY() const {
    return m_lightDirY;
}

float OpenGLViewport::lightDirZ() const {
    return m_lightDirZ;
}

QVariantList OpenGLViewport::getAxisDirections() const {
    QMatrix4x4 view = m_camera->viewMatrix();
    return {
        view(0, 0), -view(1, 0), view(2, 0),  // World X axis
        view(0, 1), -view(1, 1), view(2, 1),  // World Y axis
        view(0, 2), -view(1, 2), view(2, 2)   // World Z axis
    };
}

void OpenGLViewport::setStructure(std::shared_ptr<data::Structure> structure) {
    m_structure = structure;
    m_needsStructureUpdate = true;

    emit atomCountChanged();
    emit bondCountChanged();

    if (m_structure) {
        fitToView();
        startBondDetection();
    }

    update();
}

float OpenGLViewport::bondScale() const {
    return m_bondScale;
}

void OpenGLViewport::setBondScale(float scale) {
    if (!std::isfinite(scale)) return;
    scale = std::clamp(scale, 0.1f, 5.0f);
    if (qFuzzyCompare(m_bondScale, scale)) return;
    m_bondScale = scale;
    emit bondScaleChanged();
    if (m_structure) {
        startBondDetection();
    }
}

void OpenGLViewport::startBondDetection() {
    if (!m_structure) return;

    if (m_bondTaskRunning) {
        // Coalesce: remember the latest request; launchBondTask will pick it up.
        m_bondTaskPending  = true;
        m_pendingStructure = m_structure;
        m_pendingScale     = m_bondScale;
        return;
    }
    launchBondTask(m_structure, m_bondScale);
}

void OpenGLViewport::launchBondTask(
    std::shared_ptr<data::Structure> structure, float scale)
{
    if (!m_bondWatcher) {
        m_bondWatcher = new QFutureWatcher<BondResult>(this);
        connect(m_bondWatcher, &QFutureWatcher<BondResult>::finished,
                this, &OpenGLViewport::onBondsReady);
    }
    m_bondTaskRunning = true;
    m_bondTaskPending = false;

    m_bondWatcher->setFuture(
        QtConcurrent::run([structure, scale]() -> BondResult {
            data::NeighborList nl;
            nl.build(*structure, scale);
            return {nl.buildBondList(*structure, scale), structure};
        }));
}

void OpenGLViewport::onBondsReady() {
    m_bondTaskRunning = false;

    if (m_bondWatcher && m_structure) {
        BondResult result = m_bondWatcher->result();
        // Discard if the structure has changed since the task was launched.
        if (result.structure == m_structure && result.bonds) {
            m_structure->setBondList(std::move(result.bonds));
            m_needsStructureUpdate = true;
            emit bondCountChanged();
            if (auto* model = StructureModel::instance())
                model->notifyBondsUpdated();
            update();
        }
    }

    if (m_bondTaskPending && m_pendingStructure) {
        auto s   = std::move(m_pendingStructure);
        float sc = m_pendingScale;
        launchBondTask(std::move(s), sc);
    }
}

void OpenGLViewport::fitToView() {
    if (!m_structure || m_structure->atomCount() == 0) return;

    auto bbox = m_structure->computeBoundingBox();
    QVector3D center(bbox.centerX(), bbox.centerY(), bbox.centerZ());
    float extent = bbox.maxExtent();

    m_camera->fitToView(center, extent > 0 ? extent : 10.0f);
    emit cameraChanged();
    update();
}

void OpenGLViewport::resetCamera() {
    m_camera->reset();
    if (m_structure) {
        fitToView();
    } else {
        emit cameraChanged();
    }
    update();
}

void OpenGLViewport::setShowBonds(bool show) {
    if (m_showBonds != show) {
        m_showBonds = show;
        emit showBondsChanged();
        update();
    }
}

void OpenGLViewport::setBackgroundColor(const QColor& color) {
    QColor opaque = color;
    opaque.setAlpha(255);
    if (m_backgroundColor != opaque) {
        m_backgroundColor = opaque;
        emit backgroundColorChanged();
        update();
    }
}

void OpenGLViewport::setShowUnitCell(bool show) {
    if (m_showUnitCell != show) {
        m_showUnitCell = show;
        emit showUnitCellChanged();
        update();
    }
}

void OpenGLViewport::setUnitCellThickness(float thickness) {
    const float clamped = std::clamp(thickness, 0.01f, 2.0f);
    if (!qFuzzyCompare(m_unitCellThickness, clamped)) {
        m_unitCellThickness = clamped;
        emit unitCellThicknessChanged();
        update();
    }
}

void OpenGLViewport::setUnitCellColor(const QColor& color) {
    QColor opaque = color;
    opaque.setAlpha(255);
    if (m_unitCellColor != opaque) {
        m_unitCellColor = opaque;
        emit unitCellColorChanged();
        update();
    }
}

void OpenGLViewport::setAtomScale(float scale) {
    if (!qFuzzyCompare(m_atomScale, scale)) {
        m_atomScale = scale;
        emit atomScaleChanged();
        update();
    }
}

void OpenGLViewport::setRendererMode(int mode) {
    if (m_rendererMode != mode) {
        m_rendererMode = mode;
        emit rendererModeChanged();
        update();
    }
}

void OpenGLViewport::setMaxRTSamples(int samples) {
    if (samples < 1 || samples > 10000) {
        qWarning() << "OpenGLViewport: maxRTSamples must be in [1, 10000], got" << samples;
        return;
    }

    if (m_maxRTSamples != samples) {
        m_maxRTSamples = samples;
        emit maxRTSamplesChanged();
        update();
    }
}

void OpenGLViewport::setEnableAO(bool enable) {
    if (m_enableAO != enable) {
        m_enableAO = enable;
        emit enableAOChanged();
        update();
    }
}

void OpenGLViewport::setEnableShadows(bool enable) {
    if (m_enableShadows != enable) {
        m_enableShadows = enable;
        emit enableShadowsChanged();
        update();
    }
}

void OpenGLViewport::setShadowOpacity(float opacity) {
    const float clamped = std::clamp(opacity, 0.0f, 1.0f);
    if (!qFuzzyCompare(m_shadowOpacity, clamped)) {
        m_shadowOpacity = clamped;
        emit shadowOpacityChanged();
        update();
    }
}

void OpenGLViewport::setAOSamples(int samples) {
    const int clamped = std::clamp(samples, 1, 16);
    if (m_aoSamples != clamped) {
        m_aoSamples = clamped;
        emit aoSamplesChanged();
        update();
    }
}

void OpenGLViewport::setAORadius(float radius) {
    const float clamped = std::clamp(radius, 1.0f, 10.0f);
    if (!qFuzzyCompare(m_aoRadius, clamped)) {
        m_aoRadius = clamped;
        emit aoRadiusChanged();
        update();
    }
}

void OpenGLViewport::setAmbientStrength(float strength) {
    const float clamped = std::clamp(strength, 0.0f, 1.0f);
    if (!qFuzzyCompare(m_ambientStrength, clamped)) {
        m_ambientStrength = clamped;
        emit ambientStrengthChanged();
        update();
    }
}

void OpenGLViewport::setDiffuseStrength(float strength) {
    const float clamped = std::clamp(strength, 0.0f, 1.0f);
    if (!qFuzzyCompare(m_diffuseStrength, clamped)) {
        m_diffuseStrength = clamped;
        emit diffuseStrengthChanged();
        update();
    }
}

void OpenGLViewport::setSpecularStrength(float strength) {
    const float clamped = std::clamp(strength, 0.0f, 1.0f);
    if (!qFuzzyCompare(m_specularStrength, clamped)) {
        m_specularStrength = clamped;
        emit specularStrengthChanged();
        update();
    }
}

void OpenGLViewport::setShininess(float shininess) {
    const float clamped = std::clamp(shininess, 1.0f, 128.0f);
    if (!qFuzzyCompare(m_shininess, clamped)) {
        m_shininess = clamped;
        emit shininessChanged();
        update();
    }
}

void OpenGLViewport::setLightDirX(float value) {
    const float clamped = std::clamp(value, -1.0f, 1.0f);
    if (!qFuzzyCompare(m_lightDirX, clamped)) {
        m_lightDirX = clamped;
        emit lightDirXChanged();
        update();
    }
}

void OpenGLViewport::setLightDirY(float value) {
    const float clamped = std::clamp(value, -1.0f, 1.0f);
    if (!qFuzzyCompare(m_lightDirY, clamped)) {
        m_lightDirY = clamped;
        emit lightDirYChanged();
        update();
    }
}

void OpenGLViewport::setLightDirZ(float value) {
    const float clamped = std::clamp(value, -1.0f, 1.0f);
    if (!qFuzzyCompare(m_lightDirZ, clamped)) {
        m_lightDirZ = clamped;
        emit lightDirZChanged();
        update();
    }
}

bool OpenGLViewport::event(QEvent* event) {
    if (event->type() == QEvent::NativeGesture) {
        auto* ge = static_cast<QNativeGestureEvent*>(event);
        if (ge->gestureType() == Qt::ZoomNativeGesture) {
            // value() > 0: spread (zoom in), value() < 0: pinch (zoom out)
            float factor = 1.0f / (1.0f + static_cast<float>(ge->value()));

            // Zoom toward pinch center, same as zoom-to-cursor
            QPointF pos = ge->position();
            int w = static_cast<int>(width());
            int h = static_cast<int>(height());
            QVector3D cursorWorld = m_camera->screenToWorld(
                static_cast<float>(pos.x()),
                static_cast<float>(pos.y()),
                m_camera->distance(), w, h);
            m_camera->setTarget(
                m_camera->target() + (cursorWorld - m_camera->target()) * (1.0f - factor));

            m_camera->zoom(factor);
            emit cameraChanged();
            update();
            event->accept();
            return true;
        }
    }
    return QQuickFramebufferObject::event(event);
}

void OpenGLViewport::mousePressEvent(QMouseEvent* event) {
    m_lastMousePos = event->position();
    m_pressedButtons = event->buttons();
    event->accept();
}

void OpenGLViewport::mouseMoveEvent(QMouseEvent* event) {
    QPointF delta = event->position() - m_lastMousePos;
    m_lastMousePos = event->position();

    if (m_pressedButtons & Qt::LeftButton) {
        // Orbit
        m_camera->orbit(-delta.x() * 0.5f, -delta.y() * 0.5f);
    } else if (m_pressedButtons & Qt::RightButton) {
        // Pan
        m_camera->pan(delta.x(), delta.y());
    } else if (m_pressedButtons & Qt::MiddleButton) {
        // Zoom via drag: drag up (delta.y < 0) → zoom in (factor < 1)
        float factor = std::max(0.01f, 1.0f + static_cast<float>(delta.y()) * 0.01f);
        m_camera->zoom(factor);
    }

    emit cameraChanged();
    update();
    event->accept();
}

void OpenGLViewport::mouseReleaseEvent(QMouseEvent* event) {
    m_pressedButtons = event->buttons();
    event->accept();
}

void OpenGLViewport::wheelEvent(QWheelEvent* event) {
    float delta = event->angleDelta().y();
    if (qFuzzyIsNull(delta)) { event->accept(); return; }

    // Proportional zoom: one standard notch (delta=120) → ~11% zoom
    float factor = std::pow(0.999f, delta);

    // Zoom-to-cursor: shift target toward the world point under the cursor
    QPointF cursorPos = event->position();
    int w = static_cast<int>(width());
    int h = static_cast<int>(height());
    QVector3D cursorWorld = m_camera->screenToWorld(
        static_cast<float>(cursorPos.x()),
        static_cast<float>(cursorPos.y()),
        m_camera->distance(), w, h);
    m_camera->setTarget(
        m_camera->target() + (cursorWorld - m_camera->target()) * (1.0f - factor));

    m_camera->zoom(factor);
    emit cameraChanged();
    update();
    event->accept();
}

void OpenGLViewport::mouseDoubleClickEvent(QMouseEvent* event) {
    resetCamera();
    event->accept();
}

void OpenGLViewport::geometryChange(const QRectF& newGeometry,
                                     const QRectF& oldGeometry) {
    QQuickFramebufferObject::geometryChange(newGeometry, oldGeometry);

    if (newGeometry.size() != oldGeometry.size()) {
        float aspect = newGeometry.width() / newGeometry.height();
        if (aspect > 0) {
            m_camera->setAspectRatio(aspect);
        }
    }
}

} // namespace atom::ui
