#include "StructureOperations.h"

#include <array>
#include <algorithm>
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

struct ComponentGraph {
    struct AdjEntry { size_t neighbor; size_t bondIndex; };
    std::vector<std::vector<AdjEntry>> adj;
    std::vector<bool> atomEligible;
    std::vector<bool> bondEligible;
};

static ComponentGraph buildUnwrapConnectivityGraph(const Structure& s) {
    const size_t n = s.atomCount();
    const BondList& bonds = s.bonds();

    ComponentGraph graph;
    graph.adj.resize(n);
    graph.atomEligible.resize(n, false);
    graph.bondEligible.resize(bonds.bondCount(), false);

    for (size_t i = 0; i < n; ++i) {
        graph.atomEligible[i] = isUnwrappableElement(s.atomicNumber(i));
    }

    for (size_t b = 0; b < bonds.bondCount(); ++b) {
        const Bond& bond = bonds.bond(b);
        const size_t i = bond.atomIndex1;
        const size_t j = bond.atomIndex2;
        if (i >= n || j >= n) continue;
        if (!graph.atomEligible[i] || !graph.atomEligible[j]) continue;

        graph.bondEligible[b] = true;
        graph.adj[i].push_back({j, b});
        graph.adj[j].push_back({i, b});
    }

    return graph;
}

struct UnwrapAdjEntry {
    size_t neighbor;
    std::array<int, 3> shift;
};

static std::array<int, 3> addOffset(const std::array<int, 3>& a,
                                    const std::array<int, 3>& b) {
    return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}

static std::array<int, 3> subtractOffset(const std::array<int, 3>& a,
                                         const std::array<int, 3>& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

static bool isZeroOffset(const std::array<int, 3>& value) {
    return value[0] == 0 && value[1] == 0 && value[2] == 0;
}

static std::vector<std::vector<size_t>> findTrueComponents(
    const std::vector<std::vector<UnwrapAdjEntry>>& adj,
    const std::vector<bool>& unwrappable)
{
    const size_t n = adj.size();
    std::vector<std::vector<size_t>> components;
    std::vector<bool> visited(n, false);
    std::queue<size_t> queue;

    for (size_t start = 0; start < n; ++start) {
        if (!unwrappable[start] || visited[start]) continue;

        std::vector<size_t> component;
        visited[start] = true;
        queue.push(start);

        while (!queue.empty()) {
            const size_t atom = queue.front();
            queue.pop();
            component.push_back(atom);

            for (const UnwrapAdjEntry& edge : adj[atom]) {
                if (visited[edge.neighbor]) continue;
                visited[edge.neighbor] = true;
                queue.push(edge.neighbor);
            }
        }

        components.push_back(std::move(component));
    }

    return components;
}

static std::vector<std::vector<size_t>> findVisualFragments(
    const std::vector<size_t>& component,
    const std::vector<uint8_t>& inComponent,
    const std::vector<std::vector<UnwrapAdjEntry>>& adj,
    const std::vector<std::array<int, 3>>& offsets,
    std::vector<size_t>& atomToFragment)
{
    std::vector<std::vector<size_t>> fragments;
    std::vector<uint8_t> visited(adj.size(), 0);
    std::queue<size_t> queue;

    for (const size_t start : component) {
        if (visited[start]) continue;

        const size_t fragmentIndex = fragments.size();
        fragments.emplace_back();
        visited[start] = 1;
        atomToFragment[start] = fragmentIndex;
        queue.push(start);

        while (!queue.empty()) {
            const size_t atom = queue.front();
            queue.pop();
            fragments.back().push_back(atom);

            for (const UnwrapAdjEntry& edge : adj[atom]) {
                const size_t neighbor = edge.neighbor;
                if (!inComponent[neighbor] || visited[neighbor]) continue;

                const std::array<int, 3> currentShift =
                    subtractOffset(offsets[neighbor], offsets[atom]);
                if (currentShift != edge.shift) continue;

                visited[neighbor] = 1;
                atomToFragment[neighbor] = fragmentIndex;
                queue.push(neighbor);
            }
        }
    }

    return fragments;
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
    // 3. Build the periodic connectivity graph using only unwrappable-element
    //    bonds. The shift on each directed edge says where the neighbor must
    //    be placed, relative to the current atom, after all atoms have first
    //    been wrapped into the principal cell.
    // ------------------------------------------------------------------
    std::vector<std::vector<UnwrapAdjEntry>> adj(n);

    for (size_t b = 0; b < bonds.bondCount(); ++b) {
        const Bond& bond = bonds.bond(b);
        const size_t i = bond.atomIndex1;
        const size_t j = bond.atomIndex2;
        if (i >= n || j >= n) continue;
        if (!unwrappable[i] || !unwrappable[j]) continue;

        const std::array<int, 3> shift = {
            static_cast<int>(bond.imageX) + atomWrap[j][0] - atomWrap[i][0],
            static_cast<int>(bond.imageY) + atomWrap[j][1] - atomWrap[i][1],
            static_cast<int>(bond.imageZ) + atomWrap[j][2] - atomWrap[i][2]
        };

        adj[i].push_back({j, shift});
        adj[j].push_back({i, {-shift[0], -shift[1], -shift[2]}});
    }

    // ------------------------------------------------------------------
    // 4. Compare the periodic graph to the visual graph. The periodic graph
    //    ignores image shifts and defines the true component. The visual graph
    //    uses only bonds that are currently satisfied without adding another
    //    periodic image. Fragments are iteratively shifted onto the largest
    //    initial in-cell fragment, matching the Python unwrap strategy.
    // ------------------------------------------------------------------
    std::vector<std::array<int, 3>> offset(n, {0, 0, 0});
    const std::vector<std::vector<size_t>> trueComponents =
        findTrueComponents(adj, unwrappable);

    for (const std::vector<size_t>& component : trueComponents) {
        if (component.empty()) continue;

        std::vector<uint8_t> inComponent(n, 0);
        for (const size_t atom : component) {
            inComponent[atom] = 1;
        }

        std::vector<size_t> atomToFragment(n, static_cast<size_t>(-1));
        std::vector<std::vector<size_t>> fragments =
            findVisualFragments(component, inComponent, adj, offset, atomToFragment);

        if (fragments.size() <= 1) continue;

        size_t anchorFragment = 0;
        for (size_t f = 1; f < fragments.size(); ++f) {
            if (fragments[f].size() > fragments[anchorFragment].size()) {
                anchorFragment = f;
            }
        }

        std::vector<uint8_t> anchorAtom(n, 0);
        for (const size_t atom : fragments[anchorFragment]) {
            anchorAtom[atom] = 1;
        }

        size_t previousFragmentCount = fragments.size() + 1;
        while (fragments.size() > 1 && fragments.size() < previousFragmentCount) {
            previousFragmentCount = fragments.size();

            size_t mergedFragment = static_cast<size_t>(-1);
            for (size_t f = 0; f < fragments.size() && mergedFragment == static_cast<size_t>(-1); ++f) {
                for (const size_t atom : fragments[f]) {
                    if (anchorAtom[atom]) {
                        mergedFragment = f;
                        break;
                    }
                }
            }
            if (mergedFragment == static_cast<size_t>(-1)) break;

            std::vector<uint8_t> inMerged(n, 0);
            for (const size_t atom : fragments[mergedFragment]) {
                inMerged[atom] = 1;
            }

            bool movedFragment = false;
            for (const size_t atom : fragments[mergedFragment]) {
                for (const UnwrapAdjEntry& edge : adj[atom]) {
                    const size_t neighbor = edge.neighbor;
                    if (!inComponent[neighbor] || inMerged[neighbor]) continue;

                    const size_t fragmentToMove = atomToFragment[neighbor];
                    if (fragmentToMove == static_cast<size_t>(-1) ||
                        fragmentToMove == mergedFragment) {
                        continue;
                    }

                    const std::array<int, 3> desiredNeighborOffset =
                        addOffset(offset[atom], edge.shift);
                    const std::array<int, 3> delta =
                        subtractOffset(desiredNeighborOffset, offset[neighbor]);
                    if (isZeroOffset(delta)) continue;

                    for (const size_t movingAtom : fragments[fragmentToMove]) {
                        offset[movingAtom] = addOffset(offset[movingAtom], delta);
                    }

                    movedFragment = true;
                    break;
                }
                if (movedFragment) break;
            }

            if (!movedFragment) break;

            std::fill(atomToFragment.begin(), atomToFragment.end(), static_cast<size_t>(-1));
            fragments = findVisualFragments(component, inComponent, adj, offset, atomToFragment);
        }
    }

    // ------------------------------------------------------------------
    // 5. Apply final wrapped fractional coordinates plus fragment shifts.
    // ------------------------------------------------------------------
    for (size_t idx = 0; idx < n; ++idx) {
        if (!unwrappable[idx]) continue;

        const double fx = wrappedFrac[idx][0] + offset[idx][0];
        const double fy = wrappedFrac[idx][1] + offset[idx][1];
        const double fz = wrappedFrac[idx][2] + offset[idx][2];
        const auto cart = lat.fractionalToCartesian(fx, fy, fz);
        s.setPosition(idx,
                      static_cast<float>(cart[0]),
                      static_cast<float>(cart[1]),
                      static_cast<float>(cart[2]));
    }
}

ConnectedSelection connectedSelectionFromAtom(const Structure& s, size_t atomIndex) {
    ConnectedSelection selection;
    const size_t n = s.atomCount();
    if (atomIndex >= n) return selection;

    ComponentGraph graph = buildUnwrapConnectivityGraph(s);
    if (!graph.atomEligible[atomIndex]) {
        selection.atoms.push_back(atomIndex);
        return selection;
    }

    std::vector<uint8_t> visitedAtoms(n, 0);
    std::vector<uint8_t> visitedBonds(s.bonds().bondCount(), 0);
    std::queue<size_t> queue;
    queue.push(atomIndex);
    visitedAtoms[atomIndex] = 1;

    while (!queue.empty()) {
        const size_t atom = queue.front();
        queue.pop();
        selection.atoms.push_back(atom);

        for (const auto& edge : graph.adj[atom]) {
            if (!visitedBonds[edge.bondIndex]) {
                visitedBonds[edge.bondIndex] = 1;
                selection.bonds.push_back(edge.bondIndex);
            }
            if (!visitedAtoms[edge.neighbor]) {
                visitedAtoms[edge.neighbor] = 1;
                queue.push(edge.neighbor);
            }
        }
    }

    return selection;
}

ConnectedSelection connectedSelectionFromBond(const Structure& s, size_t bondIndex) {
    ConnectedSelection selection;
    const BondList& bonds = s.bonds();
    if (bondIndex >= bonds.bondCount()) return selection;

    ComponentGraph graph = buildUnwrapConnectivityGraph(s);
    const Bond& bond = bonds.bond(bondIndex);

    if (bondIndex < graph.bondEligible.size() && graph.bondEligible[bondIndex]) {
        return connectedSelectionFromAtom(s, bond.atomIndex1);
    }

    selection.bonds.push_back(bondIndex);
    selection.atoms.push_back(bond.atomIndex1);
    if (bond.atomIndex2 != bond.atomIndex1) {
        selection.atoms.push_back(bond.atomIndex2);
    }
    return selection;
}

} // namespace atom::data
