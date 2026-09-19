#include "common/Camera.h"
#include "common/GizmoOverlay.h"

#include <QVector2D>
#include <QSize>
#include <cmath>
#include <iostream>

using namespace atom::render;

namespace {
QVector2D pixelPosition(const GizmoOverlay& overlay, QVector3D point, int w, int h) {
    const auto clip = overlay.projectionMatrix * overlay.viewMatrix * QVector4D(point, 1);
    return {(clip.x() / clip.w() + 1) * w / 2,
            (1 - clip.y() / clip.w()) * h / 2};
}
}

int main() {
    for (const auto orientation : {QQuaternion(), QQuaternion::fromEulerAngles(27, 41, 13)}) {
        for (const auto viewport : {QSize(256, 256), QSize(800, 300), QSize(300, 800)}) {
            for (float dpr : {1.0f, 1.5f, 2.0f}) {
                const int w = qRound(viewport.width() * dpr);
                const int h = qRound(viewport.height() * dpr);
                Camera camera;
                camera.setOrientation(orientation);
                const auto reference = makeGizmoOverlay(camera, w, h, dpr);
                const QVector2D center(w / 2.0f, h / 2.0f);
                if ((pixelPosition(reference, {}, w, h) - center).length() > 0.001f)
                    return 1;
                for (const QVector3D axis : {QVector3D(1, 0, 0), QVector3D(0, 1, 0), QVector3D(0, 0, 1)}) {
                    const auto direction = orientation.conjugated().rotatedVector(axis);
                    const QVector2D expected(direction.x() * 24, -direction.y() * 24);
                    const auto actual = (pixelPosition(reference, axis * reference.axisLength, w, h)
                                         - center) / dpr;
                    if ((actual - expected).length() > 0.001f ||
                        std::abs(reference.radius / dpr - 1.2f) > 0.0001f) {
                        std::cerr << "Gizmo dimensions or orientation changed with viewport/DPI\n";
                        return 1;
                    }
                }
                for (bool perspective : {true, false}) {
                    camera.setProjection(perspective);
                    for (float fov : {10.0f, 45.0f, 90.0f, 170.0f}) {
                        camera.setFieldOfView(fov);
                        for (float scale : {0.1f, 10.0f, 1000.0f}) {
                            camera.setDistance(scale);
                            camera.setOrthoScale(scale);
                            camera.setTarget({10000, -5000, 3000});
                            camera.setNearPlane(2);
                            camera.setFarPlane(50);
                            camera.setAspectRatio(float(w) / h);
                            const auto result = makeGizmoOverlay(camera, w, h, dpr);
                            if (result.viewMatrix != reference.viewMatrix ||
                                result.projectionMatrix != reference.projectionMatrix ||
                                result.axisLength != reference.axisLength ||
                                result.radius != reference.radius) {
                                std::cerr << "Scene camera settings affected the gizmo\n";
                                return 1;
                            }
                        }
                    }
                }
            }
        }
    }
    return 0;
}
