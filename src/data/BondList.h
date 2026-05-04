#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace atom::data {

/**
 * @brief Bond order enumeration
 */
enum class BondOrder : uint8_t {
    Single   = 1,
    Double   = 2,
    Triple   = 3,
    Aromatic = 4,
    Unknown  = 0
};

/**
 * @brief A single bond between two atoms.
 *
 * atomIndex1 < atomIndex2 (invariant enforced by addBond).
 * imageX/Y/Z give the periodic image of atomIndex2 relative to atomIndex1:
 *   real_pos2 = pos[atomIndex2] + imageX*a + imageY*b + imageZ*c
 * For non-PBC structures all image shifts are zero.
 */
struct Bond {
    uint32_t  atomIndex1;
    uint32_t  atomIndex2;
    int8_t    imageX = 0;
    int8_t    imageY = 0;
    int8_t    imageZ = 0;
    BondOrder order  = BondOrder::Single;

    Bond() = default;
    Bond(uint32_t a1, uint32_t a2,
         int8_t ix = 0, int8_t iy = 0, int8_t iz = 0,
         BondOrder ord = BondOrder::Single)
        : atomIndex1(a1), atomIndex2(a2)
        , imageX(ix), imageY(iy), imageZ(iz)
        , order(ord) {}
};

/**
 * @brief Container for bond connectivity.
 *
 * Bonds are stored with atomIndex1 < atomIndex2.
 * Image shifts describe which periodic image of atomIndex2 is bonded.
 */
class BondList {
public:
    BondList() = default;

    void reserve(size_t count);
    void clear();

    /**
     * @brief Add a bond (normalises so atomIndex1 < atomIndex2, negates
     *        image shifts if indices are swapped).
     */
    size_t addBond(uint32_t atomIndex1, uint32_t atomIndex2,
                   int8_t imageX = 0, int8_t imageY = 0, int8_t imageZ = 0,
                   BondOrder order = BondOrder::Single);

    void removeBond(size_t bondIndex);

    /** @return Bond index, or -1 if not found. */
    int  findBond(uint32_t atomIndex1, uint32_t atomIndex2,
                  int8_t imageX = 0, int8_t imageY = 0, int8_t imageZ = 0) const;
    bool areBonded(uint32_t atomIndex1, uint32_t atomIndex2,
                   int8_t imageX = 0, int8_t imageY = 0, int8_t imageZ = 0) const;

    // Accessors
    size_t bondCount() const { return m_bonds.size(); }
    bool   empty()     const { return m_bonds.empty(); }

    const Bond& bond(size_t index) const { return m_bonds[index]; }
    Bond&       bond(size_t index)       { return m_bonds[index]; }

    const std::vector<Bond>& bonds() const { return m_bonds; }
    std::vector<Bond>&       bonds()       { return m_bonds; }

    std::vector<size_t> bondsForAtom(uint32_t atomIndex) const;

    // Per-bond rendering state
    float radius(size_t bondIndex) const { return m_radii[bondIndex]; }
    void setRadius(size_t bondIndex, float radius);
    void setAllRadii(float radius);
    const std::vector<float>& radii() const { return m_radii; }

    // Selection state
    bool selected(size_t bondIndex) const { return m_selected[bondIndex] != 0; }
    void setSelected(size_t bondIndex, bool selected);
    void toggleSelected(size_t bondIndex);
    void clearSelection();
    size_t selectedCount() const;
    const std::vector<uint8_t>& selectionMask() const { return m_selected; }

private:
    std::vector<Bond> m_bonds;
    std::vector<float> m_radii;
    std::vector<uint8_t> m_selected;
};

} // namespace atom::data
