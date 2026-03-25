#include "StructureOperations.h"

#include <array>
#include <cmath>
#include <queue>
#include <vector>

namespace atom::data {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/** Returns true for valid elements that have a recorded van der Waals radius. */
static bool isUnwrappableElement(int atomicNumber) {
    if (atomicNumber <= 0 || atomicNumber >= ElementData::MAX_ELEMENTS) return false;
    const ElementInfo& element = ElementData::byAtomicNumber(atomicNumber);
    return element.atomicNumber != 0 && element.vdwRadius > 0.0f;
}

std::unique_ptr<Structure> replicateCell(const Structure& src, int nx, int ny, int nz) {
    if (!src.hasLattice() || nx < 1 || ny < 1 || nz < 1)
        return nullptr;

    const size_t srcCount = src.atomCount();
    const size_t totalCount = srcCount * static_cast<size_t>(nx * ny * nz);
    const Lattice& lat = src.lattice();

    // Lattice vectors
    const auto& a = lat.matrix[0];
    const auto& b = lat.matrix[1];
    const auto& c = lat.matrix[2];

    auto s = std::make_unique<Structure>();
    s->reserve(totalCount);

    const bool hasVel    = src.hasVelocities();
    const bool hasForce  = src.hasForces();
    const bool hasCharge = src.hasCharges();
    const bool hasMass   = src.hasMasses();

    for (int iz = 0; iz < nz; ++iz) {
        for (int iy = 0; iy < ny; ++iy) {
            for (int ix = 0; ix < nx; ++ix) {
                const float dx = static_cast<float>(ix * a[0] + iy * b[0] + iz * c[0]);
                const float dy = static_cast<float>(ix * a[1] + iy * b[1] + iz * c[1]);
                const float dz = static_cast<float>(ix * a[2] + iy * b[2] + iz * c[2]);

                for (size_t at = 0; at < srcCount; ++at) {
                    const auto pos = src.position(at);
                    const size_t idx = s->addAtom(
                        pos[0] + dx,
                        pos[1] + dy,
                        pos[2] + dz,
                        src.atomicNumber(at),
                        src.symbol(at)
                    );

                    // Override rendering properties with source values
                    s->radii()[idx]   = src.radii()[at];
                    s->colorsR()[idx] = src.colorsR()[at];
                    s->colorsG()[idx] = src.colorsG()[at];
                    s->colorsB()[idx] = src.colorsB()[at];
                    s->colorsA()[idx] = src.colorsA()[at];
                }
            }
        }
    }

    // Replicate optional per-atom arrays
    if (hasVel || hasForce || hasCharge || hasMass) {
        std::vector<float> vx, vy, vz, fx, fy, fz, charges, masses;
        if (hasVel)    { vx.reserve(totalCount); vy.reserve(totalCount); vz.reserve(totalCount); }
        if (hasForce)  { fx.reserve(totalCount); fy.reserve(totalCount); fz.reserve(totalCount); }
        if (hasCharge) { charges.reserve(totalCount); }
        if (hasMass)   { masses.reserve(totalCount); }

        const int copies = nx * ny * nz;
        for (int c = 0; c < copies; ++c) {
            for (size_t at = 0; at < srcCount; ++at) {
                if (hasVel) {
                    vx.push_back(src.velocitiesX()[at]);
                    vy.push_back(src.velocitiesY()[at]);
                    vz.push_back(src.velocitiesZ()[at]);
                }
                if (hasForce) {
                    fx.push_back(src.forcesX()[at]);
                    fy.push_back(src.forcesY()[at]);
                    fz.push_back(src.forcesZ()[at]);
                }
                if (hasCharge) { charges.push_back(src.charges()[at]); }
                if (hasMass)   { masses.push_back(src.masses()[at]); }
            }
        }
        if (hasVel)    s->setVelocities(std::move(vx), std::move(vy), std::move(vz));
        if (hasForce)  s->setForces(std::move(fx), std::move(fy), std::move(fz));
        if (hasCharge) s->setCharges(std::move(charges));
        if (hasMass)   s->setMasses(std::move(masses));
    }

    // Scale lattice vectors
    Lattice newLat = lat;
    for (int d = 0; d < 3; ++d) newLat.matrix[0][d] *= nx;
    for (int d = 0; d < 3; ++d) newLat.matrix[1][d] *= ny;
    for (int d = 0; d < 3; ++d) newLat.matrix[2][d] *= nz;
    s->lattice() = newLat;

    // Metadata
    s->setSourcePath(src.sourcePath());
    s->setName(src.name());
    for (const auto& [key, val] : src.info()) s->setInfo(key, val);

    // Bonds are intentionally left empty — caller should trigger re-detection.

    return s;
}

void unwrapMolecules(Structure& s) {
    if (!s.hasLattice()) return;
    const size_t n = s.atomCount();
    if (n == 0) return;
    const BondList& bonds = s.bonds();
    if (bonds.empty()) return;

    const Lattice& lat = s.lattice();

    // ------------------------------------------------------------------
    // 1. Precompute wrapped fractional coordinates for every atom.
    // ------------------------------------------------------------------
    std::vector<std::array<double, 3>> wrappedFrac(n);
    std::vector<std::array<int, 3>> atomWrap(n, {0, 0, 0});
    for (size_t i = 0; i < n; ++i) {
        auto frac = lat.cartesianToFractional(
            s.positionsX()[i],
            s.positionsY()[i],
            s.positionsZ()[i]);

        wrappedFrac[i] = frac;
        for (size_t axis = 0; axis < 3; ++axis) {
            if (!lat.pbc[axis]) continue;
            const int wrap = static_cast<int>(std::floor(frac[axis]));
            atomWrap[i][axis] = wrap;
            wrappedFrac[i][axis] = frac[axis] - static_cast<double>(wrap);
        }
    }

    // ------------------------------------------------------------------
    // 2. Classify each atom as eligible for unwrap or not.
    // ------------------------------------------------------------------
    std::vector<bool> unwrappable(n);
    for (size_t i = 0; i < n; ++i)
        unwrappable[i] = isUnwrappableElement(s.atomicNumber(i));

    // ------------------------------------------------------------------
    // 3. Build adjacency list using only unwrappable-element bonds.
    //    For wrapped fractional coordinates, each bond shift must also account
    //    for any integer-cell wrapping already present in the stored positions.
    // ------------------------------------------------------------------
    struct AdjEntry { size_t neighbor; int dx, dy, dz; };
    std::vector<std::vector<AdjEntry>> adj(n);

    for (size_t b = 0; b < bonds.bondCount(); ++b) {
        const Bond& bond = bonds.bond(b);
        const size_t i = bond.atomIndex1;
        const size_t j = bond.atomIndex2;
        if (!unwrappable[i] || !unwrappable[j]) continue;

        const int dx = static_cast<int>(bond.imageX) + atomWrap[j][0] - atomWrap[i][0];
        const int dy = static_cast<int>(bond.imageY) + atomWrap[j][1] - atomWrap[i][1];
        const int dz = static_cast<int>(bond.imageZ) + atomWrap[j][2] - atomWrap[i][2];

        adj[i].push_back({j,  dx,  dy,  dz});
        adj[j].push_back({i, -dx, -dy, -dz});
    }

    // ------------------------------------------------------------------
    // 4. BFS over connected components of unwrappable atoms.
    //    For each atom we accumulate an integer lattice-vector offset so
    //    that the atom's effective fractional position is
    //      wrappedFrac[i] + offset[i]
    //    keeping it contiguous with its bonded neighbours.
    // ------------------------------------------------------------------
    std::vector<std::array<int, 3>> offset(n, {0, 0, 0});
    std::vector<bool> visited(n, false);
    std::vector<size_t> component;
    std::queue<size_t> queue;

    for (size_t start = 0; start < n; ++start) {
        if (!unwrappable[start] || visited[start]) continue;

        component.clear();
        visited[start] = true;
        offset[start] = {0, 0, 0};
        queue.push(start);
        bool consistent = true;

        while (!queue.empty()) {
            const size_t u = queue.front();
            queue.pop();
            component.push_back(u);

            for (const AdjEntry& edge : adj[u]) {
                const size_t v = edge.neighbor;
                const std::array<int, 3> candidate = {
                    offset[u][0] + edge.dx,
                    offset[u][1] + edge.dy,
                    offset[u][2] + edge.dz
                };

                if (!visited[v]) {
                    visited[v] = true;
                    offset[v] = candidate;
                    queue.push(v);
                    continue;
                }

                if (offset[v] != candidate) {
                    consistent = false;
                }
            }
        }

        if (!consistent) continue;

        // --------------------------------------------------------------
        // 5. Keep the largest wrapped fragment inside the unit cell.
        //    Atoms sharing the same offset belong to the same wrapped
        //    fragment in the original wrapped structure.
        // --------------------------------------------------------------
        std::vector<bool> inComponent(n, false);
        for (const size_t idx : component) {
            inComponent[idx] = true;
        }

        std::vector<bool> wrappedVisited(n, false);
        std::array<int, 3> anchorOffset = offset[component.front()];
        size_t largestSize = 0;
        for (const size_t idx : component) {
            if (wrappedVisited[idx]) continue;

            size_t size = 0;
            std::queue<size_t> wrappedQueue;
            wrappedQueue.push(idx);
            wrappedVisited[idx] = true;

            while (!wrappedQueue.empty()) {
                const size_t u = wrappedQueue.front();
                wrappedQueue.pop();
                ++size;

                for (const AdjEntry& edge : adj[u]) {
                    if (edge.dx != 0 || edge.dy != 0 || edge.dz != 0) continue;
                    const size_t v = edge.neighbor;
                    if (!inComponent[v] || wrappedVisited[v]) continue;
                    wrappedVisited[v] = true;
                    wrappedQueue.push(v);
                }
            }

            if (size > largestSize) {
                largestSize = size;
                anchorOffset = offset[idx];
            }
        }

        // --------------------------------------------------------------
        // 6. Apply the final fractional coordinates back to Cartesian.
        // --------------------------------------------------------------
        for (const size_t idx : component) {
            const double fx = wrappedFrac[idx][0] + (offset[idx][0] - anchorOffset[0]);
            const double fy = wrappedFrac[idx][1] + (offset[idx][1] - anchorOffset[1]);
            const double fz = wrappedFrac[idx][2] + (offset[idx][2] - anchorOffset[2]);
            const auto cart = lat.fractionalToCartesian(fx, fy, fz);
            s.setPosition(idx,
                          static_cast<float>(cart[0]),
                          static_cast<float>(cart[1]),
                          static_cast<float>(cart[2]));
        }
    }
}

} // namespace atom::data
