#pragma once

#include <QMatrix4x4>

namespace atom::render {

class Camera;

// Pixel-space rotation indicator, centered in the render target. Only camera
// orientation affects the geometry; scene projection and translation do not.
struct GizmoOverlay {
    QMatrix4x4 viewMatrix;
    QMatrix4x4 projectionMatrix;
    float axisLength;
    float radius;
};

GizmoOverlay makeGizmoOverlay(const Camera& camera, int width, int height,
                             float pixelRatio);

} // namespace atom::render
