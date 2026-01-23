#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace atom::data {

/**
 * @brief Bond order enumeration
 */
enum class BondOrder : uint8_t {
    Single = 1,
    Double = 2,
    Triple = 3,
    Aromatic = 4,
    Unknown = 0
};

/**
 * @brief A single bond between two atoms
 */
struct Bond {
    uint32_t atomIndex1;
    uint32_t atomIndex2;
    BondOrder order = BondOrder::Single;

    Bond() = default;
    Bond(uint32_t a1, uint32_t a2, BondOrder ord = BondOrder::Single)
        : atomIndex1(a1), atomIndex2(a2), order(ord) {}
};

/**
 * @brief Container for bond connectivity
 *
 * Stores bonds as pairs of atom indices with optional bond order.
 * Provides efficient lookup and iteration.
 */
class BondList {
public:
    BondList() = default;

    /**
     * @brief Reserve memory for bonds
     */
    void reserve(size_t count);

    /**
     * @brief Clear all bonds
     */
    void clear();

    /**
     * @brief Add a bond between two atoms
     * @param atomIndex1 First atom index
     * @param atomIndex2 Second atom index
     * @param order Bond order
     * @return Index of the added bond
     */
    size_t addBond(uint32_t atomIndex1, uint32_t atomIndex2,
                   BondOrder order = BondOrder::Single);

    /**
     * @brief Remove a bond by index
     */
    void removeBond(size_t bondIndex);

    /**
     * @brief Find bond between two atoms
     * @return Bond index, or -1 if not found
     */
    int findBond(uint32_t atomIndex1, uint32_t atomIndex2) const;

    /**
     * @brief Check if two atoms are bonded
     */
    bool areBonded(uint32_t atomIndex1, uint32_t atomIndex2) const;

    // Accessors
    size_t bondCount() const { return m_bonds.size(); }
    bool empty() const { return m_bonds.empty(); }

    const Bond& bond(size_t index) const { return m_bonds[index]; }
    Bond& bond(size_t index) { return m_bonds[index]; }

    const std::vector<Bond>& bonds() const { return m_bonds; }
    std::vector<Bond>& bonds() { return m_bonds; }

    // Raw access for rendering
    const uint32_t* atomIndices1() const;
    const uint32_t* atomIndices2() const;

    /**
     * @brief Get all bonds for a specific atom
     */
    std::vector<size_t> bondsForAtom(uint32_t atomIndex) const;

    /**
     * @brief Auto-detect bonds based on distances
     * @param posX X positions array
     * @param posY Y positions array
     * @param posZ Z positions array
     * @param types Atom types array
     * @param atomCount Number of atoms
     * @param tolerance Bond distance tolerance factor (default 1.2)
     */
    void detectBonds(const float* posX, const float* posY, const float* posZ,
                     const int* types, size_t atomCount, float tolerance = 1.2f);

private:
    std::vector<Bond> m_bonds;
};

} // namespace atom::data
