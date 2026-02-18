#include "BondList.h"

#include <algorithm>

namespace atom::data {

void BondList::reserve(size_t count) {
    m_bonds.reserve(count);
}

void BondList::clear() {
    m_bonds.clear();
}

size_t BondList::addBond(uint32_t atomIndex1, uint32_t atomIndex2,
                          int8_t imageX, int8_t imageY, int8_t imageZ,
                          BondOrder order)
{
    // Enforce atomIndex1 < atomIndex2; negate image shifts if swapped.
    if (atomIndex1 > atomIndex2) {
        std::swap(atomIndex1, atomIndex2);
        imageX = static_cast<int8_t>(-imageX);
        imageY = static_cast<int8_t>(-imageY);
        imageZ = static_cast<int8_t>(-imageZ);
    }

    size_t index = m_bonds.size();
    m_bonds.emplace_back(atomIndex1, atomIndex2, imageX, imageY, imageZ, order);
    return index;
}

void BondList::removeBond(size_t bondIndex) {
    if (bondIndex < m_bonds.size()) {
        m_bonds.erase(m_bonds.begin() + static_cast<std::ptrdiff_t>(bondIndex));
    }
}

int BondList::findBond(uint32_t atomIndex1, uint32_t atomIndex2,
                       int8_t imageX, int8_t imageY, int8_t imageZ) const {
    if (atomIndex1 > atomIndex2) {
        std::swap(atomIndex1, atomIndex2);
        imageX = static_cast<int8_t>(-imageX);
        imageY = static_cast<int8_t>(-imageY);
        imageZ = static_cast<int8_t>(-imageZ);
    }

    for (size_t i = 0; i < m_bonds.size(); ++i) {
        if (m_bonds[i].atomIndex1 == atomIndex1 &&
            m_bonds[i].atomIndex2 == atomIndex2 &&
            m_bonds[i].imageX     == imageX &&
            m_bonds[i].imageY     == imageY &&
            m_bonds[i].imageZ     == imageZ) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool BondList::areBonded(uint32_t atomIndex1, uint32_t atomIndex2,
                          int8_t imageX, int8_t imageY, int8_t imageZ) const {
    return findBond(atomIndex1, atomIndex2, imageX, imageY, imageZ) >= 0;
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

} // namespace atom::data
