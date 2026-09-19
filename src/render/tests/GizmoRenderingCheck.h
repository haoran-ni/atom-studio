#pragma once

#include "common/Camera.h"
#include "common/RenderSettings.h"
#include <QImage>
#include <cmath>
#include <iostream>

// The caller renders only the gizmo against black, using its actual GPU path.
template<class Snapshot>
bool checkGizmoRendering(atom::render::Camera& camera,
                         atom::render::RenderSettings& settings,
                         Snapshot snapshot) {
    settings.showRotationCenter = true;
    settings.showAtoms = settings.showBonds = settings.showUnitCell = false;
    settings.showViewportAxes = false;
    settings.backgroundColor = Qt::black;
    settings.maxRTSamples = 1;
    for (float dpr : {1.0f, 2.0f}) {
        settings.viewportAxesPixelRatio = dpr;
        for (const auto orientation : {QQuaternion(), QQuaternion::fromEulerAngles(27, 41, 13)}) {
            camera.setOrientation(orientation);
            const QImage reference = snapshot();
            if (reference.isNull()) return false;
            if (orientation.isIdentity()) {
                QRect bounds;
                for (int y = 0; y < reference.height(); ++y)
                    for (int x = 0; x < reference.width(); ++x)
                        if (reference.pixelColor(x, y).red() > 128 ||
                            reference.pixelColor(x, y).green() > 128 ||
                            reference.pixelColor(x, y).blue() > 128)
                            bounds |= QRect(x, y, 1, 1);
                if (std::abs(bounds.width() - 48 * dpr) > 2 ||
                    std::abs(bounds.height() - 48 * dpr) > 2 ||
                    std::abs(bounds.center().x() - reference.width() / 2) > 1 ||
                    std::abs(bounds.center().y() - reference.height() / 2) > 1) {
                    std::cerr << "GPU gizmo has incorrect size or center\n";
                    return false;
                }
            }
            for (bool perspective : {true, false}) {
                camera.setProjection(perspective);
                for (float fov : {10.0f, 45.0f, 170.0f}) {
                    camera.setFieldOfView(fov);
                    for (float scale : {0.1f, 1000.0f}) {
                        camera.setDistance(scale);
                        camera.setOrthoScale(scale);
                        camera.setTarget({100, -50, 30});
                        settings.rotationCenterX = camera.target().x();
                        settings.rotationCenterY = camera.target().y();
                        settings.rotationCenterZ = camera.target().z();
                        if (snapshot() != reference) {
                            std::cerr << "GPU gizmo changed with scene camera settings\n";
                            return false;
                        }
                    }
                }
            }
        }
    }
    return true;
}
