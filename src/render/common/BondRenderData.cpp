#include "BondRenderData.h"

#include "../../data/BondList.h"
#include "../../data/Structure.h"

namespace atom::render {

namespace {

void appendPackedPosition(std::vector<float>& packed,
                          BondPositionPacking packing,
                          float x, float y, float z, float w) {
    packed.push_back(x);
    packed.push_back(y);
    packed.push_back(z);
    if (packing == BondPositionPacking::XYZW4) {
        packed.push_back(w);
    }
}

void appendPackedColor(std::vector<float>& packed,
                       float r, float g, float b, float a) {
    packed.push_back(r);
    packed.push_back(g);
    packed.push_back(b);
    packed.push_back(a);
}

} // namespace

size_t PackedBondRenderData::bondCount() const {
    if (positionStride <= 0) return 0;
    return startPositions.size() / static_cast<size_t>(positionStride);
}

bool PackedBondRenderData::empty() const {
    return bondCount() == 0;
}

size_t bondRenderSegmentCount(const data::Structure* structure) {
    if (!structure) return 0;
    return structure->bonds().bondCount();
}

BondRenderSegment makeBondRenderSegment(const data::Structure& structure,
                                        const data::Bond& bond) {
    BondRenderSegment segment;

    const uint32_t a1 = bond.atomIndex1;
    const uint32_t a2 = bond.atomIndex2;

    const float* px = structure.positionsX();
    const float* py = structure.positionsY();
    const float* pz = structure.positionsZ();
    const float* cr = structure.colorsR();
    const float* cg = structure.colorsG();
    const float* cb = structure.colorsB();
    const float* radii = structure.radii();
    const auto& mat = structure.lattice().matrix;

    segment.startX = px[a1];
    segment.startY = py[a1];
    segment.startZ = pz[a1];

    float endX = px[a2];
    float endY = py[a2];
    float endZ = pz[a2];
    if (bond.imageX != 0 || bond.imageY != 0 || bond.imageZ != 0) {
        endX += static_cast<float>(bond.imageX * mat[0][0] + bond.imageY * mat[1][0] + bond.imageZ * mat[2][0]);
        endY += static_cast<float>(bond.imageX * mat[0][1] + bond.imageY * mat[1][1] + bond.imageZ * mat[2][1]);
        endZ += static_cast<float>(bond.imageX * mat[0][2] + bond.imageY * mat[1][2] + bond.imageZ * mat[2][2]);
    }
    segment.endX = endX;
    segment.endY = endY;
    segment.endZ = endZ;

    segment.startRadius = radii[a1];
    segment.endRadius = radii[a2];
    segment.startColorR = cr[a1];
    segment.startColorG = cg[a1];
    segment.startColorB = cb[a1];
    segment.endColorR = cr[a2];
    segment.endColorG = cg[a2];
    segment.endColorB = cb[a2];

    return segment;
}

std::vector<BondRenderSegment> collectBondRenderSegments(const data::Structure* structure) {
    std::vector<BondRenderSegment> segments;
    const size_t count = bondRenderSegmentCount(structure);
    if (count == 0 || !structure) return segments;

    segments.reserve(count);
    const auto& bonds = structure->bonds();
    for (size_t i = 0; i < count; ++i) {
        segments.push_back(makeBondRenderSegment(*structure, bonds.bond(i)));
    }
    return segments;
}

PackedBondRenderData packBondRenderData(const data::Structure* structure,
                                        BondPositionPacking packing) {
    PackedBondRenderData packed;
    packed.positionStride = (packing == BondPositionPacking::XYZW4) ? 4 : 3;

    const std::vector<BondRenderSegment> segments = collectBondRenderSegments(structure);
    if (segments.empty()) return packed;

    const size_t count = segments.size();
    packed.startPositions.reserve(count * static_cast<size_t>(packed.positionStride));
    packed.endPositions.reserve(count * static_cast<size_t>(packed.positionStride));
    packed.startRadii.reserve(count);
    packed.endRadii.reserve(count);
    packed.startColors.reserve(count * 4);
    packed.endColors.reserve(count * 4);

    for (const BondRenderSegment& segment : segments) {
        appendPackedPosition(packed.startPositions, packing,
                             segment.startX, segment.startY, segment.startZ, segment.startRadius);
        appendPackedPosition(packed.endPositions, packing,
                             segment.endX, segment.endY, segment.endZ, segment.endRadius);
        packed.startRadii.push_back(segment.startRadius);
        packed.endRadii.push_back(segment.endRadius);
        appendPackedColor(packed.startColors,
                          segment.startColorR, segment.startColorG, segment.startColorB, segment.startColorA);
        appendPackedColor(packed.endColors,
                          segment.endColorR, segment.endColorG, segment.endColorB, segment.endColorA);
    }

    return packed;
}

} // namespace atom::render
