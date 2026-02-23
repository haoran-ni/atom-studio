#include "StructureOperations.h"

namespace atom::data {

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

} // namespace atom::data
