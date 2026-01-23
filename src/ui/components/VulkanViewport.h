#pragma once

#include <QQuickItem>

namespace atom::ui {

/**
 * @brief Placeholder for future Vulkan rendering surface
 *
 * This QQuickItem subclass will serve as the integration point between
 * Qt Quick and the Vulkan renderer. For now, it's a placeholder that
 * will be implemented when the Vulkan backend is ready.
 *
 * Future responsibilities:
 * - Create and manage Vulkan surface
 * - Handle resize events
 * - Synchronize with Qt's rendering thread
 * - Provide input events to the renderer
 */
class VulkanViewport : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool rayTracingEnabled READ rayTracingEnabled WRITE setRayTracingEnabled NOTIFY rayTracingEnabledChanged)
    Q_PROPERTY(double fps READ fps NOTIFY fpsChanged)

public:
    explicit VulkanViewport(QQuickItem* parent = nullptr);
    ~VulkanViewport() override;

    // Ray tracing toggle
    bool rayTracingEnabled() const { return m_rayTracingEnabled; }
    void setRayTracingEnabled(bool enabled);

    // Performance metrics
    double fps() const { return m_fps; }

signals:
    void rayTracingEnabledChanged();
    void fpsChanged();

protected:
    // QQuickItem overrides
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;

    // Input handling
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    bool m_rayTracingEnabled = false;
    double m_fps = 0.0;
};

} // namespace atom::ui
