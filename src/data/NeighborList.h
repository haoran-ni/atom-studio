#pragma once

#include "ElementData.h"
#include "Structure.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace atom::data {

class BondList;

/**
 * @brief One entry in the symmetric neighbor list for atom i.
 *
 * 'index' is the SoA index of the neighboring atom j.
 * imageX/Y/Z are integer image shifts: the real-space position of j that is
 * within the cutoff of i is  pos[j] + imageX*a + imageY*b + imageZ*c,
 * where a, b, c are the lattice vectors.  For non-PBC structures all shifts
 * are zero.
 */
struct NeighborEntry {
    uint32_t index;
    int8_t   imageX;
    int8_t   imageY;
    int8_t   imageZ;
};

/**
 * @brief Cell-list based neighbor list for bond detection.
 *
 * Atoms whose element has no defined covalent radius (covalentRadius < 0)
 * are excluded entirely — they appear in no atom's neighbor list.
 *
 * Storage layout: CSR (Compressed Sparse Row).
 *   m_neighbors[m_offsets[i] .. m_offsets[i+1])  → all neighbors of atom i
 *
 * The list is symmetric: if j is in i's list, i is in j's list (with negated
 * image shifts).
 *
 * Usage:
 *   NeighborList nl;
 *   nl.build(structure, scale);
 *   auto bonds = NeighborList::buildBondList(nl, structure, scale);
 */
class NeighborList {
public:
    NeighborList() = default;

    /**
     * @brief Build the neighbor list from a structure.
     *
     * @param structure   Source atomic structure (positions, atomic numbers, lattice).
     * @param scale       Bond scale factor: bond exists if dist < (r_cov_i + r_cov_j) * scale.
     *                    Determines the global cutoff used for the cell grid.
     */
    void build(const Structure& structure, float scale);

    // ── Accessors ────────────────────────────────────────────────────────────

    size_t atomCount() const { return m_atomCount; }
    bool   empty()     const { return m_neighbors.empty(); }

    /** Returns a pointer to the first NeighborEntry for atom i (may be null if count == 0). */
    const NeighborEntry* neighborsOf(size_t atomIdx) const {
        if (atomIdx >= m_atomCount) return nullptr;
        return m_neighbors.data() + m_offsets[atomIdx];
    }

    size_t neighborCount(size_t atomIdx) const {
        if (atomIdx >= m_atomCount) return 0;
        return m_offsets[atomIdx + 1] - m_offsets[atomIdx];
    }

    // ── Bond detection pass ───────────────────────────────────────────────────

    /**
     * @brief Build a BondList from this neighbor list using covalent-radius criterion.
     *
     * Each atom pair (i, j) with i < j is bonded if
     *   distance(i, j_image) < (r_cov_i + r_cov_j) * scale
     * Atoms with undefined covalent radius are skipped.
     *
     * @param structure Source structure (positions, atomic numbers, lattice).
     * @param scale     Bond scale factor.
     * @return New BondList (heap-allocated).
     */
    std::shared_ptr<BondList> buildBondList(const Structure& structure, float scale) const;

private:
    // ── Internal helpers ────────────────────────────────────────────────────

    // Build using a Cartesian bounding-box cell grid (non-PBC or PBC).
    void buildCellList(
        const float* posX, const float* posY, const float* posZ,
        const int*   atomicNumbers, size_t atomCount,
        const Lattice& lattice, float globalCutoff,
        const std::vector<uint32_t>& activeAtoms,
        const std::vector<float>&    covRadii);

    // Apply minimum image convention for PBC displacement.
    // Returns the corrected displacement and updates imageX/Y/Z.
    // Only axes where pbc[k] is true are wrapped.
    static void applyMIC(
        float& dx, float& dy, float& dz,
        int8_t& imgX, int8_t& imgY, int8_t& imgZ,
        const Lattice& lattice,
        const std::array<bool, 3>& pbc);

    // ── CSR storage ─────────────────────────────────────────────────────────
    std::vector<NeighborEntry> m_neighbors; // flat neighbor array
    std::vector<uint32_t>      m_offsets;   // size = m_atomCount + 1
    size_t                     m_atomCount = 0;
};

} // namespace atom::data
