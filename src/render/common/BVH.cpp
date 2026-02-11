#include "BVH.h"
#include <algorithm>
#include <cfloat>
#include <limits>
#include <numeric>
#include <utility>

namespace atom::render {

namespace {

constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;

struct NodeBuildStats {
    float minX = FLT_MAX;
    float minY = FLT_MAX;
    float minZ = FLT_MAX;
    float maxX = -FLT_MAX;
    float maxY = -FLT_MAX;
    float maxZ = -FLT_MAX;
    float maxRadius = 0.0f;

    float centroidMinX = FLT_MAX;
    float centroidMinY = FLT_MAX;
    float centroidMinZ = FLT_MAX;
    float centroidMaxX = -FLT_MAX;
    float centroidMaxY = -FLT_MAX;
    float centroidMaxZ = -FLT_MAX;
};

struct BuildContext {
    const float* posX = nullptr;
    const float* posY = nullptr;
    const float* posZ = nullptr;
    const float* radii = nullptr;
    BVHBuildOptions options;
    std::vector<uint32_t> workingIndices;
    BVHData result;
};

NodeBuildStats computeStats(const BuildContext& ctx, size_t begin, size_t end) {
    NodeBuildStats stats;

    for (size_t i = begin; i < end; ++i) {
        const uint32_t atomIndex = ctx.workingIndices[i];
        const float cx = ctx.posX[atomIndex];
        const float cy = ctx.posY[atomIndex];
        const float cz = ctx.posZ[atomIndex];
        const float r = ctx.radii[atomIndex];

        stats.minX = std::min(stats.minX, cx - r);
        stats.minY = std::min(stats.minY, cy - r);
        stats.minZ = std::min(stats.minZ, cz - r);
        stats.maxX = std::max(stats.maxX, cx + r);
        stats.maxY = std::max(stats.maxY, cy + r);
        stats.maxZ = std::max(stats.maxZ, cz + r);
        stats.maxRadius = std::max(stats.maxRadius, r);

        stats.centroidMinX = std::min(stats.centroidMinX, cx);
        stats.centroidMinY = std::min(stats.centroidMinY, cy);
        stats.centroidMinZ = std::min(stats.centroidMinZ, cz);
        stats.centroidMaxX = std::max(stats.centroidMaxX, cx);
        stats.centroidMaxY = std::max(stats.centroidMaxY, cy);
        stats.centroidMaxZ = std::max(stats.centroidMaxZ, cz);
    }

    return stats;
}

float centroidValue(const BuildContext& ctx, uint32_t atomIndex, int axis) {
    switch (axis) {
    case 1: return ctx.posY[atomIndex];
    case 2: return ctx.posZ[atomIndex];
    default: return ctx.posX[atomIndex];
    }
}

uint32_t buildNode(BuildContext& ctx, size_t begin, size_t end) {
    const NodeBuildStats stats = computeStats(ctx, begin, end);
    const size_t primitiveCount = end - begin;

    const uint32_t nodeIndex = static_cast<uint32_t>(ctx.result.nodes.size());
    ctx.result.nodes.emplace_back();

    // Use index-based access (not a reference) because recursive buildNode()
    // calls below may push new nodes, invalidating any reference into the vector.
    ctx.result.nodes[nodeIndex].minAndMaxRadius = {stats.minX, stats.minY, stats.minZ, stats.maxRadius};
    ctx.result.nodes[nodeIndex].maxAndPad = {stats.maxX, stats.maxY, stats.maxZ, 0.0f};

    const float extentX = stats.centroidMaxX - stats.centroidMinX;
    const float extentY = stats.centroidMaxY - stats.centroidMinY;
    const float extentZ = stats.centroidMaxZ - stats.centroidMinZ;

    int splitAxis = 0;
    float maxExtent = extentX;
    if (extentY > maxExtent) {
        splitAxis = 1;
        maxExtent = extentY;
    }
    if (extentZ > maxExtent) {
        splitAxis = 2;
        maxExtent = extentZ;
    }

    const uint32_t leafSize = std::max(1u, ctx.options.leafSize);
    const bool makeLeaf = primitiveCount <= leafSize || maxExtent < 1e-7f;

    if (makeLeaf) {
        const uint32_t first = static_cast<uint32_t>(ctx.result.primitiveIndices.size());
        ctx.result.primitiveIndices.reserve(ctx.result.primitiveIndices.size() + primitiveCount);
        for (size_t i = begin; i < end; ++i) {
            ctx.result.primitiveIndices.push_back(ctx.workingIndices[i]);
        }
        ctx.result.nodes[nodeIndex].meta = {kInvalidIndex, kInvalidIndex, first, static_cast<uint32_t>(primitiveCount)};
        return nodeIndex;
    }

    const size_t mid = begin + primitiveCount / 2;
    auto firstIt = ctx.workingIndices.begin() + static_cast<std::ptrdiff_t>(begin);
    auto midIt = ctx.workingIndices.begin() + static_cast<std::ptrdiff_t>(mid);
    auto lastIt = ctx.workingIndices.begin() + static_cast<std::ptrdiff_t>(end);

    std::nth_element(firstIt, midIt, lastIt,
                     [&](uint32_t a, uint32_t b) {
                         const float ca = centroidValue(ctx, a, splitAxis);
                         const float cb = centroidValue(ctx, b, splitAxis);
                         if (ca == cb) return a < b;
                         return ca < cb;
                     });

    const uint32_t leftChild = buildNode(ctx, begin, mid);
    const uint32_t rightChild = buildNode(ctx, mid, end);
    ctx.result.nodes[nodeIndex].meta = {leftChild, rightChild, 0u, 0u};

    return nodeIndex;
}

} // namespace

BVHData buildSphereBVH(const float* posX,
                       const float* posY,
                       const float* posZ,
                       const float* radii,
                       size_t atomCount,
                       const BVHBuildOptions& options) {
    BVHData data;
    if (!posX || !posY || !posZ || !radii || atomCount == 0) {
        return data;
    }

    if (atomCount > std::numeric_limits<uint32_t>::max()) {
        return data;
    }

    BuildContext ctx;
    ctx.posX = posX;
    ctx.posY = posY;
    ctx.posZ = posZ;
    ctx.radii = radii;
    ctx.options = options;
    ctx.workingIndices.resize(atomCount);
    std::iota(ctx.workingIndices.begin(), ctx.workingIndices.end(), 0u);
    ctx.result.nodes.reserve(atomCount * 2);
    ctx.result.primitiveIndices.reserve(atomCount);

    buildNode(ctx, 0, atomCount);
    return std::move(ctx.result);
}

} // namespace atom::render
