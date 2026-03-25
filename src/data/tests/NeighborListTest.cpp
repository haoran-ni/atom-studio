#include "BondList.h"
#include "ElementData.h"
#include "NeighborList.h"
#include "Structure.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace atom::data {
namespace {

struct BondKey {
    uint32_t atom1;
    uint32_t atom2;
    int      imageX;
    int      imageY;
    int      imageZ;

    bool operator==(const BondKey& other) const {
        return atom1 == other.atom1 &&
               atom2 == other.atom2 &&
               imageX == other.imageX &&
               imageY == other.imageY &&
               imageZ == other.imageZ;
    }

    bool operator<(const BondKey& other) const {
        return std::tie(atom1, atom2, imageX, imageY, imageZ) <
               std::tie(other.atom1, other.atom2, other.imageX, other.imageY, other.imageZ);
    }
};

void canonicalizeBond(BondKey& bond) {
    if (bond.atom1 > bond.atom2) {
        std::swap(bond.atom1, bond.atom2);
        bond.imageX = -bond.imageX;
        bond.imageY = -bond.imageY;
        bond.imageZ = -bond.imageZ;
    }
}

void applyMICReference(double& dx, double& dy, double& dz,
                       int& imageX, int& imageY, int& imageZ,
                       const Lattice& lattice)
{
    auto frac = lattice.cartesianToFractional(dx, dy, dz);

    const double ridx = lattice.pbc[0] ? std::round(frac[0]) : 0.0;
    const double ridy = lattice.pbc[1] ? std::round(frac[1]) : 0.0;
    const double ridz = lattice.pbc[2] ? std::round(frac[2]) : 0.0;

    if (ridx != 0.0 || ridy != 0.0 || ridz != 0.0) {
        imageX -= static_cast<int>(ridx);
        imageY -= static_cast<int>(ridy);
        imageZ -= static_cast<int>(ridz);

        const auto shift = lattice.fractionalToCartesian(ridx, ridy, ridz);
        dx -= shift[0];
        dy -= shift[1];
        dz -= shift[2];
    }
}

std::set<BondKey> bruteForceBonds(const Structure& structure, float scale) {
    std::set<BondKey> result;

    const float* posX = structure.positionsX();
    const float* posY = structure.positionsY();
    const float* posZ = structure.positionsZ();
    const int* atomicNumbers = structure.atomicNumbers();
    const Lattice& lattice = structure.lattice();

    constexpr double kMinBondDist = 0.4;

    for (size_t i = 0; i < structure.atomCount(); ++i) {
        const auto ri = ElementData::covalentRadius(atomicNumbers[i]);
        if (!ri.has_value()) continue;

        for (size_t j = i + 1; j < structure.atomCount(); ++j) {
            const auto rj = ElementData::covalentRadius(atomicNumbers[j]);
            if (!rj.has_value()) continue;

            double dx = static_cast<double>(posX[j]) - static_cast<double>(posX[i]);
            double dy = static_cast<double>(posY[j]) - static_cast<double>(posY[i]);
            double dz = static_cast<double>(posZ[j]) - static_cast<double>(posZ[i]);

            int imageX = 0;
            int imageY = 0;
            int imageZ = 0;
            if (lattice.defined && (lattice.pbc[0] || lattice.pbc[1] || lattice.pbc[2])) {
                applyMICReference(dx, dy, dz, imageX, imageY, imageZ, lattice);
            }

            const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            const double threshold = static_cast<double>(ri.value() + rj.value()) * scale;

            if (dist > kMinBondDist && dist < threshold) {
                BondKey bond{
                    static_cast<uint32_t>(i),
                    static_cast<uint32_t>(j),
                    imageX, imageY, imageZ
                };
                canonicalizeBond(bond);
                result.insert(bond);
            }
        }
    }

    return result;
}

std::set<BondKey> detectedBonds(const BondList& bonds) {
    std::set<BondKey> result;
    for (size_t i = 0; i < bonds.bondCount(); ++i) {
        const Bond& bond = bonds.bond(i);
        BondKey key{
            bond.atomIndex1,
            bond.atomIndex2,
            bond.imageX,
            bond.imageY,
            bond.imageZ
        };
        canonicalizeBond(key);
        result.insert(key);
    }
    return result;
}

void addAtomFractional(Structure& structure,
                       const Lattice& lattice,
                       double fx, double fy, double fz,
                       int shiftX, int shiftY, int shiftZ,
                       int atomicNumber)
{
    const auto cart = lattice.fractionalToCartesian(
        fx + static_cast<double>(shiftX),
        fy + static_cast<double>(shiftY),
        fz + static_cast<double>(shiftZ));
    structure.addAtom(
        static_cast<float>(cart[0]),
        static_cast<float>(cart[1]),
        static_cast<float>(cart[2]),
        atomicNumber);
}

bool runCase(const std::string& name, const Structure& structure, float scale) {
    NeighborList nl;
    nl.build(structure, scale);
    const auto bonds = nl.buildBondList(structure, scale);

    const auto expected = bruteForceBonds(structure, scale);
    const auto actual = detectedBonds(*bonds);

    if (expected != actual) {
        std::cerr << name << " failed\n";
        std::cerr << "Expected bonds:\n";
        for (const auto& bond : expected) {
            std::cerr << "  (" << bond.atom1 << ", " << bond.atom2 << ")"
                      << " img=(" << bond.imageX << ", " << bond.imageY << ", " << bond.imageZ << ")\n";
        }
        std::cerr << "Actual bonds:\n";
        for (const auto& bond : actual) {
            std::cerr << "  (" << bond.atom1 << ", " << bond.atom2 << ")"
                      << " img=(" << bond.imageX << ", " << bond.imageY << ", " << bond.imageZ << ")\n";
        }
        return false;
    }

    return true;
}

Structure makeFullPbcSkewedStructure() {
    Structure structure;
    auto& lattice = structure.lattice();
    lattice.defined = true;
    lattice.matrix = {{
        {{10.0, 0.0, 0.0}},
        {{8.5, 3.0, 0.0}},
        {{1.5, 0.5, 9.0}},
    }};
    lattice.pbc = {true, true, true};

    addAtomFractional(structure, lattice, 0.90, 0.10, 0.10, 0, 0, 0, 6);
    addAtomFractional(structure, lattice, 0.10, 0.90, 0.10, 0, 0, 0, 6);

    addAtomFractional(structure, lattice, 0.97, 0.20, 0.20, 0, 0, 0, 6);
    addAtomFractional(structure, lattice, 0.09, 0.20, 0.20, 1, 0, 0, 6);

    addAtomFractional(structure, lattice, 0.20, 0.25, 0.95, 0, 0, 0, 6);
    addAtomFractional(structure, lattice, 0.20, 0.25, 0.05, 0, 0, 1, 6);

    addAtomFractional(structure, lattice, 0.45, 0.45, 0.45, 0, 0, 0, 6);
    addAtomFractional(structure, lattice, 0.75, 0.75, 0.75, 0, 0, 0, 2);

    return structure;
}

Structure makePartialPbcSkewedStructure() {
    Structure structure;
    auto& lattice = structure.lattice();
    lattice.defined = true;
    lattice.matrix = {{
        {{8.0, 0.0, 0.0}},
        {{3.5, 6.0, 0.0}},
        {{1.0, 1.5, 12.0}},
    }};
    lattice.pbc = {true, true, false};

    addAtomFractional(structure, lattice, 0.94, 0.10, 0.20, 0, 0, 0, 6);
    addAtomFractional(structure, lattice, 0.06, 0.90, 0.20, 0, 0, 0, 6);

    addAtomFractional(structure, lattice, 0.97, 0.35, 0.55, 0, 0, 0, 6);
    addAtomFractional(structure, lattice, 0.12, 0.35, 0.55, 1, 0, 0, 6);

    addAtomFractional(structure, lattice, 0.25, 0.25, 0.05, 0, 0, 0, 6);
    addAtomFractional(structure, lattice, 0.25, 0.25, 0.18, 0, 0, 0, 6);

    addAtomFractional(structure, lattice, 0.50, 0.50, 0.80, 0, 0, 0, 10);

    return structure;
}

} // namespace
} // namespace atom::data

int main() {
    constexpr float kScale = 1.10f;

    bool ok = true;
    ok = atom::data::runCase("full-pbc-skewed", atom::data::makeFullPbcSkewedStructure(), kScale) && ok;
    ok = atom::data::runCase("partial-pbc-skewed", atom::data::makePartialPbcSkewedStructure(), kScale) && ok;

    if (!ok) return 1;

    std::cout << "NeighborList skewed-cell tests passed\n";
    return 0;
}
