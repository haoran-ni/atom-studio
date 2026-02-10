#import <Metal/Metal.h>

#include "MetalViewport.h"
#include "StructureModel.h"
#include "../../render/common/Camera.h"
#include "../../render/metal/MetalRenderer.h"
#include "../../render/metal/MetalRayTracingRenderer.h"
#include "../../data/Structure.h"

#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGRendererInterface>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDateTime>
#include <QTimer>
#include <QDebug>
#include <algorithm>

// Qt 6 native interface for wrapping Metal textures
#include <QSGTexture>

namespace atom::ui {

// ---------------------------------------------------------------------------
// PIMPL — holds Metal renderer instances
// ---------------------------------------------------------------------------

struct MetalViewport::Impl {
    std::unique_ptr<render::metal::MetalRenderer> rasterRenderer;
    std::unique_ptr<render::metal::MetalRayTracingRenderer> rtRenderer;
    render::Renderer* activeRenderer = nullptr;
    int currentMode = 0;

    QSGTexture* cachedTexture = nullptr; // Owned by scene graph
};

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

MetalViewport::MetalViewport(QQuickItem* parent)
    : QQuickItem(parent)
    , m_impl(std::make_unique<Impl>())
    , m_camera(std::make_unique<render::Camera>())
{
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);

    m_lastFrameTime = QDateTime::currentMSecsSinceEpoch();

    // Connect to StructureModel for file loads
    QTimer::singleShot(0, this, [this]() {
        if (auto* model = StructureModel::instance()) {
            connect(model, &StructureModel::structureUpdated,
                    this, &MetalViewport::setStructure);
        }
    });
}

MetalViewport::~MetalViewport() = default;

// ---------------------------------------------------------------------------
// Properties (identical to OpenGLViewport)
// ---------------------------------------------------------------------------

int MetalViewport::atomCount() const {
    return m_structure ? static_cast<int>(m_structure->atomCount()) : 0;
}

int MetalViewport::bondCount() const {
    return m_structure ? static_cast<int>(m_structure->bonds().bondCount()) : 0;
}

float MetalViewport::fps() const { return m_fps; }
bool MetalViewport::showBonds() const { return m_showBonds; }
bool MetalViewport::showUnitCell() const { return m_showUnitCell; }
float MetalViewport::unitCellThickness() const { return m_unitCellThickness; }
QColor MetalViewport::unitCellColor() const { return m_unitCellColor; }
float MetalViewport::atomScale() const { return m_atomScale; }
int MetalViewport::rendererMode() const { return m_rendererMode; }
int MetalViewport::sampleCount() const { return m_sampleCount; }
int MetalViewport::maxRTSamples() const { return m_maxRTSamples; }
bool MetalViewport::enableAO() const { return m_enableAO; }
bool MetalViewport::enableShadows() const { return m_enableShadows; }

QVariantList MetalViewport::getAxisDirections() const {
    QMatrix4x4 view = m_camera->viewMatrix();
    return {
        view(0, 0), -view(1, 0), view(2, 0),
        view(0, 1), -view(1, 1), view(2, 1),
        view(0, 2), -view(1, 2), view(2, 2)
    };
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void MetalViewport::setStructure(std::shared_ptr<data::Structure> structure) {
    m_structure = structure;
    m_needsStructureUpdate = true;

    emit atomCountChanged();
    emit bondCountChanged();

    if (m_structure) {
        fitToView();
    }
    update();
}

void MetalViewport::fitToView() {
    if (!m_structure || m_structure->atomCount() == 0) return;

    auto bbox = m_structure->computeBoundingBox();
    QVector3D center(bbox.centerX(), bbox.centerY(), bbox.centerZ());
    float extent = bbox.maxExtent();

    m_camera->fitToView(center, extent > 0 ? extent : 10.0f);
    emit cameraChanged();
    update();
}

void MetalViewport::resetCamera() {
    m_camera->reset();
    if (m_structure) {
        fitToView();
    } else {
        emit cameraChanged();
    }
    update();
}

void MetalViewport::setShowBonds(bool show) {
    if (m_showBonds != show) {
        m_showBonds = show;
        emit showBondsChanged();
        update();
    }
}

void MetalViewport::setShowUnitCell(bool show) {
    if (m_showUnitCell != show) {
        m_showUnitCell = show;
        emit showUnitCellChanged();
        update();
    }
}

void MetalViewport::setUnitCellThickness(float thickness) {
    const float clamped = std::clamp(thickness, 0.01f, 2.0f);
    if (!qFuzzyCompare(m_unitCellThickness, clamped)) {
        m_unitCellThickness = clamped;
        emit unitCellThicknessChanged();
        update();
    }
}

void MetalViewport::setUnitCellColor(const QColor& color) {
    QColor opaque = color;
    opaque.setAlpha(255);
    if (m_unitCellColor != opaque) {
        m_unitCellColor = opaque;
        emit unitCellColorChanged();
        update();
    }
}

void MetalViewport::setAtomScale(float scale) {
    if (!qFuzzyCompare(m_atomScale, scale)) {
        m_atomScale = scale;
        emit atomScaleChanged();
        update();
    }
}

void MetalViewport::setRendererMode(int mode) {
    if (m_rendererMode != mode) {
        m_rendererMode = mode;
        emit rendererModeChanged();
        update();
    }
}

void MetalViewport::setMaxRTSamples(int samples) {
    if (samples < 1 || samples > 10000) {
        qWarning() << "MetalViewport: maxRTSamples must be in [1, 10000], got" << samples;
        return;
    }

    if (m_maxRTSamples != samples) {
        m_maxRTSamples = samples;
        emit maxRTSamplesChanged();
        update();
    }
}

void MetalViewport::setEnableAO(bool enable) {
    if (m_enableAO != enable) {
        m_enableAO = enable;
        emit enableAOChanged();
        update();
    }
}

void MetalViewport::setEnableShadows(bool enable) {
    if (m_enableShadows != enable) {
        m_enableShadows = enable;
        emit enableShadowsChanged();
        update();
    }
}

// ---------------------------------------------------------------------------
// Scene graph: updatePaintNode
// ---------------------------------------------------------------------------

QSGNode* MetalViewport::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    // This runs on the render thread with the GUI thread blocked.

    // 1. Get Metal device from Qt's scene graph
    QSGRendererInterface* ri = window()->rendererInterface();
    void* devicePtr = ri->getResource(window(), QSGRendererInterface::DeviceResource);
    if (!devicePtr) {
        qWarning() << "MetalViewport: could not get Metal device from Qt";
        return oldNode;
    }

    // 2. Initialize renderers on first call
    if (!m_metalInitialized) {
        m_impl->rasterRenderer = std::make_unique<render::metal::MetalRenderer>();
        m_impl->rasterRenderer->setDevice(devicePtr);

        if (!m_impl->rasterRenderer->initialize()) {
            qCritical() << "MetalViewport: failed to initialize Metal raster renderer";
            return oldNode;
        }

        m_impl->activeRenderer = m_impl->rasterRenderer.get();
        m_impl->currentMode = 0;
        m_metalInitialized = true;
    }

    // 3. Handle renderer mode switching
    if (m_rendererMode != m_impl->currentMode) {
        m_impl->currentMode = m_rendererMode;

        if (m_impl->currentMode == 1) {
            // Switch to ray tracing
            if (!m_impl->rtRenderer) {
                m_impl->rtRenderer = std::make_unique<render::metal::MetalRayTracingRenderer>();
                m_impl->rtRenderer->setDevice(devicePtr);
                if (!m_impl->rtRenderer->initialize()) {
                    qCritical() << "MetalViewport: failed to init RT renderer";
                    m_impl->currentMode = 0;
                    m_rendererMode = 0;
                    emit rendererModeChanged();
                }
            }
            if (m_impl->rtRenderer) {
                m_impl->activeRenderer = m_impl->rtRenderer.get();
                if (m_structure) {
                    m_impl->rtRenderer->setStructure(m_structure.get());
                }
            }
        } else {
            m_impl->activeRenderer = m_impl->rasterRenderer.get();
            m_sampleCount = 0;
            emit sampleCountChanged();
        }
    }

    // 4. Build render settings from viewport properties
    m_renderSettings.showBonds = m_showBonds;
    m_renderSettings.showUnitCell = m_showUnitCell;
    m_renderSettings.unitCellThickness = m_unitCellThickness;
    m_renderSettings.unitCellColor = m_unitCellColor;
    m_renderSettings.atomScale = m_atomScale;
    m_renderSettings.maxRTSamples = m_maxRTSamples;
    m_renderSettings.enableAmbientOcclusion = m_enableAO;
    m_renderSettings.enableShadows = m_enableShadows;

    if (m_needsStructureUpdate) {
        m_impl->activeRenderer->setStructure(m_structure.get());
        // Also update the other renderer for instant switching
        if (m_impl->currentMode == 0 && m_impl->rtRenderer) {
            m_impl->rtRenderer->setStructure(m_structure.get());
        } else if (m_impl->currentMode == 1 && m_impl->rasterRenderer) {
            m_impl->rasterRenderer->setStructure(m_structure.get());
        }
        m_needsStructureUpdate = false;
    }

    // 5. Resize
    qreal dpr = window()->devicePixelRatio();
    int pw = static_cast<int>(width() * dpr);
    int ph = static_cast<int>(height() * dpr);
    if (pw > 0 && ph > 0) {
        m_impl->activeRenderer->resize(pw, ph);
        m_camera->setAspectRatio(static_cast<float>(width()) / static_cast<float>(height()));
    }

    // 6. Render
    m_impl->activeRenderer->render(*m_camera, m_renderSettings);

    // 7. Get the output texture
    void* mtlTexture = nullptr;
    if (m_impl->currentMode == 1 && m_impl->rtRenderer) {
        m_sampleCount = m_impl->rtRenderer->sampleCount();
        emit sampleCountChanged();
        mtlTexture = m_impl->rtRenderer->outputTexture();
    } else {
        mtlTexture = m_impl->rasterRenderer->colorTexture();
    }

    if (!mtlTexture) {
        return oldNode;
    }

    // 8. Wrap MTLTexture in QSGTexture and display via QSGSimpleTextureNode
    auto* node = static_cast<QSGSimpleTextureNode*>(oldNode);
    if (!node) {
        node = new QSGSimpleTextureNode();
        node->setTextureCoordinatesTransform(QSGSimpleTextureNode::MirrorVertically);
    }

    // Create QSGTexture from native Metal texture
    // Use QQuickWindow::createTextureFromNativeObject for cross-version compatibility
    QSGTexture* tex = QNativeInterface::QSGMetalTexture::fromNative(
        (__bridge id<MTLTexture>)mtlTexture,
        window(),
        QSize(pw, ph));

    if (tex) {
        // Clean up previous texture if it exists
        if (m_impl->cachedTexture && m_impl->cachedTexture != tex) {
            delete m_impl->cachedTexture;
        }
        m_impl->cachedTexture = tex;
        node->setTexture(tex);
    }

    node->setRect(boundingRect());
    node->markDirty(QSGNode::DirtyMaterial);

    // 9. Update FPS
    m_frameCount++;
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    if (currentTime - m_lastFrameTime >= 1000) {
        m_fps = m_frameCount * 1000.0f / (currentTime - m_lastFrameTime);
        m_lastFrameTime = currentTime;
        m_frameCount = 0;
        emit fpsChanged();
    }

    // 10. Request next frame
    if (m_impl->currentMode == 1 && m_impl->rtRenderer) {
        if (!m_impl->rtRenderer->isConverged()) {
            update();
        }
    } else {
        update();
    }

    return node;
}

// ---------------------------------------------------------------------------
// Geometry change
// ---------------------------------------------------------------------------

void MetalViewport::geometryChange(const QRectF& newGeometry,
                                    const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);

    if (newGeometry.size() != oldGeometry.size()) {
        float aspect = newGeometry.width() / newGeometry.height();
        if (aspect > 0) {
            m_camera->setAspectRatio(aspect);
        }
    }
}

// ---------------------------------------------------------------------------
// Mouse handling (identical to OpenGLViewport)
// ---------------------------------------------------------------------------

void MetalViewport::mousePressEvent(QMouseEvent* event) {
    m_lastMousePos = event->position();
    m_pressedButtons = event->buttons();
    event->accept();
}

void MetalViewport::mouseMoveEvent(QMouseEvent* event) {
    QPointF delta = event->position() - m_lastMousePos;
    m_lastMousePos = event->position();

    if (m_pressedButtons & Qt::LeftButton) {
        m_camera->orbit(-delta.x() * 0.5f, -delta.y() * 0.5f);
    } else if (m_pressedButtons & Qt::RightButton) {
        m_camera->pan(delta.x(), delta.y());
    } else if (m_pressedButtons & Qt::MiddleButton) {
        m_camera->zoom(1.0f - delta.y() * 0.01f);
    }

    emit cameraChanged();
    update();
    event->accept();
}

void MetalViewport::mouseReleaseEvent(QMouseEvent* event) {
    m_pressedButtons = event->buttons();
    event->accept();
}

void MetalViewport::wheelEvent(QWheelEvent* event) {
    float delta = event->angleDelta().y();
    float factor = (delta > 0) ? 0.9f : 1.1f;
    m_camera->zoom(factor);
    emit cameraChanged();
    update();
    event->accept();
}

void MetalViewport::mouseDoubleClickEvent(QMouseEvent* event) {
    resetCamera();
    event->accept();
}

} // namespace atom::ui
