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
    combine(hb(settings.enableShadows));
    combine(hf(settings.shadowOpacity));
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

    return h;
}

} // namespace atom::render
