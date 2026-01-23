#pragma once

#include "ElementData.h"
#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include <array>

namespace atom::data {

// Forward declarations
class BondList;
class UnitCell;

/**
 * @brief Structure-of-Arrays storage for atomic structure data
 *
 * This class uses SoA layout for optimal GPU upload and SIMD operations.
 * All arrays are aligned and have the same size (atomCount).
 *
 * Memory layout is optimized for:
 * - Cache-friendly sequential access
 * - Efficient GPU buffer upload
 * - SIMD vectorization
 */
class AtomicStructure {
public:
    AtomicStructure();
    ~AtomicStructure();

    // Move semantics only (large data)
    AtomicStructure(AtomicStructure&&) noexcept;
    AtomicStructure& operator=(AtomicStructure&&) noexcept;
    AtomicStructure(const AtomicStructure&) = delete;
    AtomicStructure& operator=(const AtomicStructure&) = delete;

    /**
     * @brief Reserve memory for a number of atoms
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
     * @param type Atom type (element atomic number)
     * @return Index of the added atom
     */
    size_t addAtom(float x, float y, float z, int type);

    /**
     * @brief Add a single atom with all properties
     */
    size_t addAtom(float x, float y, float z, int type, float radius, const Color& color);

    /**
     * @brief Set atom position
     */
    void setPosition(size_t index, float x, float y, float z);

    /**
     * @brief Set atom type
     */
    void setType(size_t index, int type);

    /**
     * @brief Update colors from element types (CPK coloring)
     */
    void updateColorsFromTypes();

    /**
     * @brief Update radii from element types
     * @param scale Scale factor for radii
     * @param useVdW Use Van der Waals radii instead of covalent
     */
    void updateRadiiFromTypes(float scale = 1.0f, bool useVdW = false);

    // Accessors
    size_t atomCount() const { return m_atomCount; }
    bool empty() const { return m_atomCount == 0; }

    // SoA data access (raw pointers for performance)
    const float* positionsX() const { return m_posX.data(); }
    const float* positionsY() const { return m_posY.data(); }
    const float* positionsZ() const { return m_posZ.data(); }
    const int* types() const { return m_types.data(); }
    const float* radii() const { return m_radii.data(); }
    const float* colorsR() const { return m_colorR.data(); }
    const float* colorsG() const { return m_colorG.data(); }
    const float* colorsB() const { return m_colorB.data(); }
    const float* colorsA() const { return m_colorA.data(); }

    // Mutable access
    float* positionsX() { return m_posX.data(); }
    float* positionsY() { return m_posY.data(); }
    float* positionsZ() { return m_posZ.data(); }
    int* types() { return m_types.data(); }
    float* radii() { return m_radii.data(); }
    float* colorsR() { return m_colorR.data(); }
    float* colorsG() { return m_colorG.data(); }
    float* colorsB() { return m_colorB.data(); }
    float* colorsA() { return m_colorA.data(); }

    // Individual atom access
    std::array<float, 3> position(size_t index) const;
    int type(size_t index) const;
    float radius(size_t index) const;
    Color color(size_t index) const;

    // Bounding box
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

    /**
     * @brief Compute bounding box of all atoms
     */
    BoundingBox computeBoundingBox() const;

    /**
     * @brief Get center of mass
     */
    std::array<float, 3> centerOfMass() const;

    // Bonds
    BondList& bonds() { return *m_bonds; }
    const BondList& bonds() const { return *m_bonds; }

    // Unit cell
    UnitCell& unitCell() { return *m_unitCell; }
    const UnitCell& unitCell() const { return *m_unitCell; }
    bool hasUnitCell() const;

    // Metadata
    void setName(const std::string& name) { m_name = name; }
    const std::string& name() const { return m_name; }

    void setSourceFile(const std::string& path) { m_sourceFile = path; }
    const std::string& sourceFile() const { return m_sourceFile; }

    // Interleaved data for GPU upload (packed XYZR format)
    std::vector<float> packPositionsAndRadii() const;
    // Packed RGBA format
    std::vector<float> packColors() const;

private:
    size_t m_atomCount = 0;

    // SoA arrays
    std::vector<float> m_posX;
    std::vector<float> m_posY;
    std::vector<float> m_posZ;
    std::vector<int> m_types;     // Element atomic number
    std::vector<float> m_radii;   // Display radius
    std::vector<float> m_colorR;
    std::vector<float> m_colorG;
    std::vector<float> m_colorB;
    std::vector<float> m_colorA;

    // Optional: velocities for MD trajectories
    std::vector<float> m_velX;
    std::vector<float> m_velY;
    std::vector<float> m_velZ;

    // Bonds and unit cell
    std::unique_ptr<BondList> m_bonds;
    std::unique_ptr<UnitCell> m_unitCell;

    // Metadata
    std::string m_name;
    std::string m_sourceFile;
};

} // namespace atom::data
