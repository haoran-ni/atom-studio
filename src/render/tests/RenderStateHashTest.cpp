#include "common/Camera.h"
#include "common/RenderSettings.h"
#include "common/RenderStateHash.h"

#include <iostream>

int main() {
    atom::render::Camera camera;
    atom::render::RenderSettings opaque;
    opaque.backgroundColor = QColor(40, 80, 120, 255);
    auto translucent = opaque;
    translucent.backgroundColor.setAlpha(64);

    if (atom::render::computeRenderStateHash(camera, opaque)
        != atom::render::computeRenderStateHash(camera, translucent)) {
        std::cerr << "Background opacity must preserve ray-tracing accumulation\n";
        return 1;
    }
    for (const QColor color : {QColor(41, 80, 120), QColor(40, 81, 120), QColor(40, 80, 121)}) {
        auto recolored = opaque;
        recolored.backgroundColor = color;
        if (atom::render::computeRenderStateHash(camera, opaque)
            != atom::render::computeRenderStateHash(camera, recolored)
            || atom::render::computeRasterFrameHash(camera, opaque, 800, 600)
            == atom::render::computeRasterFrameHash(camera, recolored, 800, 600)) {
            std::cerr << "Background RGB must only invalidate display/raster output\n";
            return 1;
        }
    }
    auto relit = opaque;
    relit.lightAzimuth += 10.0f;
    if (atom::render::computeRenderStateHash(camera, opaque)
        == atom::render::computeRenderStateHash(camera, relit)) {
        std::cerr << "Lighting changes must still restart ray tracing\n";
        return 1;
    }
    if (atom::render::computeRasterFrameHash(camera, opaque, 800, 600)
        == atom::render::computeRasterFrameHash(camera, translucent, 800, 600)) {
        std::cerr << "Background opacity must invalidate the cached raster frame\n";
        return 1;
    }
    auto exportRequest = opaque;
    exportRequest.frameRequestToken = 1;
    if (atom::render::computeRasterFrameHash(camera, opaque, 800, 600)
        == atom::render::computeRasterFrameHash(camera, exportRequest, 800, 600)) {
        std::cerr << "An explicit frame request must refresh the raster output\n";
        return 1;
    }
    if (atom::render::computeRenderStateHash(camera, opaque)
        != atom::render::computeRenderStateHash(camera, exportRequest)) {
        std::cerr << "An explicit frame request must preserve ray-tracing accumulation\n";
        return 1;
    }
    return 0;
}
