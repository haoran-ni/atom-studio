// Standalone benchmark + equivalence check for the parallel BVH build.
// Compares serial (maxParallelTasks = 1) vs parallel (auto) builds:
//  - wall-clock build time
//  - tree equivalence via canonical DFS (AABBs, leaf primitive sets)
#include "BVH.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

using namespace atom::render;

static std::vector<PrimitiveBounds> makeScene(size_t count, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> pos(-200.0f, 200.0f);
    std::uniform_real_distribution<float> rad(0.3f, 1.8f);

    std::vector<PrimitiveBounds> prims(count);
    for (auto& p : prims) {
        const float x = pos(rng), y = pos(rng), z = pos(rng), r = rad(rng);
        p = {x - r, y - r, z - r, x + r, y + r, z + r, x, y, z, r};
    }
    return prims;
}

// Canonical DFS: emits (AABB, sorted leaf prims) per node in left-to-right
// order. Trees with identical splits compare equal regardless of node
// array layout.
static void canonicalize(const BVHData& bvh, uint32_t node,
                         std::vector<float>& geom, std::vector<uint32_t>& leaves) {
    const auto& n = bvh.nodes[node];
    for (int i = 0; i < 4; ++i) geom.push_back(n.minAndMaxRadius[static_cast<size_t>(i)]);
    for (int i = 0; i < 3; ++i) geom.push_back(n.maxAndPad[static_cast<size_t>(i)]);
    if (n.meta[3] > 0) {
        std::vector<uint32_t> prims(bvh.primitiveIndices.begin() + n.meta[2],
                                    bvh.primitiveIndices.begin() + n.meta[2] + n.meta[3]);
        std::sort(prims.begin(), prims.end());
        leaves.push_back(n.meta[3]);
        leaves.insert(leaves.end(), prims.begin(), prims.end());
    } else {
        leaves.push_back(0);
        canonicalize(bvh, n.meta[0], geom, leaves);
        canonicalize(bvh, n.meta[1], geom, leaves);
    }
}

static double timedBuild(const std::vector<PrimitiveBounds>& prims,
                         const BVHBuildOptions& opts, BVHData& out) {
    const auto t0 = std::chrono::steady_clock::now();
    out = buildBVH(prims.data(), prims.size(), opts);
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main() {
    for (size_t count : {100000ul, 1000000ul, 4000000ul}) {
        const auto prims = makeScene(count, 42);

        BVHBuildOptions serialOpts;
        serialOpts.maxParallelTasks = 1;
        BVHBuildOptions parallelOpts;  // auto

        BVHData serialBVH, parallelBVH;
        // Warm-up + best-of-3 for each variant.
        double serialMs = 1e30, parallelMs = 1e30;
        for (int i = 0; i < 3; ++i) {
            serialMs = std::min(serialMs, timedBuild(prims, serialOpts, serialBVH));
            parallelMs = std::min(parallelMs, timedBuild(prims, parallelOpts, parallelBVH));
        }

        std::vector<float> geomA, geomB;
        std::vector<uint32_t> leavesA, leavesB;
        canonicalize(serialBVH, 0, geomA, leavesA);
        canonicalize(parallelBVH, 0, geomB, leavesB);
        const bool equivalent = (geomA == geomB) && (leavesA == leavesB) &&
                                (serialBVH.nodes.size() == parallelBVH.nodes.size());

        std::printf("%8zu prims: serial %8.2f ms | parallel %8.2f ms | speedup %.2fx | nodes %zu/%zu | equivalent: %s\n",
                    count, serialMs, parallelMs, serialMs / parallelMs,
                    serialBVH.nodes.size(), parallelBVH.nodes.size(),
                    equivalent ? "YES" : "NO");
        if (!equivalent) return 1;
    }
    return 0;
}
