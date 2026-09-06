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
                       float r, float g, float b) {
    packed.push_back(r);
    packed.push_back(g);
    packed.push_back(b);
    packed.push_back(1.0f);  // Match four-component GPU color layouts.
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

std::vector<float> packAtomRenderColors(const data::Structure* structure) {
    std::vector<float> data;
    if (!structure) return data;

    const size_t count = structure->atomCount();
    data.resize(count * 4);

    const float* cr = structure->colorsR();
    const float* cg = structure->colorsG();
    const float* cb = structure->colorsB();

    for (size_t i = 0; i < count; ++i) {
        data[i * 4 + 0] = cr[i];
        data[i * 4 + 1] = cg[i];
        data[i * 4 + 2] = cb[i];
        data[i * 4 + 3] = 1.0f;
    }

    return data;
}

std::vector<uint32_t> packAtomSelectionMask(const data::Structure* structure) {
    std::vector<uint32_t> data;
    if (!structure) return data;

    const size_t count = structure->atomCount();
    data.resize(count);
    const auto& selectedAtoms = structure->atomSelectionMask();
    for (size_t i = 0; i < count; ++i) {
        data[i] = (i < selectedAtoms.size() && selectedAtoms[i]) ? 1u : 0u;
    }
    return data;
}

std::vector<uint32_t> packBondSelectionMask(const data::Structure* structure) {
    std::vector<uint32_t> data;
    const size_t count = bondRenderSegmentCount(structure);
    if (count == 0) return data;

    data.resize(count);
    const auto& selectedBonds = structure->bonds().selectionMask();
    for (size_t i = 0; i < count; ++i) {
        data[i] = (i < selectedBonds.size() && selectedBonds[i]) ? 1u : 0u;
    }
    return data;
}

void packBondRenderColors(const data::Structure* structure,
                          std::vector<float>& startColors,
                          std::vector<float>& endColors) {
    startColors.clear();
    endColors.clear();

    const size_t count = bondRenderSegmentCount(structure);
    if (count == 0) return;

    startColors.reserve(count * 4);
    endColors.reserve(count * 4);

    const auto& bonds = structure->bonds();
    for (size_t i = 0; i < count; ++i) {
        const data::Color storedStartColor = bonds.startColor(i);
        const data::Color storedEndColor = bonds.endColor(i);
        appendPackedColor(startColors, storedStartColor.r, storedStartColor.g, storedStartColor.b);
        appendPackedColor(endColors, storedEndColor.r, storedEndColor.g, storedEndColor.b);
    }
}

BondRenderSegment makeBondRenderSegment(const data::Structure& structure,
                                        const data::Bond& bond,
                                        size_t bondIndex) {
    BondRenderSegment segment;

    const uint32_t a1 = bond.atomIndex1;
    const uint32_t a2 = bond.atomIndex2;

    const float* px = structure.positionsX();
    const float* py = structure.positionsY();
    const float* pz = structure.positionsZ();
    const float* radii = structure.radii();
    const auto& mat = structure.lattice().matrix;
    const auto& bonds = structure.bonds();

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
    const data::Color storedStartColor = bonds.startColor(bondIndex);
    const data::Color storedEndColor = bonds.endColor(bondIndex);
    segment.startColorR = storedStartColor.r;
    segment.startColorG = storedStartColor.g;
    segment.startColorB = storedStartColor.b;
    segment.endColorR = storedEndColor.r;
    segment.endColorG = storedEndColor.g;
    segment.endColorB = storedEndColor.b;
    segment.bondRadius = bonds.radius(bondIndex);
    segment.selected = (bondIndex < bonds.bondCount() && bonds.selected(bondIndex)) ? 1.0f : 0.0f;

    return segment;
}

std::vector<BondRenderSegment> collectBondRenderSegments(const data::Structure* structure) {
    std::vector<BondRenderSegment> segments;
    const size_t count = bondRenderSegmentCount(structure);
    if (count == 0 || !structure) return segments;

    segments.reserve(count);
    const auto& bonds = structure->bonds();
    for (size_t i = 0; i < count; ++i) {
        segments.push_back(makeBondRenderSegment(*structure, bonds.bond(i), i));
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
    packed.bondRadii.reserve(count);
    packed.selected.reserve(count);

    for (const BondRenderSegment& segment : segments) {
        appendPackedPosition(packed.startPositions, packing,
                             segment.startX, segment.startY, segment.startZ, segment.startRadius);
        appendPackedPosition(packed.endPositions, packing,
                             segment.endX, segment.endY, segment.endZ, segment.endRadius);
        packed.startRadii.push_back(segment.startRadius);
        packed.endRadii.push_back(segment.endRadius);
        appendPackedColor(packed.startColors,
                          segment.startColorR, segment.startColorG, segment.startColorB);
        appendPackedColor(packed.endColors,
                          segment.endColorR, segment.endColorG, segment.endColorB);
        packed.bondRadii.push_back(segment.bondRadius);
        packed.selected.push_back(segment.selected);
    }

    return packed;
}

} // namespace atom::render
