#include "BondList.h"

#include <algorithm>

namespace atom::data {

void BondList::reserve(size_t count) {
    m_bonds.reserve(count);
    m_radii.reserve(count);
    m_selected.reserve(count);
}

void BondList::clear() {
    m_bonds.clear();
    m_radii.clear();
    m_selected.clear();
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
    m_radii.push_back(0.1f);
    m_selected.push_back(0);
    return index;
}

void BondList::removeBond(size_t bondIndex) {
    if (bondIndex < m_bonds.size()) {
        m_bonds.erase(m_bonds.begin() + static_cast<std::ptrdiff_t>(bondIndex));
        m_radii.erase(m_radii.begin() + static_cast<std::ptrdiff_t>(bondIndex));
        m_selected.erase(m_selected.begin() + static_cast<std::ptrdiff_t>(bondIndex));
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

void BondList::setRadius(size_t bondIndex, float radius) {
    if (bondIndex < m_radii.size()) {
        m_radii[bondIndex] = radius;
    }
}

void BondList::setAllRadii(float radius) {
    std::fill(m_radii.begin(), m_radii.end(), radius);
}

void BondList::setSelected(size_t bondIndex, bool selected) {
    if (bondIndex < m_selected.size()) {
        m_selected[bondIndex] = selected ? 1 : 0;
    }
}

void BondList::toggleSelected(size_t bondIndex) {
    if (bondIndex < m_selected.size()) {
        m_selected[bondIndex] = m_selected[bondIndex] ? 0 : 1;
    }
}

void BondList::clearSelection() {
    std::fill(m_selected.begin(), m_selected.end(), uint8_t{0});
}

size_t BondList::selectedCount() const {
    return static_cast<size_t>(std::count(m_selected.begin(), m_selected.end(), uint8_t{1}));
}

} // namespace atom::data
