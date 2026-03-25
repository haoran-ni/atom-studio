#include "StructureOperations.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace atom::data {
namespace {

bool nearlyEqual(double a, double b, double eps = 1e-6) {
    return std::abs(a - b) <= eps;
}

bool checkWrappedAnchorPreserved() {
    Structure s;
    auto& lat = s.lattice();
    lat.defined = true;
    lat.matrix = {{
        {{10.0, 0.0, 0.0}},
        {{0.0, 10.0, 0.0}},
        {{0.0, 0.0, 10.0}},
    }};
    lat.pbc = {true, true, true};

    const std::vector<double> fracX = {0.95, 0.96, 0.97, 0.20, 0.21};
    for (double fx : fracX) {
        s.addAtom(static_cast<float>(fx * 10.0), 5.0f, 5.0f, 6);
    }

    BondList bonds;
    bonds.addBond(0, 1, 0, 0, 0);
    bonds.addBond(1, 2, 0, 0, 0);
    bonds.addBond(2, 3, 1, 0, 0);
    bonds.addBond(3, 4, 0, 0, 0);
    s.setBondList(std::make_shared<BondList>(bonds));

    unwrapMolecules(s);

    const double expectedX[] = {9.5, 9.6, 9.7, 12.0, 12.1};
    for (size_t i = 0; i < 5; ++i) {
        if (!nearlyEqual(s.positionsX()[i], expectedX[i])) {
            std::cerr << "Largest wrapped fragment was not kept in-cell for atom "
                      << i << ": expected " << expectedX[i]
                      << ", got " << s.positionsX()[i] << "\n";
            return false;
        }
    }

    return true;
}

bool checkInconsistentCycleDoesNotMoveAtoms() {
    Structure s;
    auto& lat = s.lattice();
    lat.defined = true;
    lat.matrix = {{
        {{10.0, 0.0, 0.0}},
        {{0.0, 10.0, 0.0}},
        {{0.0, 0.0, 10.0}},
    }};
    lat.pbc = {true, true, true};

    s.addAtom(1.0f, 1.0f, 1.0f, 6);
    s.addAtom(2.0f, 1.0f, 1.0f, 6);
    s.addAtom(3.0f, 1.0f, 1.0f, 6);

    const std::vector<float> beforeX = {s.positionsX()[0], s.positionsX()[1], s.positionsX()[2]};
    const std::vector<float> beforeY = {s.positionsY()[0], s.positionsY()[1], s.positionsY()[2]};
    const std::vector<float> beforeZ = {s.positionsZ()[0], s.positionsZ()[1], s.positionsZ()[2]};

    BondList bonds;
    bonds.addBond(0, 1, 0, 0, 0);
    bonds.addBond(1, 2, 0, 0, 0);
    bonds.addBond(0, 2, 1, 0, 0);
    s.setBondList(std::make_shared<BondList>(bonds));

    unwrapMolecules(s);

    for (size_t i = 0; i < 3; ++i) {
        if (!nearlyEqual(s.positionsX()[i], beforeX[i]) ||
            !nearlyEqual(s.positionsY()[i], beforeY[i]) ||
            !nearlyEqual(s.positionsZ()[i], beforeZ[i])) {
            std::cerr << "Inconsistent cyclic bond images should leave component unchanged\n";
            return false;
        }
    }

    return true;
}

bool checkNonOrganicElementsAlsoUnwrap() {
    Structure s;
    auto& lat = s.lattice();
    lat.defined = true;
    lat.matrix = {{
        {{10.0, 0.0, 0.0}},
        {{0.0, 10.0, 0.0}},
        {{0.0, 0.0, 10.0}},
    }};
    lat.pbc = {true, true, true};

    const std::vector<double> fracX = {0.96, 0.97, 0.15};
    const int atomicNumbers[] = {26, 29, 30};
    for (size_t i = 0; i < fracX.size(); ++i) {
        s.addAtom(static_cast<float>(fracX[i] * 10.0), 5.0f, 5.0f, atomicNumbers[i]);
    }

    BondList bonds;
    bonds.addBond(0, 1, 0, 0, 0);
    bonds.addBond(1, 2, 1, 0, 0);
    s.setBondList(std::make_shared<BondList>(bonds));

    unwrapMolecules(s);

    const double expectedX[] = {9.6, 9.7, 11.5};
    for (size_t i = 0; i < 3; ++i) {
        if (!nearlyEqual(s.positionsX()[i], expectedX[i])) {
            std::cerr << "Elements with VdW radius should unwrap regardless of organic classification\n";
            return false;
        }
    }

    return true;
}

} // namespace
} // namespace atom::data

int main() {
    bool ok = true;

    ok = atom::data::checkWrappedAnchorPreserved() && ok;
    ok = atom::data::checkInconsistentCycleDoesNotMoveAtoms() && ok;
    ok = atom::data::checkNonOrganicElementsAlsoUnwrap() && ok;

    if (!ok) return 1;

    std::cout << "StructureOperations unwrap tests passed\n";
    return 0;
}
