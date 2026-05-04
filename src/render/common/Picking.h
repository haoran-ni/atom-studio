#pragma once

#include <cstddef>

namespace atom::data {
class Structure;
}

namespace atom::render {

class Camera;
struct RenderSettings;

enum class PickObjectType {
    None,
    Atom,
    Bond
};

struct PickResult {
    PickObjectType type = PickObjectType::None;
    size_t index = 0;
    float rayDistance = 0.0f;

    bool hit() const { return type != PickObjectType::None; }
};

PickResult pickStructureObject(const data::Structure* structure,
                               const Camera& camera,
                               const RenderSettings& settings,
                               float screenX,
                               float screenY,
                               int viewportWidth,
                               int viewportHeight);

} // namespace atom::render
