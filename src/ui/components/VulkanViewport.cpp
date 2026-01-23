#include "VulkanViewport.h"

#include <QDebug>

namespace atom::ui {

VulkanViewport::VulkanViewport(QQuickItem* parent)
    : QQuickItem(parent)
{
    // Enable input handling
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    setFlag(ItemAcceptsInputMethod, true);

    qDebug() << "VulkanViewport created (placeholder)";
}

VulkanViewport::~VulkanViewport()
{
    qDebug() << "VulkanViewport destroyed";
}

void VulkanViewport::setRayTracingEnabled(bool enabled)
{
    if (m_rayTracingEnabled != enabled) {
        m_rayTracingEnabled = enabled;
        emit rayTracingEnabledChanged();

        qDebug() << "Ray tracing" << (enabled ? "enabled" : "disabled") << "(placeholder)";
    }
}

void VulkanViewport::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);

    if (newGeometry.size() != oldGeometry.size()) {
        qDebug() << "Viewport resized to" << newGeometry.size() << "(placeholder - Vulkan swapchain resize would happen here)";
    }
}

void VulkanViewport::itemChange(ItemChange change, const ItemChangeData& value)
{
    QQuickItem::itemChange(change, value);

    if (change == ItemSceneChange) {
        qDebug() << "Viewport scene changed (placeholder - Vulkan initialization would happen here)";
    }
}

void VulkanViewport::mousePressEvent(QMouseEvent* event)
{
    qDebug() << "Mouse press at" << event->position() << "(placeholder)";
    event->accept();
}

void VulkanViewport::mouseReleaseEvent(QMouseEvent* event)
{
    qDebug() << "Mouse release at" << event->position() << "(placeholder)";
    event->accept();
}

void VulkanViewport::mouseMoveEvent(QMouseEvent* event)
{
    // Intentionally not logging mouse moves to avoid spam
    event->accept();
}

void VulkanViewport::wheelEvent(QWheelEvent* event)
{
    qDebug() << "Wheel event, delta:" << event->angleDelta() << "(placeholder)";
    event->accept();
}

void VulkanViewport::keyPressEvent(QKeyEvent* event)
{
    qDebug() << "Key press:" << event->key() << "(placeholder)";
    event->accept();
}

void VulkanViewport::keyReleaseEvent(QKeyEvent* event)
{
    qDebug() << "Key release:" << event->key() << "(placeholder)";
    event->accept();
}

} // namespace atom::ui
