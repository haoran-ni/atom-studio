#pragma once

#include <QQuickFramebufferObject>
#include <QOpenGLFramebufferObject>
#include <QtQml/qqmlregistration.h>
#include <memory>

namespace atom::data {
class Structure;
}

namespace atom::render {
class Camera;
class OpenGLRenderer;
}

namespace atom::ui {

/**
 * @brief QML component for OpenGL rendering
 *
 * Integrates the OpenGL renderer with Qt Quick using QQuickFramebufferObject.
 * Handles mouse events for camera control.
 */
class OpenGLViewport : public QQuickFramebufferObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int atomCount READ atomCount NOTIFY atomCountChanged)
    Q_PROPERTY(int bondCount READ bondCount NOTIFY bondCountChanged)
    Q_PROPERTY(float fps READ fps NOTIFY fpsChanged)
    Q_PROPERTY(bool showBonds READ showBonds WRITE setShowBonds NOTIFY showBondsChanged)
    Q_PROPERTY(float atomScale READ atomScale WRITE setAtomScale NOTIFY atomScaleChanged)

public:
    explicit OpenGLViewport(QQuickItem* parent = nullptr);
    ~OpenGLViewport() override;

    Renderer* createRenderer() const override;

    // Properties
    int atomCount() const;
    int bondCount() const;
    float fps() const;
    bool showBonds() const;
    float atomScale() const;

public slots:
    void setStructure(std::shared_ptr<atom::data::Structure> structure);
    void fitToView();
    void resetCamera();
    void setShowBonds(bool show);
    void setAtomScale(float scale);

signals:
    void atomCountChanged();
    void bondCountChanged();
    void fpsChanged();
    void showBondsChanged();
    void atomScaleChanged();

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

    QPointF m_lastMousePos;
    Qt::MouseButtons m_pressedButtons;

    bool m_showBonds = true;
    float m_atomScale = 1.0f;
    float m_fps = 0.0f;
    qint64 m_lastFrameTime = 0;
    int m_frameCount = 0;

    bool m_needsStructureUpdate = false;
    bool m_needsCameraUpdate = false;
};

} // namespace atom::ui
