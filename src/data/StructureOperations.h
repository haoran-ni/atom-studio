#pragma once

#include "Structure.h"
#include <memory>
#include <vector>

namespace atom::data {

/**
 * @brief Build a supercell by replicating the unit cell of a periodic structure.
 *
 * The source structure must have a lattice defined.
 * Each atom at position r is replicated at r + i*a + j*b + k*c for
 * i in [0, nx), j in [0, ny), k in [0, nz).
 * The new lattice vectors are nx*a, ny*b, nz*c.
 * All per-atom optional data (velocities, forces, charges, masses) is replicated.
 * Rendering properties (radii, colors) are replicated.
 * Bonds are cleared — bond detection should be re-run on the result.
 *
 * @param src  Source structure (must have a lattice).
 * @param nx   Replication count along lattice vector a (>= 1).
 * @param ny   Replication count along lattice vector b (>= 1).
 * @param nz   Replication count along lattice vector c (>= 1).
 * @return New structure representing the supercell, or nullptr if src has no lattice
 *         or replication counts are invalid.
 */
std::unique_ptr<Structure> replicateCell(const Structure& src, int nx, int ny, int nz);

/**
 * @brief Unwrap molecules so that bonded atoms are adjacent in Cartesian space.
 *
 * For each connected fragment whose atoms have a recorded van der Waals radius,
 * the BondList image shifts are used to resolve which periodic image of each
 * atom keeps the fragment contiguous. If the wrapped structure shows the same
 * molecule as multiple fragments inside the unit cell, the largest wrapped
 * fragment is kept in-cell and the remaining fragments are translated to
 * reconnect to it.
 *
 * Requirements:
 *   - @p s must have a lattice defined.
 *   - @p s must have a non-empty BondList (call bond detection first).
 *
 * The structure is modified in-place.
 */
void unwrapMolecules(Structure& s);

/**
 * @brief Connected component returned by the same bond graph used by unwrap.
 */
struct ConnectedSelection {
    std::vector<size_t> atoms;
    std::vector<size_t> bonds;
};

/**
 * @brief Find the atom/bond component connected to an atom using unwrap's graph.
 *
 * If the atom is not eligible for that graph, the returned component contains
 * only the atom.
 */
ConnectedSelection connectedSelectionFromAtom(const Structure& s, size_t atomIndex);

/**
 * @brief Find the atom/bond component connected to a bond using unwrap's graph.
 *
 * If the bond is not part of unwrap's graph, the returned component contains
 * the bond and its two endpoint atoms.
 */
ConnectedSelection connectedSelectionFromBond(const Structure& s, size_t bondIndex);

} // namespace atom::data
