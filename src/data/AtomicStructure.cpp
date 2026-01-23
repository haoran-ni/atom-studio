#include "AtomicStructure.h"
#include "BondList.h"
#include "UnitCell.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace atom::data {

AtomicStructure::AtomicStructure()
    : m_bonds(std::make_unique<BondList>())
    , m_unitCell(std::make_unique<UnitCell>())
{
}

AtomicStructure::~AtomicStructure() = default;

AtomicStructure::AtomicStructure(AtomicStructure&&) noexcept = default;
AtomicStructure& AtomicStructure::operator=(AtomicStructure&&) noexcept = default;

void AtomicStructure::reserve(size_t count) {
    m_posX.reserve(count);
    m_posY.reserve(count);
    m_posZ.reserve(count);
    m_types.reserve(count);
    m_radii.reserve(count);
    m_colorR.reserve(count);
    m_colorG.reserve(count);
    m_colorB.reserve(count);
    m_colorA.reserve(count);
}

void AtomicStructure::resize(size_t count) {
    m_atomCount = count;
    m_posX.resize(count, 0.0f);
    m_posY.resize(count, 0.0f);
    m_posZ.resize(count, 0.0f);
    m_types.resize(count, 0);
    m_radii.resize(count, 1.0f);
    m_colorR.resize(count, 1.0f);
    m_colorG.resize(count, 1.0f);
    m_colorB.resize(count, 1.0f);
    m_colorA.resize(count, 1.0f);
}

void AtomicStructure::clear() {
    m_atomCount = 0;
    m_posX.clear();
    m_posY.clear();
    m_posZ.clear();
    m_types.clear();
    m_radii.clear();
    m_colorR.clear();
    m_colorG.clear();
    m_colorB.clear();
    m_colorA.clear();
    m_velX.clear();
    m_velY.clear();
    m_velZ.clear();
    m_bonds->clear();
}

size_t AtomicStructure::addAtom(float x, float y, float z, int type) {
    const auto& elem = ElementData::byAtomicNumber(type);
    return addAtom(x, y, z, type, elem.covalentRadius, elem.cpkColor);
}

size_t AtomicStructure::addAtom(float x, float y, float z, int type,
                                 float radius, const Color& color) {
    size_t index = m_atomCount++;
    m_posX.push_back(x);
    m_posY.push_back(y);
    m_posZ.push_back(z);
    m_types.push_back(type);
    m_radii.push_back(radius);
    m_colorR.push_back(color.r);
    m_colorG.push_back(color.g);
    m_colorB.push_back(color.b);
    m_colorA.push_back(color.a);
    return index;
}

void AtomicStructure::setPosition(size_t index, float x, float y, float z) {
    if (index < m_atomCount) {
        m_posX[index] = x;
        m_posY[index] = y;
        m_posZ[index] = z;
    }
}

void AtomicStructure::setType(size_t index, int type) {
    if (index < m_atomCount) {
        m_types[index] = type;
    }
}

void AtomicStructure::updateColorsFromTypes() {
    for (size_t i = 0; i < m_atomCount; ++i) {
        Color c = ElementData::colorForElement(m_types[i]);
        m_colorR[i] = c.r;
        m_colorG[i] = c.g;
        m_colorB[i] = c.b;
        m_colorA[i] = c.a;
    }
}

void AtomicStructure::updateRadiiFromTypes(float scale, bool useVdW) {
    for (size_t i = 0; i < m_atomCount; ++i) {
        m_radii[i] = ElementData::radiusForElement(m_types[i], useVdW) * scale;
    }
}

std::array<float, 3> AtomicStructure::position(size_t index) const {
    if (index >= m_atomCount) return {0, 0, 0};
    return {m_posX[index], m_posY[index], m_posZ[index]};
}

int AtomicStructure::type(size_t index) const {
    if (index >= m_atomCount) return 0;
    return m_types[index];
}

float AtomicStructure::radius(size_t index) const {
    if (index >= m_atomCount) return 1.0f;
    return m_radii[index];
}

Color AtomicStructure::color(size_t index) const {
    if (index >= m_atomCount) return Color();
    return Color(m_colorR[index], m_colorG[index], m_colorB[index], m_colorA[index]);
}

float AtomicStructure::BoundingBox::maxExtent() const {
    return std::max({extentX(), extentY(), extentZ()});
}

AtomicStructure::BoundingBox AtomicStructure::computeBoundingBox() const {
    BoundingBox box;
    if (m_atomCount == 0) return box;

    box.minX = box.maxX = m_posX[0];
    box.minY = box.maxY = m_posY[0];
    box.minZ = box.maxZ = m_posZ[0];

    for (size_t i = 1; i < m_atomCount; ++i) {
        box.minX = std::min(box.minX, m_posX[i]);
        box.maxX = std::max(box.maxX, m_posX[i]);
        box.minY = std::min(box.minY, m_posY[i]);
        box.maxY = std::max(box.maxY, m_posY[i]);
        box.minZ = std::min(box.minZ, m_posZ[i]);
        box.maxZ = std::max(box.maxZ, m_posZ[i]);
    }

    return box;
}

std::array<float, 3> AtomicStructure::centerOfMass() const {
    if (m_atomCount == 0) return {0, 0, 0};

    double sumX = 0, sumY = 0, sumZ = 0;
    double totalMass = 0;

    for (size_t i = 0; i < m_atomCount; ++i) {
        float mass = ElementData::byAtomicNumber(m_types[i]).mass;
        sumX += m_posX[i] * mass;
        sumY += m_posY[i] * mass;
        sumZ += m_posZ[i] * mass;
        totalMass += mass;
    }

    if (totalMass > 0) {
        return {
            static_cast<float>(sumX / totalMass),
            static_cast<float>(sumY / totalMass),
            static_cast<float>(sumZ / totalMass)
        };
    }

    // Fallback to geometric center
    auto box = computeBoundingBox();
    return {box.centerX(), box.centerY(), box.centerZ()};
}

bool AtomicStructure::hasUnitCell() const {
    return m_unitCell && m_unitCell->isDefined();
}

std::vector<float> AtomicStructure::packPositionsAndRadii() const {
    std::vector<float> packed(m_atomCount * 4);
    for (size_t i = 0; i < m_atomCount; ++i) {
        packed[i * 4 + 0] = m_posX[i];
        packed[i * 4 + 1] = m_posY[i];
        packed[i * 4 + 2] = m_posZ[i];
        packed[i * 4 + 3] = m_radii[i];
    }
    return packed;
}

std::vector<float> AtomicStructure::packColors() const {
    std::vector<float> packed(m_atomCount * 4);
    for (size_t i = 0; i < m_atomCount; ++i) {
        packed[i * 4 + 0] = m_colorR[i];
        packed[i * 4 + 1] = m_colorG[i];
        packed[i * 4 + 2] = m_colorB[i];
        packed[i * 4 + 3] = m_colorA[i];
    }
    return packed;
}

} // namespace atom::data
