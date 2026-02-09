#pragma once

#include <QQuickItem>
#include <QColor>
#include <QtQml/qqmlregistration.h>
#include <memory>

namespace atom::data { class Structure; }
namespace atom::render { class Camera; }

namespace atom::ui {

/// QQuickItem-based viewport for the Metal rendering backend.
/// Renders to an offscreen MTLTexture and displays it via QSGSimpleTextureNode.
/// Exposes the same Q_PROPERTY interface as OpenGLViewport for QML compatibility.
class MetalViewport : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int atomCount READ atomCount NOTIFY atomCountChanged)
    Q_PROPERTY(int bondCount READ bondCount NOTIFY bondCountChanged)
    Q_PROPERTY(float fps READ fps NOTIFY fpsChanged)
    Q_PROPERTY(bool showBonds READ showBonds WRITE setShowBonds NOTIFY showBondsChanged)
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
    explicit MetalViewport(QQuickItem* parent = nullptr);
    ~MetalViewport() override;

    // Properties
    int atomCount() const;
    int bondCount() const;
    float fps() const;
    bool showBonds() const;
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
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    std::shared_ptr<data::Structure> m_structure;
    std::unique_ptr<render::Camera> m_camera;

    QPointF m_lastMousePos;
    Qt::MouseButtons m_pressedButtons;

    bool m_showBonds = true;
    bool m_showUnitCell = true;
    float m_unitCellThickness = 0.12f;
    QColor m_unitCellColor = QColor(255, 255, 255);
    float m_atomScale = 1.0f;
    int m_rendererMode = 0;
    int m_sampleCount = 0;
    int m_maxRTSamples = 1000;
    bool m_enableAO = false;
    bool m_enableShadows = false;
    float m_fps = 0.0f;
    qint64 m_lastFrameTime = 0;
    int m_frameCount = 0;

    bool m_needsStructureUpdate = false;
    bool m_metalInitialized = false;
};

} // namespace atom::ui
