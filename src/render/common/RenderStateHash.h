#pragma once

#include <cstdint>

namespace atom::render {

class Camera;
struct RenderSettings;

/**
 * @brief Hash camera + render settings fields that affect RT accumulation.
 *
 * Any change in the hashed state should reset progressive accumulation.
 */
uint64_t computeRenderStateHash(const Camera& camera, const RenderSettings& settings);

/**
 * @brief Hash everything that affects a raster frame's pixels.
 *
 * Superset of computeRenderStateHash: also covers overlays (unit cell,
 * viewport axes, rotation-center gizmo), mesh tessellation, and the
 * viewport size. Explicit frame requests also invalidate this hash.
 * Used to skip raster re-rendering when nothing changed.
 */
uint64_t computeRasterFrameHash(const Camera& camera, const RenderSettings& settings,
                                int width, int height);

} // namespace atom::render
