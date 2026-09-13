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

void NeighborList::build(const Structure& structure, float scale, const std::atomic_bool* cancelled)
{
    m_cancelled = cancelled;
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
        if (m_cancelled && m_cancelled->load(std::memory_order_relaxed)) return;
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
    const std::vector<float>&    /*covRadii*/)
{
    const bool hasPBC = lattice.defined &&
                        (lattice.pbc[0] || lattice.pbc[1] || lattice.pbc[2]);

    struct ImageAtom {
        uint32_t index;
        float    x;
        float    y;
        float    z;
        int8_t   imageX;
        int8_t   imageY;
        int8_t   imageZ;
    };

    auto expandDegenerateBounds = [&](float& lo, float& hi) {
        if (hi <= lo) {
            lo -= 0.5f * globalCutoff;
            hi += 0.5f * globalCutoff;
        }
    };

    // ── Build per-atom neighbor lists (temporary) ───────────────────────────
    // We accumulate into per-atom vectors, then pack into CSR.
    std::vector<std::vector<NeighborEntry>> perAtom(atomCount);
    const float cutoff2 = globalCutoff * globalCutoff;

    if (!hasPBC) {
        float xmin =  std::numeric_limits<float>::max();
        float ymin =  std::numeric_limits<float>::max();
        float zmin =  std::numeric_limits<float>::max();
        float xmax = -std::numeric_limits<float>::max();
        float ymax = -std::numeric_limits<float>::max();
        float zmax = -std::numeric_limits<float>::max();

        for (uint32_t idx : activeAtoms) {
            if (m_cancelled && m_cancelled->load(std::memory_order_relaxed)) return;
            xmin = std::min(xmin, posX[idx]);
            ymin = std::min(ymin, posY[idx]);
            zmin = std::min(zmin, posZ[idx]);
            xmax = std::max(xmax, posX[idx]);
            ymax = std::max(ymax, posY[idx]);
            zmax = std::max(zmax, posZ[idx]);
        }

        xmin -= globalCutoff; ymin -= globalCutoff; zmin -= globalCutoff;
        xmax += globalCutoff; ymax += globalCutoff; zmax += globalCutoff;
        expandDegenerateBounds(xmin, xmax);
        expandDegenerateBounds(ymin, ymax);
        expandDegenerateBounds(zmin, zmax);

        const float boxX = xmax - xmin;
        const float boxY = ymax - ymin;
        const float boxZ = zmax - zmin;

        const int nx = std::max(1, static_cast<int>(boxX / globalCutoff));
        const int ny = std::max(1, static_cast<int>(boxY / globalCutoff));
        const int nz = std::max(1, static_cast<int>(boxZ / globalCutoff));

        const float csX = boxX / static_cast<float>(nx);
        const float csY = boxY / static_cast<float>(ny);
        const float csZ = boxZ / static_cast<float>(nz);

        const int nCells = nx * ny * nz;
        std::vector<std::vector<uint32_t>> cellAtoms(nCells);

        auto cellOf = [&](float x, float y, float z) -> int {
            int ix = std::clamp(static_cast<int>((x - xmin) / csX), 0, nx - 1);
            int iy = std::clamp(static_cast<int>((y - ymin) / csY), 0, ny - 1);
            int iz = std::clamp(static_cast<int>((z - zmin) / csZ), 0, nz - 1);
            return ix + iy * nx + iz * nx * ny;
        };

        for (uint32_t idx : activeAtoms) {
            if (m_cancelled && m_cancelled->load(std::memory_order_relaxed)) return;
            cellAtoms[cellOf(posX[idx], posY[idx], posZ[idx])].push_back(idx);
        }

        for (uint32_t i : activeAtoms) {
            if (m_cancelled && m_cancelled->load(std::memory_order_relaxed)) return;
            const float xi = posX[i];
            const float yi = posY[i];
            const float zi = posZ[i];
            const int iCell = cellOf(xi, yi, zi);
            const int ix = iCell % nx;
            const int iy = (iCell / nx) % ny;
            const int iz = iCell / (nx * ny);

            for (int dz = -1; dz <= 1; ++dz) {
                const int jz = iz + dz;
                if (jz < 0 || jz >= nz) continue;

                for (int dy = -1; dy <= 1; ++dy) {
                    const int jy = iy + dy;
                    if (jy < 0 || jy >= ny) continue;

                    for (int dx = -1; dx <= 1; ++dx) {
                        const int jx = ix + dx;
                        if (jx < 0 || jx >= nx) continue;

                        const int jCell = jx + jy * nx + jz * nx * ny;
                        for (uint32_t j : cellAtoms[jCell]) {
                            if (j == i) continue;

                            const float fdx = posX[j] - xi;
                            const float fdy = posY[j] - yi;
                            const float fdz = posZ[j] - zi;
                            const float d2 = fdx * fdx + fdy * fdy + fdz * fdz;
                            if (d2 < cutoff2 && d2 > 0.0f) {
                                perAtom[i].push_back({j, 0, 0, 0});
                            }
                        }
                    }
                }
            }
        }
    } else {
        const auto& pbc = lattice.pbc;
        const auto& m = lattice.matrix;

        std::vector<float> principalX(atomCount, 0.0f);
        std::vector<float> principalY(atomCount, 0.0f);
        std::vector<float> principalZ(atomCount, 0.0f);
        std::vector<int> wrapX(atomCount, 0);
        std::vector<int> wrapY(atomCount, 0);
        std::vector<int> wrapZ(atomCount, 0);

        std::vector<ImageAtom> imageAtoms;
        const int copiesX = pbc[0] ? 3 : 1;
        const int copiesY = pbc[1] ? 3 : 1;
        const int copiesZ = pbc[2] ? 3 : 1;
        imageAtoms.reserve(activeAtoms.size() * static_cast<size_t>(copiesX * copiesY * copiesZ));

        float xmin =  std::numeric_limits<float>::max();
        float ymin =  std::numeric_limits<float>::max();
        float zmin =  std::numeric_limits<float>::max();
        float xmax = -std::numeric_limits<float>::max();
        float ymax = -std::numeric_limits<float>::max();
        float zmax = -std::numeric_limits<float>::max();

        for (uint32_t idx : activeAtoms) {
            if (m_cancelled && m_cancelled->load(std::memory_order_relaxed)) return;
            auto frac = lattice.cartesianToFractional(
                static_cast<double>(posX[idx]),
                static_cast<double>(posY[idx]),
                static_cast<double>(posZ[idx]));

            std::array<double, 3> wrappedFrac = frac;
            if (pbc[0]) {
                const double base = std::floor(frac[0]);
                wrapX[idx] = static_cast<int>(base);
                wrappedFrac[0] = frac[0] - base;
            }
            if (pbc[1]) {
                const double base = std::floor(frac[1]);
                wrapY[idx] = static_cast<int>(base);
                wrappedFrac[1] = frac[1] - base;
            }
            if (pbc[2]) {
                const double base = std::floor(frac[2]);
                wrapZ[idx] = static_cast<int>(base);
                wrappedFrac[2] = frac[2] - base;
            }

            const auto principal = lattice.fractionalToCartesian(
                wrappedFrac[0], wrappedFrac[1], wrappedFrac[2]);
            principalX[idx] = static_cast<float>(principal[0]);
            principalY[idx] = static_cast<float>(principal[1]);
            principalZ[idx] = static_cast<float>(principal[2]);

            // Enumerate the principal atom and its immediate lattice images.
            // Candidate generation then becomes an ordinary Cartesian cell-list
            // query over real-space image positions, which is robust for
            // triclinic/skewed cells.
            const int minImgX = pbc[0] ? -1 : 0;
            const int maxImgX = pbc[0] ?  1 : 0;
            const int minImgY = pbc[1] ? -1 : 0;
            const int maxImgY = pbc[1] ?  1 : 0;
            const int minImgZ = pbc[2] ? -1 : 0;
            const int maxImgZ = pbc[2] ?  1 : 0;

            for (int imgZ = minImgZ; imgZ <= maxImgZ; ++imgZ) {
                if (m_cancelled && m_cancelled->load(std::memory_order_relaxed)) return;
                for (int imgY = minImgY; imgY <= maxImgY; ++imgY) {
                    for (int imgX = minImgX; imgX <= maxImgX; ++imgX) {
                        const float rx = principalX[idx] + static_cast<float>(
                            imgX * m[0][0] + imgY * m[1][0] + imgZ * m[2][0]);
                        const float ry = principalY[idx] + static_cast<float>(
                            imgX * m[0][1] + imgY * m[1][1] + imgZ * m[2][1]);
                        const float rz = principalZ[idx] + static_cast<float>(
                            imgX * m[0][2] + imgY * m[1][2] + imgZ * m[2][2]);

                        xmin = std::min(xmin, rx);
                        ymin = std::min(ymin, ry);
                        zmin = std::min(zmin, rz);
                        xmax = std::max(xmax, rx);
                        ymax = std::max(ymax, ry);
                        zmax = std::max(zmax, rz);

                        imageAtoms.push_back({
                            idx,
                            rx, ry, rz,
                            static_cast<int8_t>(imgX),
                            static_cast<int8_t>(imgY),
                            static_cast<int8_t>(imgZ)
                        });
                    }
                }
            }
        }

        expandDegenerateBounds(xmin, xmax);
        expandDegenerateBounds(ymin, ymax);
        expandDegenerateBounds(zmin, zmax);

        const float boxX = xmax - xmin;
        const float boxY = ymax - ymin;
        const float boxZ = zmax - zmin;

        const int nx = std::max(1, static_cast<int>(boxX / globalCutoff));
        const int ny = std::max(1, static_cast<int>(boxY / globalCutoff));
        const int nz = std::max(1, static_cast<int>(boxZ / globalCutoff));

        const float csX = boxX / static_cast<float>(nx);
        const float csY = boxY / static_cast<float>(ny);
        const float csZ = boxZ / static_cast<float>(nz);

        const int nCells = nx * ny * nz;
        std::vector<std::vector<uint32_t>> cellImages(nCells);

        auto cellOf = [&](float x, float y, float z) -> int {
            int ix = std::clamp(static_cast<int>((x - xmin) / csX), 0, nx - 1);
            int iy = std::clamp(static_cast<int>((y - ymin) / csY), 0, ny - 1);
            int iz = std::clamp(static_cast<int>((z - zmin) / csZ), 0, nz - 1);
            return ix + iy * nx + iz * nx * ny;
        };

        for (size_t imageIdx = 0; imageIdx < imageAtoms.size(); ++imageIdx) {
            if (m_cancelled && m_cancelled->load(std::memory_order_relaxed)) return;
            const auto& image = imageAtoms[imageIdx];
            cellImages[cellOf(image.x, image.y, image.z)].push_back(
                static_cast<uint32_t>(imageIdx));
        }

        for (uint32_t i : activeAtoms) {
            if (m_cancelled && m_cancelled->load(std::memory_order_relaxed)) return;
            const float xi = principalX[i];
            const float yi = principalY[i];
            const float zi = principalZ[i];
            const int iCell = cellOf(xi, yi, zi);
            const int ix = iCell % nx;
            const int iy = (iCell / nx) % ny;
            const int iz = iCell / (nx * ny);

            for (int dz = -1; dz <= 1; ++dz) {
                const int jz = iz + dz;
                if (jz < 0 || jz >= nz) continue;

                for (int dy = -1; dy <= 1; ++dy) {
                    const int jy = iy + dy;
                    if (jy < 0 || jy >= ny) continue;

                    for (int dx = -1; dx <= 1; ++dx) {
                        const int jx = ix + dx;
                        if (jx < 0 || jx >= nx) continue;

                        const int jCell = jx + jy * nx + jz * nx * ny;
                        for (uint32_t imageIdx : cellImages[jCell]) {
                            const ImageAtom& image = imageAtoms[imageIdx];
                            if (image.index == i) continue;

                            float fdx = image.x - xi;
                            float fdy = image.y - yi;
                            float fdz = image.z - zi;

                            int8_t imgX = static_cast<int8_t>(
                                static_cast<int>(image.imageX) + wrapX[i] - wrapX[image.index]);
                            int8_t imgY = static_cast<int8_t>(
                                static_cast<int>(image.imageY) + wrapY[i] - wrapY[image.index]);
                            int8_t imgZ = static_cast<int8_t>(
                                static_cast<int>(image.imageZ) + wrapZ[i] - wrapZ[image.index]);

                            applyMIC(fdx, fdy, fdz, imgX, imgY, imgZ, lattice, pbc);

                            const float d2 = fdx * fdx + fdy * fdy + fdz * fdz;
                            if (d2 < cutoff2 && d2 > 0.0f) {
                                perAtom[i].push_back({image.index, imgX, imgY, imgZ});
                            }
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
        if (m_cancelled && m_cancelled->load(std::memory_order_relaxed)) return;
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
        if (m_cancelled && m_cancelled->load(std::memory_order_relaxed)) return;
        m_offsets[i + 1] = m_offsets[i] + static_cast<uint32_t>(perAtom[i].size());
    }

    m_neighbors.resize(m_offsets[atomCount]);
    for (size_t i = 0; i < atomCount; ++i) {
        if (m_cancelled && m_cancelled->load(std::memory_order_relaxed)) return;
        std::copy(perAtom[i].begin(), perAtom[i].end(),
                  m_neighbors.begin() + m_offsets[i]);
    }
}

// ============================================================================
// buildBondList()
// ============================================================================

std::shared_ptr<BondList> NeighborList::buildBondList(
    const Structure& structure, float scale, const std::atomic_bool* cancelled) const
{
    auto bonds = std::make_shared<BondList>();
    if (m_atomCount == 0 || m_neighbors.empty()) return bonds;

    const float* posX = structure.positionsX();
    const float* posY = structure.positionsY();
    const float* posZ = structure.positionsZ();
    const int*   atomicNums = structure.atomicNumbers();
    const Lattice& lattice  = structure.lattice();

    for (size_t i = 0; i < m_atomCount; ++i) {
        if (cancelled && cancelled->load(std::memory_order_relaxed)) return nullptr;
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
