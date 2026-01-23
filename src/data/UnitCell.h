#pragma once

#include <array>
#include <cmath>

namespace atom::data {

/**
 * @brief Unit cell for periodic structures
 *
 * Represents a crystallographic unit cell with lattice vectors.
 * Supports conversion between fractional and Cartesian coordinates.
 */
class UnitCell {
public:
    UnitCell();

    /**
     * @brief Create unit cell from lattice parameters
     * @param a Length of a vector (Angstroms)
     * @param b Length of b vector (Angstroms)
     * @param c Length of c vector (Angstroms)
     * @param alpha Angle between b and c (degrees)
     * @param beta Angle between a and c (degrees)
     * @param gamma Angle between a and b (degrees)
     */
    UnitCell(float a, float b, float c, float alpha, float beta, float gamma);

    /**
     * @brief Create unit cell from lattice vectors
     */
    UnitCell(const std::array<float, 3>& va,
             const std::array<float, 3>& vb,
             const std::array<float, 3>& vc);

    /**
     * @brief Set lattice parameters
     */
    void setParameters(float a, float b, float c, float alpha, float beta, float gamma);

    /**
     * @brief Set lattice vectors directly
     */
    void setVectors(const std::array<float, 3>& va,
                    const std::array<float, 3>& vb,
                    const std::array<float, 3>& vc);

    /**
     * @brief Mark cell as valid/defined
     */
    void setDefined(bool defined) { m_defined = defined; }
    bool isDefined() const { return m_defined; }

    // Lattice parameters
    float a() const { return m_a; }
    float b() const { return m_b; }
    float c() const { return m_c; }
    float alpha() const { return m_alpha; }
    float beta() const { return m_beta; }
    float gamma() const { return m_gamma; }

    // Lattice vectors
    const std::array<float, 3>& vectorA() const { return m_vectorA; }
    const std::array<float, 3>& vectorB() const { return m_vectorB; }
    const std::array<float, 3>& vectorC() const { return m_vectorC; }

    /**
     * @brief Get cell matrix (column-major, 3x3)
     * Columns are the lattice vectors a, b, c
     */
    std::array<float, 9> matrix() const;

    /**
     * @brief Get inverse cell matrix for fractional coordinate conversion
     */
    std::array<float, 9> inverseMatrix() const;

    /**
     * @brief Convert fractional coordinates to Cartesian
     */
    std::array<float, 3> fractionalToCartesian(float fx, float fy, float fz) const;
    std::array<float, 3> fractionalToCartesian(const std::array<float, 3>& frac) const;

    /**
     * @brief Convert Cartesian coordinates to fractional
     */
    std::array<float, 3> cartesianToFractional(float x, float y, float z) const;
    std::array<float, 3> cartesianToFractional(const std::array<float, 3>& cart) const;

    /**
     * @brief Compute volume of the unit cell
     */
    float volume() const;

    /**
     * @brief Periodicity flags for each direction
     */
    void setPeriodicity(bool px, bool py, bool pz);
    bool periodicX() const { return m_periodicX; }
    bool periodicY() const { return m_periodicY; }
    bool periodicZ() const { return m_periodicZ; }

    /**
     * @brief Apply periodic boundary conditions to a position
     */
    std::array<float, 3> wrapPosition(float x, float y, float z) const;

    /**
     * @brief Get minimum image distance vector between two points
     */
    std::array<float, 3> minimumImageVector(
        float x1, float y1, float z1,
        float x2, float y2, float z2) const;

    /**
     * @brief Get minimum image distance between two points
     */
    float minimumImageDistance(
        float x1, float y1, float z1,
        float x2, float y2, float z2) const;

    /**
     * @brief Check if cell is orthorhombic (all angles 90 degrees)
     */
    bool isOrthorhombic() const;

    /**
     * @brief Check if cell is cubic
     */
    bool isCubic() const;

private:
    void computeVectorsFromParameters();
    void computeParametersFromVectors();
    void computeInverseMatrix();

    // Lattice parameters
    float m_a = 0, m_b = 0, m_c = 0;
    float m_alpha = 90, m_beta = 90, m_gamma = 90;

    // Lattice vectors
    std::array<float, 3> m_vectorA = {0, 0, 0};
    std::array<float, 3> m_vectorB = {0, 0, 0};
    std::array<float, 3> m_vectorC = {0, 0, 0};

    // Inverse matrix for fractional coordinate conversion
    std::array<float, 9> m_inverseMatrix = {0};

    // State
    bool m_defined = false;
    bool m_periodicX = true;
    bool m_periodicY = true;
    bool m_periodicZ = true;
};

} // namespace atom::data
