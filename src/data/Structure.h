#pragma once

#include "BondList.h"
#include "ElementData.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace atom::data {

/**
 * @brief Lattice/unit cell for periodic structures
 *
 * Stores the 3x3 cell matrix (row-major: rows are lattice vectors a, b, c)
 * and periodic boundary condition flags.
 */
struct Lattice {
    bool defined = false;
    std::array<std::array<double, 3>, 3> matrix = {{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}};
    std::array<bool, 3> pbc = {false, false, false};

    // Lattice parameters derived from matrix
    double a() const;
    double b() const;
    double c() const;
    double alpha() const;  // angle between b and c (degrees)
    double beta() const;   // angle between a and c (degrees)
    double gamma() const;  // angle between a and b (degrees)
    double volume() const;

    // Set from parameters (angles in degrees)
    void setFromParameters(double a, double b, double c,
                           double alpha, double beta, double gamma);

    // Coordinate transformations
    std::array<double, 3> fractionalToCartesian(double fx, double fy, double fz) const;
    std::array<double, 3> cartesianToFractional(double x, double y, double z) const;

    // Apply periodic boundary conditions
    std::array<double, 3> wrapPosition(double x, double y, double z) const;

    // Check cell type
    bool isOrthorhombic() const;
    bool isCubic() const;
};

/**
 * @brief Structure-of-Arrays storage for atomic structure data
 *
 * This class uses SoA layout for optimal GPU upload and SIMD operations.
 * Designed to interface with ASE's Atoms object.
 *
 * Core properties (always present):
 * - positions (x, y, z)
 * - atomic numbers
 * - symbols
 *
 * Optional per-atom properties (placeholders, may be empty):
 * - velocities
 * - forces
 * - charges
 * - masses
 *
 * Rendering properties (computed from atomic numbers or set directly):
 * - radii
 * - colors (RGBA)
 */
class Structure {
public:
    Structure();
    ~Structure();

    // Move semantics only (large data)
    Structure(Structure&&) noexcept;
    Structure& operator=(Structure&&) noexcept;
    Structure(const Structure&) = delete;
    Structure& operator=(const Structure&) = delete;

    /**
     * @brief Deep-copy this structure into a new independent instance.
     *
     * All SoA arrays, lattice, bonds, and metadata are copied.
     * The returned structure shares no data with the original.
     */
    std::unique_ptr<Structure> clone() const;

    // ========== Atom management ==========

    /**
     * @brief Reserve memory for atoms
     * @param count Number of atoms to reserve
     */
    void reserve(size_t count);

    /**
     * @brief Resize to a specific number of atoms
     * @param count New atom count
     */
    void resize(size_t count);

    /**
     * @brief Clear all atoms
     */
    void clear();

    /**
     * @brief Add a single atom
     * @param x X position in Angstroms
     * @param y Y position in Angstroms
     * @param z Z position in Angstroms
     * @param atomicNumber Element atomic number (1-118)
     * @return Index of the added atom
     */
    size_t addAtom(float x, float y, float z, int atomicNumber);

    /**
     * @brief Add a single atom with symbol
     */
    size_t addAtom(float x, float y, float z, int atomicNumber, std::string_view symbol);

    // ========== Core data accessors (SoA) ==========

    size_t atomCount() const { return m_atomCount; }
    bool empty() const { return m_atomCount == 0; }

    // Positions (required)
    const float* positionsX() const { return m_posX.data(); }
    const float* positionsY() const { return m_posY.data(); }
    const float* positionsZ() const { return m_posZ.data(); }
    float* positionsX() { return m_posX.data(); }
    float* positionsY() { return m_posY.data(); }
    float* positionsZ() { return m_posZ.data(); }

    // Atomic numbers (required)
    const int* atomicNumbers() const { return m_atomicNumbers.data(); }
    int* atomicNumbers() { return m_atomicNumbers.data(); }

    // Symbols (required)
    const std::vector<std::string>& symbols() const { return m_symbols; }
    std::vector<std::string>& symbols() { return m_symbols; }

    // ========== Optional per-atom properties ==========
    // These vectors may be empty if not set

    // Velocities (optional)
    bool hasVelocities() const { return !m_velX.empty(); }
    const float* velocitiesX() const { return m_velX.data(); }
    const float* velocitiesY() const { return m_velY.data(); }
    const float* velocitiesZ() const { return m_velZ.data(); }
    void setVelocities(std::vector<float> vx, std::vector<float> vy, std::vector<float> vz);
    void clearVelocities();

    // Forces (optional)
    bool hasForces() const { return !m_forceX.empty(); }
    const float* forcesX() const { return m_forceX.data(); }
    const float* forcesY() const { return m_forceY.data(); }
    const float* forcesZ() const { return m_forceZ.data(); }
    void setForces(std::vector<float> fx, std::vector<float> fy, std::vector<float> fz);
    void clearForces();

    // Charges (optional)
    bool hasCharges() const { return !m_charges.empty(); }
    const float* charges() const { return m_charges.data(); }
    void setCharges(std::vector<float> charges);
    void clearCharges();

    // Masses (optional, defaults to element masses if not set)
    bool hasMasses() const { return !m_masses.empty(); }
    const float* masses() const { return m_masses.data(); }
    void setMasses(std::vector<float> masses);
    void clearMasses();

    // ========== Rendering properties ==========

    // Radii (for visualization)
    const float* radii() const { return m_radii.data(); }
    float* radii() { return m_radii.data(); }

    // Colors (RGBA)
    const float* colorsR() const { return m_colorR.data(); }
    const float* colorsG() const { return m_colorG.data(); }
    const float* colorsB() const { return m_colorB.data(); }
    const float* colorsA() const { return m_colorA.data(); }
    float* colorsR() { return m_colorR.data(); }
    float* colorsG() { return m_colorG.data(); }
    float* colorsB() { return m_colorB.data(); }
    float* colorsA() { return m_colorA.data(); }

    /**
     * @brief Update colors from element types using the selected ASE color scheme
     */
    void updateColorsFromElements(
        ElementColorScheme scheme = ElementColorScheme::Jmol);

    /**
     * @brief Update radii from element types
     * @param scale Scale factor for radii
     * @param useVdW Use Van der Waals radii instead of covalent
     */
    void updateRadiiFromElements(float scale = 1.0f, bool useVdW = false);

    // ========== Individual atom access ==========

    std::array<float, 3> position(size_t index) const;
    int atomicNumber(size_t index) const;
    const std::string& symbol(size_t index) const;
    float radius(size_t index) const;
    Color color(size_t index) const;

    void setPosition(size_t index, float x, float y, float z);
    void setAtomicNumber(size_t index, int atomicNumber);

    // ========== Lattice ==========

    Lattice& lattice() { return m_lattice; }
    const Lattice& lattice() const { return m_lattice; }
    bool hasLattice() const { return m_lattice.defined; }

    // ========== Bonds ==========

    BondList& bonds() { return *m_bonds; }
    const BondList& bonds() const { return *m_bonds; }

    /**
     * @brief Replace the bond list (used after async bond detection completes).
     * @param newBonds New bond list to adopt; must not be null.
     */
    void setBondList(std::shared_ptr<BondList> newBonds);

    // ========== Metadata ==========

    void setSourcePath(const std::string& path) { m_sourcePath = path; }
    const std::string& sourcePath() const { return m_sourcePath; }

    void setName(const std::string& name) { m_name = name; }
    const std::string& name() const { return m_name; }

    // ASE info dictionary (arbitrary key-value pairs)
    void setInfo(const std::string& key, const std::string& value);
    std::optional<std::string> getInfo(const std::string& key) const;
    const std::unordered_map<std::string, std::string>& info() const { return m_info; }

    // Scalar properties
    void setEnergy(double energy) { m_energy = energy; }
    double energy() const { return m_energy; }
    bool hasEnergy() const { return !std::isnan(m_energy); }

    // ========== Geometry ==========

    struct BoundingBox {
        float minX = 0, minY = 0, minZ = 0;
        float maxX = 0, maxY = 0, maxZ = 0;

        float centerX() const { return (minX + maxX) * 0.5f; }
        float centerY() const { return (minY + maxY) * 0.5f; }
        float centerZ() const { return (minZ + maxZ) * 0.5f; }
        float extentX() const { return maxX - minX; }
        float extentY() const { return maxY - minY; }
        float extentZ() const { return maxZ - minZ; }
        float maxExtent() const;
    };

    BoundingBox computeBoundingBox() const;
    BoundingBox computeUnitCellBoundingBox() const;
    BoundingBox computeViewBoundingBox() const;
    std::array<float, 3> geometricCenter() const;
    std::array<float, 3> unitCellCenter() const;
    std::array<float, 3> centerOfMass() const;

    // ========== GPU data packing ==========

    /**
     * @brief Pack positions and radii in interleaved XYZR format
     */
    std::vector<float> packPositionsAndRadii() const;

    /**
     * @brief Pack colors in RGBA format
     */
    std::vector<float> packColors() const;

private:
    size_t m_atomCount = 0;

    // Core SoA arrays (required, size = atomCount)
    std::vector<float> m_posX;
    std::vector<float> m_posY;
    std::vector<float> m_posZ;
    std::vector<int> m_atomicNumbers;
    std::vector<std::string> m_symbols;

    // Optional per-atom properties (may be empty)
    std::vector<float> m_velX;
    std::vector<float> m_velY;
    std::vector<float> m_velZ;
    std::vector<float> m_forceX;
    std::vector<float> m_forceY;
    std::vector<float> m_forceZ;
    std::vector<float> m_charges;
    std::vector<float> m_masses;

    // Rendering properties (size = atomCount)
    std::vector<float> m_radii;
    std::vector<float> m_colorR;
    std::vector<float> m_colorG;
    std::vector<float> m_colorB;
    std::vector<float> m_colorA;

    // Lattice
    Lattice m_lattice;

    // Bonds
    std::shared_ptr<BondList> m_bonds;

    // Metadata
    std::string m_sourcePath;
    std::string m_name;
    std::unordered_map<std::string, std::string> m_info;
    double m_energy = std::numeric_limits<double>::quiet_NaN();
};

} // namespace atom::data
