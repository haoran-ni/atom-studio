#include "NeighborList.h"
#include "BondList.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace atom::data {

// ============================================================================
// Minimum Image Convention
// ============================================================================

void NeighborList::applyMIC(float& dx, float& dy, float& dz,
                             int8_t& imgX, int8_t& imgY, int8_t& imgZ,
                             const Lattice& lattice,
                             const std::array<bool, 3>& pbc)
{
    // Project displacement into fractional coordinates
    auto frac = lattice.cartesianToFractional(
        static_cast<double>(dx),
        static_cast<double>(dy),
        static_cast<double>(dz));

    // Round only periodic axes; non-periodic axes keep their displacement
    double ridx = pbc[0] ? std::round(frac[0]) : 0.0;
    double ridy = pbc[1] ? std::round(frac[1]) : 0.0;
    double ridz = pbc[2] ? std::round(frac[2]) : 0.0;

    if (ridx != 0.0 || ridy != 0.0 || ridz != 0.0) {
        // Shift the image accumulators
        imgX = static_cast<int8_t>(static_cast<int>(imgX) - static_cast<int>(ridx));
        imgY = static_cast<int8_t>(static_cast<int>(imgY) - static_cast<int>(ridy));
        imgZ = static_cast<int8_t>(static_cast<int>(imgZ) - static_cast<int>(ridz));

        // Correct the Cartesian displacement
        auto shift = lattice.fractionalToCartesian(ridx, ridy, ridz);
        dx -= static_cast<float>(shift[0]);
        dy -= static_cast<float>(shift[1]);
        dz -= static_cast<float>(shift[2]);
    }
}

// ============================================================================
// build()
// ============================================================================

void NeighborList::build(const Structure& structure, float scale)
{
    const size_t n = structure.atomCount();
    m_atomCount = n;
    m_offsets.assign(n + 1, 0u);
    m_neighbors.clear();

    if (!std::isfinite(scale) || scale <= 0.0f) return;
    if (n == 0) return;

    const float* posX = structure.positionsX();
    const float* posY = structure.positionsY();
    const float* posZ = structure.positionsZ();
    const int*   atomicNums = structure.atomicNumbers();
    const Lattice& lattice  = structure.lattice();

    // ── 1. Identify active atoms and their covalent radii ───────────────────
    // Active = has a defined covalent radius (>= 0)
    std::vector<uint32_t> activeAtoms;
    std::vector<float>    covRadii(n, -1.0f);
    float maxCovRadius = 0.0f;

    activeAtoms.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        auto r = ElementData::covalentRadius(atomicNums[i]);
        if (r.has_value()) {
            covRadii[i] = r.value();
            activeAtoms.push_back(static_cast<uint32_t>(i));
            if (r.value() > maxCovRadius) maxCovRadius = r.value();
        }
    }

    if (activeAtoms.empty() || maxCovRadius <= 0.0f) return;

    // ── 2. Compute global cutoff ─────────────────────────────────────────────
    const float globalCutoff = 2.0f * maxCovRadius * scale;

    // ── 3. Delegate to cell-list builder ────────────────────────────────────
    buildCellList(posX, posY, posZ, atomicNums, n,
                  lattice, globalCutoff, activeAtoms, covRadii);
}

// ============================================================================
// buildCellList()
// ============================================================================

void NeighborList::buildCellList(
    const float* posX, const float* posY, const float* posZ,
    const int*   /*atomicNumbers*/, size_t atomCount,
    const Lattice& lattice, float globalCutoff,
    const std::vector<uint32_t>& activeAtoms,
    const std::vector<float>&    covRadii)
{
    const bool hasPBC = lattice.defined &&
                        (lattice.pbc[0] || lattice.pbc[1] || lattice.pbc[2]);

    // ── Grid dimensions ──────────────────────────────────────────────────────
    // For PBC directions use the lattice vector lengths; for non-PBC use the
    // Cartesian bounding box of active atom positions.

    float xmin, ymin, zmin, xmax, ymax, zmax;

    if (hasPBC && lattice.pbc[0] && lattice.pbc[1] && lattice.pbc[2]) {
        // Full PBC: use lattice origin (0,0,0) and lattice vector extents
        xmin = ymin = zmin = 0.0f;
        // Approximate Cartesian extents of the parallelepiped
        xmax = static_cast<float>(std::abs(lattice.matrix[0][0]) +
                                   std::abs(lattice.matrix[1][0]) +
                                   std::abs(lattice.matrix[2][0]));
        ymax = static_cast<float>(std::abs(lattice.matrix[0][1]) +
                                   std::abs(lattice.matrix[1][1]) +
                                   std::abs(lattice.matrix[2][1]));
        zmax = static_cast<float>(std::abs(lattice.matrix[0][2]) +
                                   std::abs(lattice.matrix[1][2]) +
                                   std::abs(lattice.matrix[2][2]));
    } else {
        // Non-PBC or partial PBC: use bounding box of active atoms
        xmin = ymin = zmin =  std::numeric_limits<float>::max();
        xmax = ymax = zmax = -std::numeric_limits<float>::max();
        for (uint32_t idx : activeAtoms) {
            if (posX[idx] < xmin) xmin = posX[idx];
            if (posY[idx] < ymin) ymin = posY[idx];
            if (posZ[idx] < zmin) zmin = posZ[idx];
            if (posX[idx] > xmax) xmax = posX[idx];
            if (posY[idx] > ymax) ymax = posY[idx];
            if (posZ[idx] > zmax) zmax = posZ[idx];
        }
        // Pad by cutoff so edge atoms are covered
        xmin -= globalCutoff; ymin -= globalCutoff; zmin -= globalCutoff;
        xmax += globalCutoff; ymax += globalCutoff; zmax += globalCutoff;
    }

    float boxX = xmax - xmin;
    float boxY = ymax - ymin;
    float boxZ = zmax - zmin;

    int nx = std::max(1, static_cast<int>(boxX / globalCutoff));
    int ny = std::max(1, static_cast<int>(boxY / globalCutoff));
    int nz = std::max(1, static_cast<int>(boxZ / globalCutoff));

    float csX = boxX / static_cast<float>(nx);
    float csY = boxY / static_cast<float>(ny);
    float csZ = boxZ / static_cast<float>(nz);

    // ── Bin active atoms into cells ──────────────────────────────────────────
    const int nCells = nx * ny * nz;
    std::vector<std::vector<uint32_t>> cellAtoms(nCells);

    auto cellOf = [&](float x, float y, float z) -> int {
        int ix = std::clamp(static_cast<int>((x - xmin) / csX), 0, nx - 1);
        int iy = std::clamp(static_cast<int>((y - ymin) / csY), 0, ny - 1);
        int iz = std::clamp(static_cast<int>((z - zmin) / csZ), 0, nz - 1);
        return ix + iy * nx + iz * nx * ny;
    };

    for (uint32_t idx : activeAtoms) {
        int c = cellOf(posX[idx], posY[idx], posZ[idx]);
        cellAtoms[c].push_back(idx);
    }

    // ── Build per-atom neighbor lists (temporary) ───────────────────────────
    // We accumulate into per-atom vectors, then pack into CSR.
    std::vector<std::vector<NeighborEntry>> perAtom(atomCount);

    const float cutoff2 = globalCutoff * globalCutoff;

    for (uint32_t i : activeAtoms) {
        float xi = posX[i], yi = posY[i], zi = posZ[i];
        int iCell = cellOf(xi, yi, zi);

        int ix = iCell % nx;
        int iy = (iCell / nx) % ny;
        int iz = iCell / (nx * ny);

        for (int dz = -1; dz <= 1; ++dz) {
            int jz_raw = iz + dz;
            int imgZ_base = 0;
            int jz;
            if (hasPBC && lattice.pbc[2]) {
                if      (jz_raw < 0)  { jz = jz_raw + nz; imgZ_base = -1; }
                else if (jz_raw >= nz){ jz = jz_raw - nz; imgZ_base = +1; }
                else                  { jz = jz_raw; }
            } else {
                if (jz_raw < 0 || jz_raw >= nz) continue;
                jz = jz_raw;
            }

            for (int dy = -1; dy <= 1; ++dy) {
                int jy_raw = iy + dy;
                int imgY_base = 0;
                int jy;
                if (hasPBC && lattice.pbc[1]) {
                    if      (jy_raw < 0)  { jy = jy_raw + ny; imgY_base = -1; }
                    else if (jy_raw >= ny){ jy = jy_raw - ny; imgY_base = +1; }
                    else                  { jy = jy_raw; }
                } else {
                    if (jy_raw < 0 || jy_raw >= ny) continue;
                    jy = jy_raw;
                }

                for (int dx = -1; dx <= 1; ++dx) {
                    int jx_raw = ix + dx;
                    int imgX_base = 0;
                    int jx;
                    if (hasPBC && lattice.pbc[0]) {
                        if      (jx_raw < 0)  { jx = jx_raw + nx; imgX_base = -1; }
                        else if (jx_raw >= nx){ jx = jx_raw - nx; imgX_base = +1; }
                        else                  { jx = jx_raw; }
                    } else {
                        if (jx_raw < 0 || jx_raw >= nx) continue;
                        jx = jx_raw;
                    }

                    int jCell = jx + jy * nx + jz * nx * ny;

                    for (uint32_t j : cellAtoms[jCell]) {
                        if (j == i) continue;

                        float fdx = posX[j] - xi;
                        float fdy = posY[j] - yi;
                        float fdz = posZ[j] - zi;

                        int8_t imgX = static_cast<int8_t>(imgX_base);
                        int8_t imgY = static_cast<int8_t>(imgY_base);
                        int8_t imgZ = static_cast<int8_t>(imgZ_base);

                        // Add lattice image displacement
                        if (imgX_base != 0 || imgY_base != 0 || imgZ_base != 0) {
                            const auto& m = lattice.matrix;
                            fdx += static_cast<float>(imgX_base * m[0][0] + imgY_base * m[1][0] + imgZ_base * m[2][0]);
                            fdy += static_cast<float>(imgX_base * m[0][1] + imgY_base * m[1][1] + imgZ_base * m[2][1]);
                            fdz += static_cast<float>(imgX_base * m[0][2] + imgY_base * m[1][2] + imgZ_base * m[2][2]);
                        }

                        // Apply MIC for PBC to ensure nearest image
                        if (hasPBC) {
                            applyMIC(fdx, fdy, fdz, imgX, imgY, imgZ, lattice, lattice.pbc);
                        }

                        float d2 = fdx * fdx + fdy * fdy + fdz * fdz;
                        if (d2 < cutoff2 && d2 > 0.0f) {
                            // j is from cellAtoms which contains only active atoms (defined covalent radius).
                            perAtom[i].push_back({j, imgX, imgY, imgZ});
                        }
                    }
                }
            }
        }
    }

    // ── Deduplicate per-atom neighbor lists ──────────────────────────────────
    // When any cell dimension is 1, multiple (dx,dy,dz) offsets wrap to the same
    // physical cell, causing the same NeighborEntry to be pushed multiple times.
    // Sort and unique each list to collapse identical {index, imgX, imgY, imgZ}
    // tuples before CSR packing.
    for (auto& neighbors : perAtom) {
        std::sort(neighbors.begin(), neighbors.end(),
            [](const NeighborEntry& a, const NeighborEntry& b) {
                if (a.index  != b.index)  return a.index  < b.index;
                if (a.imageX != b.imageX) return a.imageX < b.imageX;
                if (a.imageY != b.imageY) return a.imageY < b.imageY;
                return a.imageZ < b.imageZ;
            });
        neighbors.erase(
            std::unique(neighbors.begin(), neighbors.end(),
                [](const NeighborEntry& a, const NeighborEntry& b) {
                    return a.index  == b.index  &&
                           a.imageX == b.imageX &&
                           a.imageY == b.imageY &&
                           a.imageZ == b.imageZ;
                }),
            neighbors.end());
    }

    // ── Pack into CSR ────────────────────────────────────────────────────────
    m_offsets[0] = 0;
    for (size_t i = 0; i < atomCount; ++i) {
        m_offsets[i + 1] = m_offsets[i] + static_cast<uint32_t>(perAtom[i].size());
    }

    m_neighbors.resize(m_offsets[atomCount]);
    for (size_t i = 0; i < atomCount; ++i) {
        std::copy(perAtom[i].begin(), perAtom[i].end(),
                  m_neighbors.begin() + m_offsets[i]);
    }
}

// ============================================================================
// buildBondList()
// ============================================================================

std::shared_ptr<BondList> NeighborList::buildBondList(
    const Structure& structure, float scale) const
{
    auto bonds = std::make_shared<BondList>();
    if (m_atomCount == 0 || m_neighbors.empty()) return bonds;

    const float* posX = structure.positionsX();
    const float* posY = structure.positionsY();
    const float* posZ = structure.positionsZ();
    const int*   atomicNums = structure.atomicNumbers();
    const Lattice& lattice  = structure.lattice();

    for (size_t i = 0; i < m_atomCount; ++i) {
        auto ri_opt = ElementData::covalentRadius(atomicNums[i]);
        if (!ri_opt.has_value()) continue;
        float ri = ri_opt.value();

        const NeighborEntry* begin = m_neighbors.data() + m_offsets[i];
        const NeighborEntry* end   = m_neighbors.data() + m_offsets[i + 1];

        for (const NeighborEntry* e = begin; e != end; ++e) {
            uint32_t j = e->index;
            if (j <= static_cast<uint32_t>(i)) continue; // each pair once (i < j)

            auto rj_opt = ElementData::covalentRadius(atomicNums[j]);
            if (!rj_opt.has_value()) continue;
            float rj = rj_opt.value();

            // Compute distance to the image of j indicated by the entry
            float fdx = posX[j] - posX[i];
            float fdy = posY[j] - posY[i];
            float fdz = posZ[j] - posZ[i];

            if (e->imageX != 0 || e->imageY != 0 || e->imageZ != 0) {
                const auto& m = lattice.matrix;
                fdx += static_cast<float>(e->imageX * m[0][0] + e->imageY * m[1][0] + e->imageZ * m[2][0]);
                fdy += static_cast<float>(e->imageX * m[0][1] + e->imageY * m[1][1] + e->imageZ * m[2][1]);
                fdz += static_cast<float>(e->imageX * m[0][2] + e->imageY * m[1][2] + e->imageZ * m[2][2]);
            }

            float dist = std::sqrt(fdx * fdx + fdy * fdy + fdz * fdz);

            constexpr float kMinBondDist = 0.4f;
            float threshold = (ri + rj) * scale;

            if (dist > kMinBondDist && dist < threshold) {
                bonds->addBond(static_cast<uint32_t>(i),
                               static_cast<uint32_t>(j),
                               e->imageX, e->imageY, e->imageZ);
            }
        }
    }

    return bonds;
}

} // namespace atom::data
