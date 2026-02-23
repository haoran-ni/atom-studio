#pragma once

#include "Structure.h"
#include <memory>

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

} // namespace atom::data
