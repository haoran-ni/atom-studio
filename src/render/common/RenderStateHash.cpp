#include "RenderStateHash.h"
#include "Camera.h"
#include "RenderSettings.h"
#include <functional>

namespace atom::render {

uint64_t computeRenderStateHash(const Camera& camera, const RenderSettings& settings) {
    std::hash<float> hf;
    std::hash<int> hi;
    std::hash<bool> hb;

    uint64_t h = 0;
    auto combine = [&](uint64_t val) {
        h ^= val + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    };

    // Camera
    QQuaternion q = camera.orientation();
    combine(hf(q.scalar()));
    combine(hf(q.x()));
    combine(hf(q.y()));
    combine(hf(q.z()));
    combine(hf(camera.distance()));
    combine(hf(camera.target().x()));
    combine(hf(camera.target().y()));
    combine(hf(camera.target().z()));
    combine(hf(camera.fieldOfView()));
    combine(hf(camera.aspectRatio()));
    combine(hb(camera.isPerspective()));
    combine(hf(camera.orthoScale()));

    // Settings that affect the image
    combine(hf(settings.atomScale));
    combine(hf(settings.bondRadius));
    combine(hb(settings.showBonds));
    combine(hb(settings.enableShadows));
    combine(hb(settings.enableAmbientOcclusion));
    combine(hi(settings.aoSamples));
    combine(hf(settings.aoRadius));
    combine(hf(settings.ambientStrength));
    combine(hf(settings.diffuseStrength));
    combine(hf(settings.specularStrength));
    combine(hf(settings.shininess));
    combine(hf(settings.lightAzimuth));
    combine(hf(settings.lightElevation));
    combine(hi(settings.backgroundColor.red()));
    combine(hi(settings.backgroundColor.green()));
    combine(hi(settings.backgroundColor.blue()));
    combine(hi(settings.backgroundColor.alpha()));
    combine(hb(settings.outlineEnabled));
    combine(hf(settings.outlineWidth));
    combine(hi(settings.outlineColor.red()));
    combine(hi(settings.outlineColor.green()));
    combine(hi(settings.outlineColor.blue()));

    return h;
}

uint64_t computeRasterFrameHash(const Camera& camera, const RenderSettings& settings,
                                int width, int height) {
    std::hash<float> hf;
    std::hash<int> hi;
    std::hash<bool> hb;

    uint64_t h = computeRenderStateHash(camera, settings);
    auto combine = [&](uint64_t val) {
        h ^= val + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    };

    // Viewport size
    combine(hi(width));
    combine(hi(height));

    // Visibility/tessellation not covered by the RT accumulation hash
    combine(hb(settings.showAtoms));
    combine(hi(settings.cylinderSegments));

    // Unit cell overlay
    combine(hb(settings.showUnitCell));
    combine(hf(settings.unitCellThickness));
    combine(hi(settings.unitCellColor.red()));
    combine(hi(settings.unitCellColor.green()));
    combine(hi(settings.unitCellColor.blue()));

    // Viewport axes overlay
    combine(hb(settings.showViewportAxes));
    combine(hf(settings.viewportAxesScreenX));
    combine(hf(settings.viewportAxesScreenY));
    combine(hf(settings.viewportAxesScale));
    combine(hf(settings.viewportAxesPixelRatio));

    // Rotation-center gizmo
    combine(hb(settings.showRotationCenter));
    combine(hf(settings.rotationCenterX));
    combine(hf(settings.rotationCenterY));
    combine(hf(settings.rotationCenterZ));

    return h;
}

} // namespace atom::render
