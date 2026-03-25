#include "Structure.h"

#include <cmath>
#include <iostream>

namespace atom::data {
namespace {

bool nearlyEqual(float a, float b, float eps = 1e-5f) {
    return std::abs(a - b) <= eps;
}

bool checkGeometricCenterWithoutLattice() {
    Structure s;
    s.addAtom(0.0f, 0.0f, 0.0f, 6);
    s.addAtom(2.0f, 4.0f, 6.0f, 8);

    const auto center = s.geometricCenter();
    if (!nearlyEqual(center[0], 1.0f) ||
        !nearlyEqual(center[1], 2.0f) ||
        !nearlyEqual(center[2], 3.0f)) {
        std::cerr << "Geometric center should be the arithmetic mean of atom positions\n";
        return false;
    }

    const auto viewBox = s.computeViewBoundingBox();
    if (!nearlyEqual(viewBox.minX, 0.0f) ||
        !nearlyEqual(viewBox.maxX, 2.0f) ||
        !nearlyEqual(viewBox.minY, 0.0f) ||
        !nearlyEqual(viewBox.maxY, 4.0f) ||
        !nearlyEqual(viewBox.minZ, 0.0f) ||
        !nearlyEqual(viewBox.maxZ, 6.0f)) {
        std::cerr << "Non-periodic view bounds should match atom bounds\n";
        return false;
    }

    return true;
}

bool checkUnitCellCenterWithLattice() {
    Structure s;
    auto& lattice = s.lattice();
    lattice.defined = true;
    lattice.matrix = {{
        {{8.0, 0.0, 0.0}},
        {{2.0, 6.0, 0.0}},
        {{1.0, 1.5, 10.0}},
    }};

    const auto center = s.unitCellCenter();
    if (!nearlyEqual(center[0], 5.5f) ||
        !nearlyEqual(center[1], 3.75f) ||
        !nearlyEqual(center[2], 5.0f)) {
        std::cerr << "Unit-cell center should be at fractional coordinate (0.5, 0.5, 0.5)\n";
        return false;
    }

    const auto cellBox = s.computeUnitCellBoundingBox();
    if (!nearlyEqual(cellBox.minX, 0.0f) ||
        !nearlyEqual(cellBox.maxX, 11.0f) ||
        !nearlyEqual(cellBox.minY, 0.0f) ||
        !nearlyEqual(cellBox.maxY, 7.5f) ||
        !nearlyEqual(cellBox.minZ, 0.0f) ||
        !nearlyEqual(cellBox.maxZ, 10.0f)) {
        std::cerr << "Unit-cell bounds should enclose all eight lattice corners\n";
        return false;
    }

    return true;
}

bool checkViewBoundsIncludeAtomsAndCell() {
    Structure s;
    auto& lattice = s.lattice();
    lattice.defined = true;
    lattice.matrix = {{
        {{10.0, 0.0, 0.0}},
        {{0.0, 10.0, 0.0}},
        {{0.0, 0.0, 10.0}},
    }};

    s.addAtom(15.0f, 5.0f, 5.0f, 6);

    const auto viewBox = s.computeViewBoundingBox();
    if (!nearlyEqual(viewBox.minX, 0.0f) ||
        !nearlyEqual(viewBox.maxX, 15.0f) ||
        !nearlyEqual(viewBox.minY, 0.0f) ||
        !nearlyEqual(viewBox.maxY, 10.0f) ||
        !nearlyEqual(viewBox.minZ, 0.0f) ||
        !nearlyEqual(viewBox.maxZ, 10.0f)) {
        std::cerr << "Periodic view bounds should cover both the unit cell and atom positions\n";
        return false;
    }

    return true;
}

} // namespace
} // namespace atom::data

int main() {
    bool ok = true;

    ok = atom::data::checkGeometricCenterWithoutLattice() && ok;
    ok = atom::data::checkUnitCellCenterWithLattice() && ok;
    ok = atom::data::checkViewBoundsIncludeAtomsAndCell() && ok;

    if (!ok) return 1;

    std::cout << "Structure geometry tests passed\n";
    return 0;
}
