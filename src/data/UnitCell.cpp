#include "UnitCell.h"

#include <cmath>
#include <algorithm>

namespace atom::data {

namespace {
constexpr float PI = 3.14159265358979323846f;
constexpr float DEG_TO_RAD = PI / 180.0f;
constexpr float RAD_TO_DEG = 180.0f / PI;
}

UnitCell::UnitCell() = default;

UnitCell::UnitCell(float a, float b, float c, float alpha, float beta, float gamma) {
    setParameters(a, b, c, alpha, beta, gamma);
}

UnitCell::UnitCell(const std::array<float, 3>& va,
                   const std::array<float, 3>& vb,
                   const std::array<float, 3>& vc) {
    setVectors(va, vb, vc);
}

void UnitCell::setParameters(float a, float b, float c,
                              float alpha, float beta, float gamma) {
    m_a = a;
    m_b = b;
    m_c = c;
    m_alpha = alpha;
    m_beta = beta;
    m_gamma = gamma;
    m_defined = (a > 0 && b > 0 && c > 0);
    computeVectorsFromParameters();
    computeInverseMatrix();
}

void UnitCell::setVectors(const std::array<float, 3>& va,
                           const std::array<float, 3>& vb,
                           const std::array<float, 3>& vc) {
    m_vectorA = va;
    m_vectorB = vb;
    m_vectorC = vc;
    computeParametersFromVectors();
    computeInverseMatrix();
    m_defined = (m_a > 0 && m_b > 0 && m_c > 0);
}

void UnitCell::computeVectorsFromParameters() {
    // Convert angles to radians
    float alphaRad = m_alpha * DEG_TO_RAD;
    float betaRad = m_beta * DEG_TO_RAD;
    float gammaRad = m_gamma * DEG_TO_RAD;

    // Vector a along x-axis
    m_vectorA = {m_a, 0.0f, 0.0f};

    // Vector b in xy-plane
    m_vectorB = {
        m_b * std::cos(gammaRad),
        m_b * std::sin(gammaRad),
        0.0f
    };

    // Vector c
    float cx = m_c * std::cos(betaRad);
    float cy = m_c * (std::cos(alphaRad) - std::cos(betaRad) * std::cos(gammaRad)) /
               std::sin(gammaRad);
    float cz = std::sqrt(m_c * m_c - cx * cx - cy * cy);
    m_vectorC = {cx, cy, cz};
}

void UnitCell::computeParametersFromVectors() {
    // Compute lengths
    auto length = [](const std::array<float, 3>& v) {
        return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    };
    auto dot = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };

    m_a = length(m_vectorA);
    m_b = length(m_vectorB);
    m_c = length(m_vectorC);

    // Compute angles
    if (m_a > 0 && m_b > 0 && m_c > 0) {
        m_alpha = std::acos(dot(m_vectorB, m_vectorC) / (m_b * m_c)) * RAD_TO_DEG;
        m_beta = std::acos(dot(m_vectorA, m_vectorC) / (m_a * m_c)) * RAD_TO_DEG;
        m_gamma = std::acos(dot(m_vectorA, m_vectorB) / (m_a * m_b)) * RAD_TO_DEG;
    } else {
        m_alpha = m_beta = m_gamma = 90.0f;
    }
}

void UnitCell::computeInverseMatrix() {
    // Matrix M = [a b c] (columns are lattice vectors)
    // We need M^(-1) to convert Cartesian to fractional
    float ax = m_vectorA[0], ay = m_vectorA[1], az = m_vectorA[2];
    float bx = m_vectorB[0], by = m_vectorB[1], bz = m_vectorB[2];
    float cx = m_vectorC[0], cy = m_vectorC[1], cz = m_vectorC[2];

    // Compute determinant
    float det = ax * (by * cz - bz * cy) -
                bx * (ay * cz - az * cy) +
                cx * (ay * bz - az * by);

    if (std::abs(det) < 1e-10f) {
        // Singular matrix, set identity
        m_inverseMatrix = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        return;
    }

    float invDet = 1.0f / det;

    // Compute inverse (row-major storage for easy indexing)
    m_inverseMatrix[0] = (by * cz - bz * cy) * invDet;
    m_inverseMatrix[1] = (bz * cx - bx * cz) * invDet;
    m_inverseMatrix[2] = (bx * cy - by * cx) * invDet;
    m_inverseMatrix[3] = (az * cy - ay * cz) * invDet;
    m_inverseMatrix[4] = (ax * cz - az * cx) * invDet;
    m_inverseMatrix[5] = (ay * cx - ax * cy) * invDet;
    m_inverseMatrix[6] = (ay * bz - az * by) * invDet;
    m_inverseMatrix[7] = (az * bx - ax * bz) * invDet;
    m_inverseMatrix[8] = (ax * by - ay * bx) * invDet;
}

std::array<float, 9> UnitCell::matrix() const {
    // Column-major: columns are a, b, c vectors
    return {
        m_vectorA[0], m_vectorA[1], m_vectorA[2],
        m_vectorB[0], m_vectorB[1], m_vectorB[2],
        m_vectorC[0], m_vectorC[1], m_vectorC[2]
    };
}

std::array<float, 9> UnitCell::inverseMatrix() const {
    return m_inverseMatrix;
}

std::array<float, 3> UnitCell::fractionalToCartesian(float fx, float fy, float fz) const {
    return {
        fx * m_vectorA[0] + fy * m_vectorB[0] + fz * m_vectorC[0],
        fx * m_vectorA[1] + fy * m_vectorB[1] + fz * m_vectorC[1],
        fx * m_vectorA[2] + fy * m_vectorB[2] + fz * m_vectorC[2]
    };
}

std::array<float, 3> UnitCell::fractionalToCartesian(const std::array<float, 3>& frac) const {
    return fractionalToCartesian(frac[0], frac[1], frac[2]);
}

std::array<float, 3> UnitCell::cartesianToFractional(float x, float y, float z) const {
    return {
        m_inverseMatrix[0] * x + m_inverseMatrix[1] * y + m_inverseMatrix[2] * z,
        m_inverseMatrix[3] * x + m_inverseMatrix[4] * y + m_inverseMatrix[5] * z,
        m_inverseMatrix[6] * x + m_inverseMatrix[7] * y + m_inverseMatrix[8] * z
    };
}

std::array<float, 3> UnitCell::cartesianToFractional(const std::array<float, 3>& cart) const {
    return cartesianToFractional(cart[0], cart[1], cart[2]);
}

float UnitCell::volume() const {
    // Volume = a . (b x c)
    float bxc_x = m_vectorB[1] * m_vectorC[2] - m_vectorB[2] * m_vectorC[1];
    float bxc_y = m_vectorB[2] * m_vectorC[0] - m_vectorB[0] * m_vectorC[2];
    float bxc_z = m_vectorB[0] * m_vectorC[1] - m_vectorB[1] * m_vectorC[0];
    return std::abs(m_vectorA[0] * bxc_x + m_vectorA[1] * bxc_y + m_vectorA[2] * bxc_z);
}

void UnitCell::setPeriodicity(bool px, bool py, bool pz) {
    m_periodicX = px;
    m_periodicY = py;
    m_periodicZ = pz;
}

std::array<float, 3> UnitCell::wrapPosition(float x, float y, float z) const {
    auto frac = cartesianToFractional(x, y, z);

    if (m_periodicX) {
        frac[0] = frac[0] - std::floor(frac[0]);
    }
    if (m_periodicY) {
        frac[1] = frac[1] - std::floor(frac[1]);
    }
    if (m_periodicZ) {
        frac[2] = frac[2] - std::floor(frac[2]);
    }

    return fractionalToCartesian(frac);
}

std::array<float, 3> UnitCell::minimumImageVector(
    float x1, float y1, float z1,
    float x2, float y2, float z2) const {

    float dx = x2 - x1;
    float dy = y2 - y1;
    float dz = z2 - z1;

    auto frac = cartesianToFractional(dx, dy, dz);

    // Apply minimum image convention
    if (m_periodicX) {
        frac[0] = frac[0] - std::round(frac[0]);
    }
    if (m_periodicY) {
        frac[1] = frac[1] - std::round(frac[1]);
    }
    if (m_periodicZ) {
        frac[2] = frac[2] - std::round(frac[2]);
    }

    return fractionalToCartesian(frac);
}

float UnitCell::minimumImageDistance(
    float x1, float y1, float z1,
    float x2, float y2, float z2) const {

    auto v = minimumImageVector(x1, y1, z1, x2, y2, z2);
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

bool UnitCell::isOrthorhombic() const {
    const float tolerance = 0.01f; // 0.01 degree tolerance
    return std::abs(m_alpha - 90.0f) < tolerance &&
           std::abs(m_beta - 90.0f) < tolerance &&
           std::abs(m_gamma - 90.0f) < tolerance;
}

bool UnitCell::isCubic() const {
    const float tolerance = 0.001f;
    return isOrthorhombic() &&
           std::abs(m_a - m_b) < tolerance &&
           std::abs(m_b - m_c) < tolerance;
}

} // namespace atom::data
