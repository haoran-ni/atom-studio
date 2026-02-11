#pragma once

#include <QQuickFramebufferObject>
#include <QOpenGLFramebufferObject>
#include <QColor>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include "../../render/common/RenderSettings.h"

namespace atom::data {
class Structure;
}

namespace atom::render {
class Camera;
class OpenGLRenderer;
class RayTracingRenderer;
}

namespace atom::ui {

/**
 * @brief QML component for OpenGL rendering
 *
 * Integrates the OpenGL renderer with Qt Quick using QQuickFramebufferObject.
 * Supports switching between rasterization and ray tracing renderers.
 * Handles mouse events for camera control.
 */
class OpenGLViewport : public QQuickFramebufferObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int atomCount READ atomCount NOTIFY atomCountChanged)
    Q_PROPERTY(int bondCount READ bondCount NOTIFY bondCountChanged)
    Q_PROPERTY(float fps READ fps NOTIFY fpsChanged)
    Q_PROPERTY(bool showBonds READ showBonds WRITE setShowBonds NOTIFY showBondsChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY backgroundColorChanged)
    Q_PROPERTY(bool showUnitCell READ showUnitCell WRITE setShowUnitCell NOTIFY showUnitCellChanged)
    Q_PROPERTY(float unitCellThickness READ unitCellThickness WRITE setUnitCellThickness NOTIFY unitCellThicknessChanged)
    Q_PROPERTY(QColor unitCellColor READ unitCellColor WRITE setUnitCellColor NOTIFY unitCellColorChanged)
    Q_PROPERTY(float atomScale READ atomScale WRITE setAtomScale NOTIFY atomScaleChanged)
    Q_PROPERTY(int rendererMode READ rendererMode WRITE setRendererMode NOTIFY rendererModeChanged)
    Q_PROPERTY(int sampleCount READ sampleCount NOTIFY sampleCountChanged)
    Q_PROPERTY(int maxRTSamples READ maxRTSamples WRITE setMaxRTSamples NOTIFY maxRTSamplesChanged)
    Q_PROPERTY(bool enableAO READ enableAO WRITE setEnableAO NOTIFY enableAOChanged)
    Q_PROPERTY(bool enableShadows READ enableShadows WRITE setEnableShadows NOTIFY enableShadowsChanged)

public:
    explicit OpenGLViewport(QQuickItem* parent = nullptr);
    ~OpenGLViewport() override;

    Renderer* createRenderer() const override;

    // Properties
    int atomCount() const;
    int bondCount() const;
    float fps() const;
    bool showBonds() const;
    QColor backgroundColor() const;
    bool showUnitCell() const;
    float unitCellThickness() const;
    QColor unitCellColor() const;
    float atomScale() const;
    int rendererMode() const;
    int sampleCount() const;
    int maxRTSamples() const;
    bool enableAO() const;
    bool enableShadows() const;

    Q_INVOKABLE QVariantList getAxisDirections() const;

public slots:
    void setStructure(std::shared_ptr<atom::data::Structure> structure);
    void fitToView();
    void resetCamera();
    void setShowBonds(bool show);
    void setBackgroundColor(const QColor& color);
    void setShowUnitCell(bool show);
    void setUnitCellThickness(float thickness);
    void setUnitCellColor(const QColor& color);
    void setAtomScale(float scale);
    void setRendererMode(int mode);
    void setMaxRTSamples(int samples);
    void setEnableAO(bool enable);
    void setEnableShadows(bool enable);

signals:
    void atomCountChanged();
    void bondCountChanged();
    void fpsChanged();
    void showBondsChanged();
    void backgroundColorChanged();
    void showUnitCellChanged();
    void unitCellThicknessChanged();
    void unitCellColorChanged();
    void atomScaleChanged();
    void rendererModeChanged();
    void sampleCountChanged();
    void maxRTSamplesChanged();
    void enableAOChanged();
    void enableShadowsChanged();
    void cameraChanged();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void geometryChange(const QRectF& newGeometry,
                        const QRectF& oldGeometry) override;

private:
    class RendererImpl;
    friend class RendererImpl;

    std::shared_ptr<data::Structure> m_structure;
    std::unique_ptr<render::Camera> m_camera;
    render::RenderSettings m_renderSettings;

    QPointF m_lastMousePos;
    Qt::MouseButtons m_pressedButtons;

    bool m_showBonds = true;
    QColor m_backgroundColor = QColor(230, 230, 230);
    bool m_showUnitCell = true;
    float m_unitCellThickness = 0.12f;
    QColor m_unitCellColor = QColor(255, 255, 255);
    float m_atomScale = 1.0f;
    int m_rendererMode = 0;       // 0 = Raster, 1 = RayTracing
    int m_sampleCount = 0;
    int m_maxRTSamples = 1000;
    bool m_enableAO = false;
    bool m_enableShadows = false;
    float m_fps = 0.0f;
    qint64 m_lastFrameTime = 0;
    int m_frameCount = 0;

    bool m_needsStructureUpdate = false;
    bool m_needsCameraUpdate = false;
    bool m_rendererModeChanged = false;
};

} // namespace atom::ui
