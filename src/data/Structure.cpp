#include "Structure.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace atom::data {

// ============================================================================
// Lattice implementation
// ============================================================================

double Lattice::a() const {
    const auto& v = matrix[0];
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

double Lattice::b() const {
    const auto& v = matrix[1];
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

double Lattice::c() const {
    const auto& v = matrix[2];
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

double Lattice::alpha() const {
    // Angle between b and c
    const auto& vb = matrix[1];
    const auto& vc = matrix[2];
    double dot = vb[0] * vc[0] + vb[1] * vc[1] + vb[2] * vc[2];
    double len_b = b();
    double len_c = c();
    if (len_b < 1e-10 || len_c < 1e-10) return 90.0;
    double cos_angle = dot / (len_b * len_c);
    cos_angle = std::clamp(cos_angle, -1.0, 1.0);
    return std::acos(cos_angle) * 180.0 / M_PI;
}

double Lattice::beta() const {
    // Angle between a and c
    const auto& va = matrix[0];
    const auto& vc = matrix[2];
    double dot = va[0] * vc[0] + va[1] * vc[1] + va[2] * vc[2];
    double len_a = a();
    double len_c = c();
    if (len_a < 1e-10 || len_c < 1e-10) return 90.0;
    double cos_angle = dot / (len_a * len_c);
    cos_angle = std::clamp(cos_angle, -1.0, 1.0);
    return std::acos(cos_angle) * 180.0 / M_PI;
}

double Lattice::gamma() const {
    // Angle between a and b
    const auto& va = matrix[0];
    const auto& vb = matrix[1];
    double dot = va[0] * vb[0] + va[1] * vb[1] + va[2] * vb[2];
    double len_a = a();
    double len_b = b();
    if (len_a < 1e-10 || len_b < 1e-10) return 90.0;
    double cos_angle = dot / (len_a * len_b);
    cos_angle = std::clamp(cos_angle, -1.0, 1.0);
    return std::acos(cos_angle) * 180.0 / M_PI;
}

double Lattice::volume() const {
    // V = a . (b x c)
    const auto& va = matrix[0];
    const auto& vb = matrix[1];
    const auto& vc = matrix[2];

    // b x c
    double cx = vb[1] * vc[2] - vb[2] * vc[1];
    double cy = vb[2] * vc[0] - vb[0] * vc[2];
    double cz = vb[0] * vc[1] - vb[1] * vc[0];

    return std::abs(va[0] * cx + va[1] * cy + va[2] * cz);
}

void Lattice::setFromParameters(double a_len, double b_len, double c_len,
                                 double alpha_deg, double beta_deg, double gamma_deg) {
    // Convert angles to radians
    const double deg2rad = M_PI / 180.0;
    double alpha_rad = alpha_deg * deg2rad;
    double beta_rad = beta_deg * deg2rad;
    double gamma_rad = gamma_deg * deg2rad;

    double cos_alpha = std::cos(alpha_rad);
    double cos_beta = std::cos(beta_rad);
    double cos_gamma = std::cos(gamma_rad);
    double sin_gamma = std::sin(gamma_rad);

    // Build lattice vectors (a along x, b in xy plane, c general)
    // a = (a, 0, 0)
    matrix[0] = {a_len, 0.0, 0.0};

    // b = (b*cos(gamma), b*sin(gamma), 0)
    matrix[1] = {b_len * cos_gamma, b_len * sin_gamma, 0.0};

    // c = (c*cos(beta), c*(cos(alpha)-cos(beta)*cos(gamma))/sin(gamma), c*sqrt(...))
    double cx = c_len * cos_beta;
    double cy = c_len * (cos_alpha - cos_beta * cos_gamma) / sin_gamma;
    double cz_sq = c_len * c_len - cx * cx - cy * cy;
    double cz = (cz_sq > 0) ? std::sqrt(cz_sq) : 0.0;
    matrix[2] = {cx, cy, cz};

    defined = true;
}

std::array<double, 3> Lattice::fractionalToCartesian(double fx, double fy, double fz) const {
    return {
        fx * matrix[0][0] + fy * matrix[1][0] + fz * matrix[2][0],
        fx * matrix[0][1] + fy * matrix[1][1] + fz * matrix[2][1],
        fx * matrix[0][2] + fy * matrix[1][2] + fz * matrix[2][2]
    };
}

std::array<double, 3> Lattice::cartesianToFractional(double x, double y, double z) const {
    // Compute inverse of the matrix
    const auto& m = matrix;
    double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
               - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
               + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);

    if (std::abs(det) < 1e-10) {
        return {0, 0, 0};
    }

    double inv_det = 1.0 / det;

    // Compute inverse matrix elements
    double inv00 = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * inv_det;
    double inv01 = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * inv_det;
    double inv02 = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * inv_det;
    double inv10 = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * inv_det;
    double inv11 = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * inv_det;
    double inv12 = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * inv_det;
    double inv20 = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * inv_det;
    double inv21 = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * inv_det;
    double inv22 = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * inv_det;

    return {
        x * inv00 + y * inv10 + z * inv20,
        x * inv01 + y * inv11 + z * inv21,
        x * inv02 + y * inv12 + z * inv22
    };
}

std::array<double, 3> Lattice::wrapPosition(double x, double y, double z) const {
    auto frac = cartesianToFractional(x, y, z);

    if (pbc[0]) frac[0] = frac[0] - std::floor(frac[0]);
    if (pbc[1]) frac[1] = frac[1] - std::floor(frac[1]);
    if (pbc[2]) frac[2] = frac[2] - std::floor(frac[2]);

    return fractionalToCartesian(frac[0], frac[1], frac[2]);
}

bool Lattice::isOrthorhombic() const {
    const double tol = 0.01;  // degrees
    return std::abs(alpha() - 90.0) < tol &&
           std::abs(beta() - 90.0) < tol &&
           std::abs(gamma() - 90.0) < tol;
}

bool Lattice::isCubic() const {
    if (!isOrthorhombic()) return false;
    const double tol = 1e-6;
    double a_len = a();
    return std::abs(b() - a_len) < tol && std::abs(c() - a_len) < tol;
}

// ============================================================================
// Structure implementation
// ============================================================================

Structure::Structure()
    : m_bonds(std::make_shared<BondList>()) {
}

Structure::~Structure() = default;

Structure::Structure(Structure&&) noexcept = default;
Structure& Structure::operator=(Structure&&) noexcept = default;

std::unique_ptr<Structure> Structure::clone() const {
    auto s = std::make_unique<Structure>();
    s->m_atomCount = m_atomCount;

    s->m_posX          = m_posX;
    s->m_posY          = m_posY;
    s->m_posZ          = m_posZ;
    s->m_atomicNumbers = m_atomicNumbers;
    s->m_symbols       = m_symbols;

    s->m_velX   = m_velX;
    s->m_velY   = m_velY;
    s->m_velZ   = m_velZ;
    s->m_forceX = m_forceX;
    s->m_forceY = m_forceY;
    s->m_forceZ = m_forceZ;
    s->m_charges = m_charges;
    s->m_masses  = m_masses;

    s->m_radii  = m_radii;
    s->m_colorR = m_colorR;
    s->m_colorG = m_colorG;
    s->m_colorB = m_colorB;
    s->m_colorA = m_colorA;

    s->m_lattice = m_lattice;
    s->m_bonds   = std::make_shared<BondList>(*m_bonds);

    s->m_sourcePath = m_sourcePath;
    s->m_name       = m_name;
    s->m_info       = m_info;
    s->m_energy     = m_energy;

    return s;
}

void Structure::reserve(size_t count) {
    m_posX.reserve(count);
    m_posY.reserve(count);
    m_posZ.reserve(count);
    m_atomicNumbers.reserve(count);
    m_symbols.reserve(count);
    m_radii.reserve(count);
    m_colorR.reserve(count);
    m_colorG.reserve(count);
    m_colorB.reserve(count);
    m_colorA.reserve(count);
}

void Structure::resize(size_t count) {
    m_posX.resize(count, 0.0f);
    m_posY.resize(count, 0.0f);
    m_posZ.resize(count, 0.0f);
    m_atomicNumbers.resize(count, 0);
    m_symbols.resize(count);
    m_radii.resize(count, 1.0f);
    m_colorR.resize(count, 1.0f);
    m_colorG.resize(count, 1.0f);
    m_colorB.resize(count, 1.0f);
    m_colorA.resize(count, 1.0f);
    m_atomCount = count;
}

void Structure::clear() {
    m_posX.clear();
    m_posY.clear();
    m_posZ.clear();
    m_atomicNumbers.clear();
    m_symbols.clear();
    m_radii.clear();
    m_colorR.clear();
    m_colorG.clear();
    m_colorB.clear();
    m_colorA.clear();

    m_velX.clear();
    m_velY.clear();
    m_velZ.clear();
    m_forceX.clear();
    m_forceY.clear();
    m_forceZ.clear();
    m_charges.clear();
    m_masses.clear();

    m_bonds->clear();
    m_atomCount = 0;
}

size_t Structure::addAtom(float x, float y, float z, int atomicNumber) {
    return addAtom(x, y, z, atomicNumber, ElementData::byAtomicNumber(atomicNumber).symbol);
}

size_t Structure::addAtom(float x, float y, float z, int atomicNumber, std::string_view symbol) {
    size_t index = m_atomCount++;

    m_posX.push_back(x);
    m_posY.push_back(y);
    m_posZ.push_back(z);
    m_atomicNumbers.push_back(atomicNumber);
    m_symbols.emplace_back(symbol);

    // Default rendering properties from element data
    const auto& elem = ElementData::byAtomicNumber(atomicNumber);
    m_radii.push_back(ElementData::radiusForElement(atomicNumber, false));
    m_colorR.push_back(elem.cpkColor.r);
    m_colorG.push_back(elem.cpkColor.g);
    m_colorB.push_back(elem.cpkColor.b);
    m_colorA.push_back(elem.cpkColor.a);

    return index;
}

void Structure::setVelocities(std::vector<float> vx, std::vector<float> vy, std::vector<float> vz) {
    if (vx.size() != m_atomCount || vy.size() != m_atomCount || vz.size() != m_atomCount) {
        throw std::invalid_argument("Velocity array size mismatch");
    }
    m_velX = std::move(vx);
    m_velY = std::move(vy);
    m_velZ = std::move(vz);
}

void Structure::clearVelocities() {
    m_velX.clear();
    m_velY.clear();
    m_velZ.clear();
}

void Structure::setForces(std::vector<float> fx, std::vector<float> fy, std::vector<float> fz) {
    if (fx.size() != m_atomCount || fy.size() != m_atomCount || fz.size() != m_atomCount) {
        throw std::invalid_argument("Force array size mismatch");
    }
    m_forceX = std::move(fx);
    m_forceY = std::move(fy);
    m_forceZ = std::move(fz);
}

void Structure::clearForces() {
    m_forceX.clear();
    m_forceY.clear();
    m_forceZ.clear();
}

void Structure::setCharges(std::vector<float> charges) {
    if (charges.size() != m_atomCount) {
        throw std::invalid_argument("Charges array size mismatch");
    }
    m_charges = std::move(charges);
}

void Structure::clearCharges() {
    m_charges.clear();
}

void Structure::setMasses(std::vector<float> masses) {
    if (masses.size() != m_atomCount) {
        throw std::invalid_argument("Masses array size mismatch");
    }
    m_masses = std::move(masses);
}

void Structure::clearMasses() {
    m_masses.clear();
}

void Structure::updateColorsFromElements() {
    for (size_t i = 0; i < m_atomCount; ++i) {
        const auto& elem = ElementData::byAtomicNumber(m_atomicNumbers[i]);
        m_colorR[i] = elem.cpkColor.r;
        m_colorG[i] = elem.cpkColor.g;
        m_colorB[i] = elem.cpkColor.b;
        m_colorA[i] = elem.cpkColor.a;
    }
}

void Structure::updateRadiiFromElements(float scale, bool useVdW) {
    for (size_t i = 0; i < m_atomCount; ++i) {
        m_radii[i] = ElementData::radiusForElement(m_atomicNumbers[i], useVdW) * scale;
    }
}

std::array<float, 3> Structure::position(size_t index) const {
    return {m_posX[index], m_posY[index], m_posZ[index]};
}

int Structure::atomicNumber(size_t index) const {
    return m_atomicNumbers[index];
}

const std::string& Structure::symbol(size_t index) const {
    return m_symbols[index];
}

float Structure::radius(size_t index) const {
    return m_radii[index];
}

Color Structure::color(size_t index) const {
    return Color(m_colorR[index], m_colorG[index], m_colorB[index], m_colorA[index]);
}

void Structure::setPosition(size_t index, float x, float y, float z) {
    m_posX[index] = x;
    m_posY[index] = y;
    m_posZ[index] = z;
}

void Structure::setAtomicNumber(size_t index, int atomicNumber) {
    m_atomicNumbers[index] = atomicNumber;
    m_symbols[index] = std::string(ElementData::byAtomicNumber(atomicNumber).symbol);
}

void Structure::setInfo(const std::string& key, const std::string& value) {
    m_info[key] = value;
}

std::optional<std::string> Structure::getInfo(const std::string& key) const {
    auto it = m_info.find(key);
    if (it != m_info.end()) {
        return it->second;
    }
    return std::nullopt;
}

float Structure::BoundingBox::maxExtent() const {
    return std::max({extentX(), extentY(), extentZ()});
}

Structure::BoundingBox Structure::computeBoundingBox() const {
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

Structure::BoundingBox Structure::computeUnitCellBoundingBox() const {
    BoundingBox box;
    if (!hasLattice()) return box;

    const auto& lattice = m_lattice;
    const auto& m = lattice.matrix;

    const std::array<std::array<float, 3>, 8> corners = {{
        {{0.0f, 0.0f, 0.0f}},
        {{static_cast<float>(m[0][0]), static_cast<float>(m[0][1]), static_cast<float>(m[0][2])}},
        {{static_cast<float>(m[1][0]), static_cast<float>(m[1][1]), static_cast<float>(m[1][2])}},
        {{static_cast<float>(m[2][0]), static_cast<float>(m[2][1]), static_cast<float>(m[2][2])}},
        {{static_cast<float>(m[0][0] + m[1][0]), static_cast<float>(m[0][1] + m[1][1]), static_cast<float>(m[0][2] + m[1][2])}},
        {{static_cast<float>(m[0][0] + m[2][0]), static_cast<float>(m[0][1] + m[2][1]), static_cast<float>(m[0][2] + m[2][2])}},
        {{static_cast<float>(m[1][0] + m[2][0]), static_cast<float>(m[1][1] + m[2][1]), static_cast<float>(m[1][2] + m[2][2])}},
        {{static_cast<float>(m[0][0] + m[1][0] + m[2][0]),
          static_cast<float>(m[0][1] + m[1][1] + m[2][1]),
          static_cast<float>(m[0][2] + m[1][2] + m[2][2])}},
    }};

    box.minX = box.maxX = corners[0][0];
    box.minY = box.maxY = corners[0][1];
    box.minZ = box.maxZ = corners[0][2];

    for (size_t i = 1; i < corners.size(); ++i) {
        box.minX = std::min(box.minX, corners[i][0]);
        box.maxX = std::max(box.maxX, corners[i][0]);
        box.minY = std::min(box.minY, corners[i][1]);
        box.maxY = std::max(box.maxY, corners[i][1]);
        box.minZ = std::min(box.minZ, corners[i][2]);
        box.maxZ = std::max(box.maxZ, corners[i][2]);
    }

    return box;
}

Structure::BoundingBox Structure::computeViewBoundingBox() const {
    BoundingBox box = computeBoundingBox();
    if (!hasLattice()) return box;

    BoundingBox cellBox = computeUnitCellBoundingBox();
    if (m_atomCount == 0) return cellBox;

    box.minX = std::min(box.minX, cellBox.minX);
    box.maxX = std::max(box.maxX, cellBox.maxX);
    box.minY = std::min(box.minY, cellBox.minY);
    box.maxY = std::max(box.maxY, cellBox.maxY);
    box.minZ = std::min(box.minZ, cellBox.minZ);
    box.maxZ = std::max(box.maxZ, cellBox.maxZ);
    return box;
}

std::array<float, 3> Structure::geometricCenter() const {
    if (m_atomCount == 0) return {0.0f, 0.0f, 0.0f};

    double cx = 0.0;
    double cy = 0.0;
    double cz = 0.0;

    for (size_t i = 0; i < m_atomCount; ++i) {
        cx += m_posX[i];
        cy += m_posY[i];
        cz += m_posZ[i];
    }

    const double invCount = 1.0 / static_cast<double>(m_atomCount);
    return {
        static_cast<float>(cx * invCount),
        static_cast<float>(cy * invCount),
        static_cast<float>(cz * invCount),
    };
}

std::array<float, 3> Structure::unitCellCenter() const {
    if (!hasLattice()) return {0.0f, 0.0f, 0.0f};

    const auto center = m_lattice.fractionalToCartesian(0.5, 0.5, 0.5);
    return {
        static_cast<float>(center[0]),
        static_cast<float>(center[1]),
        static_cast<float>(center[2]),
    };
}

std::array<float, 3> Structure::centerOfMass() const {
    if (m_atomCount == 0) return {0, 0, 0};

    double totalMass = 0;
    double cx = 0, cy = 0, cz = 0;

    for (size_t i = 0; i < m_atomCount; ++i) {
        float mass;
        if (!m_masses.empty()) {
            mass = m_masses[i];
        } else {
            mass = ElementData::byAtomicNumber(m_atomicNumbers[i]).mass;
        }
        totalMass += mass;
        cx += mass * m_posX[i];
        cy += mass * m_posY[i];
        cz += mass * m_posZ[i];
    }

    if (totalMass > 0) {
        cx /= totalMass;
        cy /= totalMass;
        cz /= totalMass;
    }

    return {static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(cz)};
}

std::vector<float> Structure::packPositionsAndRadii() const {
    std::vector<float> data(m_atomCount * 4);
    for (size_t i = 0; i < m_atomCount; ++i) {
        data[i * 4 + 0] = m_posX[i];
        data[i * 4 + 1] = m_posY[i];
        data[i * 4 + 2] = m_posZ[i];
        data[i * 4 + 3] = m_radii[i];
    }
    return data;
}

std::vector<float> Structure::packColors() const {
    std::vector<float> data(m_atomCount * 4);
    for (size_t i = 0; i < m_atomCount; ++i) {
        data[i * 4 + 0] = m_colorR[i];
        data[i * 4 + 1] = m_colorG[i];
        data[i * 4 + 2] = m_colorB[i];
        data[i * 4 + 3] = m_colorA[i];
    }
    return data;
}

void Structure::setBondList(std::shared_ptr<BondList> newBonds) {
    if (newBonds) {
        m_bonds = std::move(newBonds);
    }
}

} // namespace atom::data
