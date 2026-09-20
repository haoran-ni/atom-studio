#pragma once

#include "ElementData.h"

namespace atom::data {

// Persistent object appearance, separate from the temporary selection highlight.
// Negative values inherit the corresponding global structure stroke setting.
struct StrokeStyle {
    Color color{-1.0f, -1.0f, -1.0f};
    float width = -1.0f;

    bool overridden() const { return width >= 0.0f || color.r >= 0.0f; }
};

} // namespace atom::data
