#include "BondList.h"

#include <algorithm>

namespace atom::data {

void BondList::reserve(size_t count) {
    m_bonds.reserve(count);
    m_strokes.reserve(count);
    m_radii.reserve(count);
    m_startColors.reserve(count);
    m_endColors.reserve(count);
    m_selected.reserve(count);
    m_radiusOverrides.reserve(count);
    m_startOverrides.reserve(count);
    m_endOverrides.reserve(count);
}

void BondList::clear() {
    m_bonds.clear();
    m_strokes.clear();
    m_radii.clear();
    m_startColors.clear();
    m_endColors.clear();
    m_selected.clear();
    m_radiusOverrides.clear();
    m_startOverrides.clear();
    m_endOverrides.clear();
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
    m_strokes.emplace_back();
    m_radii.push_back(0.1f);
    m_startColors.emplace_back(1.0f, 1.0f, 1.0f);
    m_endColors.emplace_back(1.0f, 1.0f, 1.0f);
    m_selected.push_back(0);
    m_radiusOverrides.push_back(0);
    m_startOverrides.push_back(0);
    m_endOverrides.push_back(0);
    return index;
}

void BondList::removeBond(size_t bondIndex) {
    if (bondIndex < m_bonds.size()) {
        m_bonds.erase(m_bonds.begin() + static_cast<std::ptrdiff_t>(bondIndex));
        m_strokes.erase(m_strokes.begin() + static_cast<std::ptrdiff_t>(bondIndex));
        m_radii.erase(m_radii.begin() + static_cast<std::ptrdiff_t>(bondIndex));
        m_startColors.erase(m_startColors.begin() + static_cast<std::ptrdiff_t>(bondIndex));
        m_endColors.erase(m_endColors.begin() + static_cast<std::ptrdiff_t>(bondIndex));
        m_selected.erase(m_selected.begin() + static_cast<std::ptrdiff_t>(bondIndex));
        m_radiusOverrides.erase(m_radiusOverrides.begin() + static_cast<std::ptrdiff_t>(bondIndex));
        m_startOverrides.erase(m_startOverrides.begin() + static_cast<std::ptrdiff_t>(bondIndex));
        m_endOverrides.erase(m_endOverrides.begin() + static_cast<std::ptrdiff_t>(bondIndex));
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

void BondList::setRadius(size_t bondIndex, float radius, bool custom) {
    if (bondIndex < m_radii.size()) {
        m_radii[bondIndex] = radius;
        m_radiusOverrides[bondIndex] = custom;
    }
}

void BondList::setAllRadii(float radius, bool preserveOverrides) {
    for (size_t i = 0; i < m_radii.size(); ++i) {
        if (!preserveOverrides || !m_radiusOverrides[i]) setRadius(i, radius);
    }
}

void BondList::setStartColor(size_t bondIndex, Color color, bool custom) {
    if (bondIndex < m_startColors.size()) {
        m_startColors[bondIndex] = color;
        m_startOverrides[bondIndex] = custom;
    }
}

void BondList::setEndColor(size_t bondIndex, Color color, bool custom) {
    if (bondIndex < m_endColors.size()) {
        m_endColors[bondIndex] = color;
        m_endOverrides[bondIndex] = custom;
    }
}

void BondList::setEndpointColors(size_t bondIndex, Color startColor, Color endColor) {
    if (bondIndex < m_startColors.size() && bondIndex < m_endColors.size()) {
        m_startColors[bondIndex] = startColor;
        m_endColors[bondIndex] = endColor;
        m_startOverrides[bondIndex] = m_endOverrides[bondIndex] = 0;
    }
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
