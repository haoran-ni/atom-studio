#include "OpenGLViewport.h"
#include "StructureModel.h"
#include "../../render/Camera.h"
#include "../../render/opengl/OpenGLRenderer.h"
#include "../../data/Structure.h"
#include "../../data/BondList.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QDateTime>
#include <QTimer>
#include <QOpenGLFramebufferObjectFormat>
#include <QDebug>

namespace atom::ui {

/**
 * @brief Renderer implementation for QQuickFramebufferObject
 */
class OpenGLViewport::RendererImpl : public QQuickFramebufferObject::Renderer {
public:
    RendererImpl(OpenGLViewport* viewport)
        : m_viewport(viewport)
        , m_renderer(std::make_unique<render::OpenGLRenderer>())
    {
    }

    void render() override {
        if (!m_initialized) {
            return;
        }

        m_renderer->render(*m_viewport->m_camera);
        update();
    }

    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        format.setSamples(4);
        return new QOpenGLFramebufferObject(size, format);
    }

    void synchronize(QQuickFramebufferObject* item) override {
        auto* viewport = static_cast<OpenGLViewport*>(item);

        // Initialize renderer if not done yet
        // (synchronize can be called before render in Qt's scene graph)
        if (!m_initialized) {
            if (!m_renderer->initialize()) {
                qCritical() << "Failed to initialize OpenGL renderer";
                return;
            }
            m_initialized = true;
        }

        // Update viewport size
        QSize size = viewport->size().toSize();
        if (size.width() > 0 && size.height() > 0) {
            m_renderer->resize(size.width(), size.height());
            viewport->m_camera->setAspectRatio(
                static_cast<float>(size.width()) / size.height());
        }

        // Update structure if needed
        if (viewport->m_needsStructureUpdate) {
            m_renderer->setStructure(viewport->m_structure.get());
            viewport->m_needsStructureUpdate = false;
        }

        // Update render settings
        m_renderer->settings().showBonds = viewport->m_showBonds;
        m_renderer->settings().atomScale = viewport->m_atomScale;

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
    std::unique_ptr<render::OpenGLRenderer> m_renderer;
    bool m_initialized = false;
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

float OpenGLViewport::atomScale() const {
    return m_atomScale;
}

void OpenGLViewport::setStructure(std::shared_ptr<data::Structure> structure) {
    m_structure = structure;
    m_needsStructureUpdate = true;

    emit atomCountChanged();
    emit bondCountChanged();

    if (m_structure) {
        fitToView();
    }

    update();
}

void OpenGLViewport::fitToView() {
    if (!m_structure || m_structure->atomCount() == 0) return;

    auto bbox = m_structure->computeBoundingBox();
    QVector3D center(bbox.centerX(), bbox.centerY(), bbox.centerZ());
    float extent = bbox.maxExtent();

    m_camera->fitToView(center, extent > 0 ? extent : 10.0f);
    update();
}

void OpenGLViewport::resetCamera() {
    m_camera->reset();
    if (m_structure) {
        fitToView();
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

void OpenGLViewport::setAtomScale(float scale) {
    if (!qFuzzyCompare(m_atomScale, scale)) {
        m_atomScale = scale;
        emit atomScaleChanged();
        update();
    }
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
        // Zoom via drag
        m_camera->zoom(1.0f - delta.y() * 0.01f);
    }

    update();
    event->accept();
}

void OpenGLViewport::mouseReleaseEvent(QMouseEvent* event) {
    m_pressedButtons = event->buttons();
    event->accept();
}

void OpenGLViewport::wheelEvent(QWheelEvent* event) {
    float delta = event->angleDelta().y();
    float factor = (delta > 0) ? 0.9f : 1.1f;
    m_camera->zoom(factor);
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
