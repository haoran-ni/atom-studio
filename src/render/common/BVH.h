#pragma once

#include <array>
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
};

struct BVHData {
    std::vector<BVHNodeGPU> nodes;
    std::vector<uint32_t> primitiveIndices;
};

/**
 * @brief Build a sphere BVH from SoA atom data.
 *
 * Radii are assumed unscaled (base radii). Runtime atom scale should be
 * handled during traversal by conservatively expanding node AABBs.
 */
BVHData buildSphereBVH(const float* posX,
                       const float* posY,
                       const float* posZ,
                       const float* radii,
                       size_t atomCount,
                       const BVHBuildOptions& options = {});

} // namespace atom::render
