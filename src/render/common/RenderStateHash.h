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

} // namespace atom::render
