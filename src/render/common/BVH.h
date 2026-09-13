#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace atom::render {

/**
 * @brief Packed BVH node layout shared by OpenGL/Metal RT shaders.
 */
struct BVHNodeGPU {
    // xyz = node AABB min; w = max base (unscaled) atom radius in this node.
    std::array<float, 4> minAndMaxRadius = {0.0f, 0.0f, 0.0f, 0.0f};

    // xyz = node AABB max; w unused (padding).
    std::array<float, 4> maxAndPad = {0.0f, 0.0f, 0.0f, 0.0f};

    // x = left child index
    // y = right child index
    // z = first primitive index (leaf only)
    // w = primitive count (leaf only; 0 for interior)
    std::array<uint32_t, 4> meta = {0, 0, 0, 0};
};

struct BVHBuildOptions {
    uint32_t leafSize = 8;
    // Optional cooperative cancellation; a cancelled build returns empty data.
    const std::atomic_bool* cancelled = nullptr;

    // Upper bound on parallel subtree-build tasks.
    // 0 = auto (derived from hardware concurrency), 1 = force serial build.
    // The produced tree is identical in shape and primitive partitioning
    // regardless of this value; only node array ordering differs.
    uint32_t maxParallelTasks = 0;
};

struct BVHData {
    std::vector<BVHNodeGPU> nodes;
    std::vector<uint32_t> primitiveIndices;
};

/**
 * @brief Precomputed AABB + centroid for a single primitive (sphere, cylinder, etc.).
 *
 * maxRadius is stored per-node in the BVH for runtime AABB expansion
 * (e.g. atom scale > 1). Set to 0 for primitives that don't need expansion.
 */
struct PrimitiveBounds {
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
    float centroidX, centroidY, centroidZ;
    float maxRadius;
};

/**
 * @brief Build a BVH from precomputed primitive bounds.
 *
 * Generic entry point — works for any primitive type as long as the caller
 * provides AABBs, centroids, and maxRadius per primitive.
 */
BVHData buildBVH(const PrimitiveBounds* primitives, size_t count,
                 const BVHBuildOptions& options = {});

/**
 * @brief Build a sphere BVH from SoA atom data.
 *
 * Convenience wrapper around buildBVH. Radii are assumed unscaled (base
 * radii). Runtime atom scale should be handled during traversal by
 * conservatively expanding node AABBs.
 */
BVHData buildSphereBVH(const float* posX,
                       const float* posY,
                       const float* posZ,
                       const float* radii,
                       size_t atomCount,
                       const BVHBuildOptions& options = {});

} // namespace atom::render
