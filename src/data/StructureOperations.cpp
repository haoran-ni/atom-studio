#include "StructureOperations.h"

#include <array>
#include <cmath>
#include <queue>
#include <vector>

namespace atom::data {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/** Returns true for elements commonly found in organic molecules
 *  (non-metals and metalloids; excludes all metals and noble gases). */
static bool isOrganicElement(int Z) {
    // H, B, C, N, O, F, Si, P, S, Cl, Ge, As, Se, Br, Sb, Te, I, At
    static const int kOrganic[] = {
        1, 5, 6, 7, 8, 9, 14, 15, 16, 17, 32, 33, 34, 35, 51, 52, 53, 85
    };
    for (int z : kOrganic)
        if (Z == z) return true;
    return false;
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
    // 1. Precompute fractional coordinates for every atom.
    // ------------------------------------------------------------------
    std::vector<std::array<double, 3>> frac(n);
    for (size_t i = 0; i < n; ++i) {
        frac[i] = lat.cartesianToFractional(
            s.positionsX()[i],
            s.positionsY()[i],
            s.positionsZ()[i]);
    }

    // ------------------------------------------------------------------
    // 2. Classify each atom as organic (will be moved) or not.
    // ------------------------------------------------------------------
    std::vector<bool> organic(n);
    for (size_t i = 0; i < n; ++i)
        organic[i] = isOrganicElement(s.atomicNumber(i));

    // ------------------------------------------------------------------
    // 3. Build adjacency list using only organic–organic bonds.
    //    For a bond with atomIndex1=i, atomIndex2=j, imageX/Y/Z:
    //      real_pos[j] = pos[j] + imageX*a + imageY*b + imageZ*c
    //    Traversing i→j: add (imageX, imageY, imageZ) to j's offset.
    //    Traversing j→i: add (-imageX, -imageY, -imageZ) to i's offset.
    // ------------------------------------------------------------------
    struct AdjEntry { size_t neighbor; int dx, dy, dz; };
    std::vector<std::vector<AdjEntry>> adj(n);

    for (size_t b = 0; b < bonds.bondCount(); ++b) {
        const Bond& bond = bonds.bond(b);
        const size_t i = bond.atomIndex1;
        const size_t j = bond.atomIndex2;
        if (!organic[i] || !organic[j]) continue;
        adj[i].push_back({j,  bond.imageX,  bond.imageY,  bond.imageZ});
        adj[j].push_back({i, -bond.imageX, -bond.imageY, -bond.imageZ});
    }

    // ------------------------------------------------------------------
    // 4. BFS over connected components of organic atoms.
    //    For each atom we accumulate an integer lattice-vector offset so
    //    that the atom's effective fractional position is
    //      frac[i] + offset[i]
    //    keeping it contiguous with its bonded neighbours.
    // ------------------------------------------------------------------
    std::vector<std::array<int, 3>> offset(n, {0, 0, 0});
    std::vector<bool> visited(n, false);
    std::vector<size_t> component;
    std::queue<size_t> queue;

    for (size_t start = 0; start < n; ++start) {
        if (!organic[start] || visited[start]) continue;

        component.clear();
        visited[start] = true;
        queue.push(start);

        while (!queue.empty()) {
            const size_t u = queue.front();
            queue.pop();
            component.push_back(u);

            for (const AdjEntry& edge : adj[u]) {
                const size_t v = edge.neighbor;
                if (visited[v]) continue;
                visited[v] = true;
                offset[v][0] = offset[u][0] + edge.dx;
                offset[v][1] = offset[u][1] + edge.dy;
                offset[v][2] = offset[u][2] + edge.dz;
                queue.push(v);
            }
        }

        // --------------------------------------------------------------
        // 5. Compute the geometric centre of this fragment
        //    in adjusted fractional coordinates.
        // --------------------------------------------------------------
        double cx = 0.0, cy = 0.0, cz = 0.0;
        for (const size_t idx : component) {
            cx += frac[idx][0] + offset[idx][0];
            cy += frac[idx][1] + offset[idx][1];
            cz += frac[idx][2] + offset[idx][2];
        }
        const double inv = 1.0 / static_cast<double>(component.size());
        cx *= inv;
        cy *= inv;
        cz *= inv;

        // Integer shift to bring the centre into [0, 1)^3.
        const int shiftX = static_cast<int>(std::floor(cx));
        const int shiftY = static_cast<int>(std::floor(cy));
        const int shiftZ = static_cast<int>(std::floor(cz));

        // --------------------------------------------------------------
        // 6. Apply the final fractional coordinates back to Cartesian.
        // --------------------------------------------------------------
        for (const size_t idx : component) {
            const double fx = frac[idx][0] + (offset[idx][0] - shiftX);
            const double fy = frac[idx][1] + (offset[idx][1] - shiftY);
            const double fz = frac[idx][2] + (offset[idx][2] - shiftZ);
            const auto cart = lat.fractionalToCartesian(fx, fy, fz);
            s.setPosition(idx,
                          static_cast<float>(cart[0]),
                          static_cast<float>(cart[1]),
                          static_cast<float>(cart[2]));
        }
    }
}

} // namespace atom::data
