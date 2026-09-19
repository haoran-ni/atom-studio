#include "GizmoOverlay.h"
#include "Camera.h"

#include <algorithm>

namespace atom::render {

GizmoOverlay makeGizmoOverlay(const Camera& camera, int width, int height,
                             float pixelRatio) {
    GizmoOverlay overlay;
    overlay.axisLength = 24.0f * std::max(pixelRatio, 1.0f);
    overlay.radius = overlay.axisLength * 0.05f;
    // Construct rotation directly so large world coordinates and camera zoom
    // cannot introduce translation or numerical drift into this UI overlay.
    overlay.viewMatrix.rotate(camera.orientation().conjugated());
    const float halfWidth = std::max(width, 1) * 0.5f;
    const float halfHeight = std::max(height, 1) * 0.5f;
    const float depthRange = overlay.axisLength * 4.0f;
    overlay.projectionMatrix.ortho(-halfWidth, halfWidth, -halfHeight, halfHeight,
                                   -depthRange, depthRange);
    return overlay;
}

} // namespace atom::render
