#include "BondList.h"
#include "ElementData.h"

#include <algorithm>
#include <cmath>

namespace atom::data {

void BondList::reserve(size_t count) {
    m_bonds.reserve(count);
}

void BondList::clear() {
    m_bonds.clear();
}

size_t BondList::addBond(uint32_t atomIndex1, uint32_t atomIndex2, BondOrder order) {
    // Always store with smaller index first for consistent lookup
    if (atomIndex1 > atomIndex2) {
        std::swap(atomIndex1, atomIndex2);
    }

    size_t index = m_bonds.size();
    m_bonds.emplace_back(atomIndex1, atomIndex2, order);
    return index;
}

void BondList::removeBond(size_t bondIndex) {
    if (bondIndex < m_bonds.size()) {
        m_bonds.erase(m_bonds.begin() + bondIndex);
    }
}

int BondList::findBond(uint32_t atomIndex1, uint32_t atomIndex2) const {
    // Normalize order
    if (atomIndex1 > atomIndex2) {
        std::swap(atomIndex1, atomIndex2);
    }

    for (size_t i = 0; i < m_bonds.size(); ++i) {
        if (m_bonds[i].atomIndex1 == atomIndex1 &&
            m_bonds[i].atomIndex2 == atomIndex2) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool BondList::areBonded(uint32_t atomIndex1, uint32_t atomIndex2) const {
    return findBond(atomIndex1, atomIndex2) >= 0;
}

const uint32_t* BondList::atomIndices1() const {
    if (m_bonds.empty()) return nullptr;
    return &m_bonds[0].atomIndex1;
}

const uint32_t* BondList::atomIndices2() const {
    if (m_bonds.empty()) return nullptr;
    return &m_bonds[0].atomIndex2;
}

std::vector<size_t> BondList::bondsForAtom(uint32_t atomIndex) const {
    std::vector<size_t> result;
    for (size_t i = 0; i < m_bonds.size(); ++i) {
        if (m_bonds[i].atomIndex1 == atomIndex ||
            m_bonds[i].atomIndex2 == atomIndex) {
            result.push_back(i);
        }
    }
    return result;
}

void BondList::detectBonds(const float* posX, const float* posY, const float* posZ,
                           const int* types, size_t atomCount, float tolerance) {
    clear();

    if (atomCount == 0) return;

    // Simple O(n^2) algorithm - suitable for small structures
    // For large structures (>10k atoms), use spatial hashing
    const float maxBondDist = 4.0f; // Maximum possible bond distance

    for (size_t i = 0; i < atomCount; ++i) {
        float r1 = ElementData::radiusForElement(types[i], false);

        for (size_t j = i + 1; j < atomCount; ++j) {
            float dx = posX[j] - posX[i];
            float dy = posY[j] - posY[i];
            float dz = posZ[j] - posZ[i];
            float dist2 = dx * dx + dy * dy + dz * dz;

            // Quick rejection
            if (dist2 > maxBondDist * maxBondDist) continue;

            float dist = std::sqrt(dist2);
            float r2 = ElementData::radiusForElement(types[j], false);

            // Bond exists if distance < sum of covalent radii * tolerance
            float bondThreshold = (r1 + r2) * tolerance;
            if (dist < bondThreshold && dist > 0.4f) { // Minimum bond length
                addBond(static_cast<uint32_t>(i), static_cast<uint32_t>(j));
            }
        }
    }
}

} // namespace atom::data
