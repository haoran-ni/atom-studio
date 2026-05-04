#pragma once

#include <cstddef>
#include <vector>

namespace atom::data {
struct Bond;
class Structure;
}

namespace atom::render {

struct BondRenderSegment {
    float startX = 0.0f;
    float startY = 0.0f;
    float startZ = 0.0f;
    float endX = 0.0f;
    float endY = 0.0f;
    float endZ = 0.0f;
    float startRadius = 0.0f;
    float endRadius = 0.0f;
    float startColorR = 0.0f;
    float startColorG = 0.0f;
    float startColorB = 0.0f;
    float startColorA = 1.0f;
    float endColorR = 0.0f;
    float endColorG = 0.0f;
    float endColorB = 0.0f;
    float endColorA = 1.0f;
    float bondRadius = 0.1f;
};

enum class BondPositionPacking {
    XYZ3,
    XYZW4
};

struct PackedBondRenderData {
    std::vector<float> startPositions;
    std::vector<float> endPositions;
    std::vector<float> startRadii;
    std::vector<float> endRadii;
    std::vector<float> startColors;
    std::vector<float> endColors;
    std::vector<float> bondRadii;
    int positionStride = 0;

    size_t bondCount() const;
    bool empty() const;
};

size_t bondRenderSegmentCount(const data::Structure* structure);
std::vector<float> packAtomRenderColors(const data::Structure* structure);
BondRenderSegment makeBondRenderSegment(const data::Structure& structure,
                                        const data::Bond& bond,
                                        size_t bondIndex);
std::vector<BondRenderSegment> collectBondRenderSegments(const data::Structure* structure);
PackedBondRenderData packBondRenderData(const data::Structure* structure,
                                        BondPositionPacking packing);

} // namespace atom::render
