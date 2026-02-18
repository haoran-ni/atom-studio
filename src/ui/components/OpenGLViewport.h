#pragma once

#include <QQuickFramebufferObject>
#include <QOpenGLFramebufferObject>
#include <QColor>
#include <QFutureWatcher>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include "../../render/common/RenderSettings.h"

namespace atom::data {
class Structure;
class BondList;
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
    Q_PROPERTY(float bondScale READ bondScale WRITE setBondScale NOTIFY bondScaleChanged)
    Q_PROPERTY(int rendererMode READ rendererMode WRITE setRendererMode NOTIFY rendererModeChanged)
    Q_PROPERTY(int sampleCount READ sampleCount NOTIFY sampleCountChanged)
    Q_PROPERTY(int maxRTSamples READ maxRTSamples WRITE setMaxRTSamples NOTIFY maxRTSamplesChanged)
    Q_PROPERTY(bool enableAO READ enableAO WRITE setEnableAO NOTIFY enableAOChanged)
    Q_PROPERTY(bool enableShadows READ enableShadows WRITE setEnableShadows NOTIFY enableShadowsChanged)
    Q_PROPERTY(int aoSamples READ aoSamples WRITE setAOSamples NOTIFY aoSamplesChanged)
    Q_PROPERTY(float aoRadius READ aoRadius WRITE setAORadius NOTIFY aoRadiusChanged)
    Q_PROPERTY(float ambientStrength READ ambientStrength WRITE setAmbientStrength NOTIFY ambientStrengthChanged)
    Q_PROPERTY(float diffuseStrength READ diffuseStrength WRITE setDiffuseStrength NOTIFY diffuseStrengthChanged)
    Q_PROPERTY(float specularStrength READ specularStrength WRITE setSpecularStrength NOTIFY specularStrengthChanged)
    Q_PROPERTY(float shininess READ shininess WRITE setShininess NOTIFY shininessChanged)
    Q_PROPERTY(float lightDirX READ lightDirX WRITE setLightDirX NOTIFY lightDirXChanged)
    Q_PROPERTY(float lightDirY READ lightDirY WRITE setLightDirY NOTIFY lightDirYChanged)
    Q_PROPERTY(float lightDirZ READ lightDirZ WRITE setLightDirZ NOTIFY lightDirZChanged)

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
    float bondScale() const;
    int rendererMode() const;
    int sampleCount() const;
    int maxRTSamples() const;
    bool enableAO() const;
    bool enableShadows() const;
    int aoSamples() const;
    float aoRadius() const;
    float ambientStrength() const;
    float diffuseStrength() const;
    float specularStrength() const;
    float shininess() const;
    float lightDirX() const;
    float lightDirY() const;
    float lightDirZ() const;

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
    void setBondScale(float scale);
    void setRendererMode(int mode);
    void setMaxRTSamples(int samples);
    void setEnableAO(bool enable);
    void setEnableShadows(bool enable);
    void setAOSamples(int samples);
    void setAORadius(float radius);
    void setAmbientStrength(float strength);
    void setDiffuseStrength(float strength);
    void setSpecularStrength(float strength);
    void setShininess(float shininess);
    void setLightDirX(float value);
    void setLightDirY(float value);
    void setLightDirZ(float value);

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
    void bondScaleChanged();
    void rendererModeChanged();
    void sampleCountChanged();
    void maxRTSamplesChanged();
    void enableAOChanged();
    void enableShadowsChanged();
    void aoSamplesChanged();
    void aoRadiusChanged();
    void ambientStrengthChanged();
    void diffuseStrengthChanged();
    void specularStrengthChanged();
    void shininessChanged();
    void lightDirXChanged();
    void lightDirYChanged();
    void lightDirZChanged();
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
    float m_unitCellThickness = 0.06f;
    QColor m_unitCellColor = QColor(0, 0, 0);
    float m_atomScale = 1.0f;
    float m_bondScale = 1.0f;
    int m_rendererMode = 0;       // 0 = Raster, 1 = RayTracing
    int m_sampleCount = 0;
    int m_maxRTSamples = 1000;
    bool m_enableAO = false;
    bool m_enableShadows = false;
    int m_aoSamples = 4;
    float m_aoRadius = 3.0f;
    float m_ambientStrength = 0.3f;
    float m_diffuseStrength = 0.7f;
    float m_specularStrength = 0.5f;
    float m_shininess = 32.0f;
    float m_lightDirX = 0.3f;
    float m_lightDirY = 0.8f;
    float m_lightDirZ = 0.5f;
    float m_fps = 0.0f;
    qint64 m_lastFrameTime = 0;
    int m_frameCount = 0;

    bool m_needsStructureUpdate = false;
    bool m_needsCameraUpdate = false;
    bool m_rendererModeChanged = false;

    // Async bond detection
    QFutureWatcher<std::shared_ptr<atom::data::BondList>>* m_bondWatcher = nullptr;
    void startBondDetection();

private slots:
    void onBondsReady();
};

} // namespace atom::ui
