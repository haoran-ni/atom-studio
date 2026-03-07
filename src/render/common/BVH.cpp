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
    const PrimitiveBounds* primitives = nullptr;
    BVHBuildOptions options;
    std::vector<uint32_t> workingIndices;
    BVHData result;
};

NodeBuildStats computeStats(const BuildContext& ctx, size_t begin, size_t end) {
    NodeBuildStats stats;

    for (size_t i = begin; i < end; ++i) {
        const uint32_t idx = ctx.workingIndices[i];
        const auto& p = ctx.primitives[idx];

        stats.minX = std::min(stats.minX, p.minX);
        stats.minY = std::min(stats.minY, p.minY);
        stats.minZ = std::min(stats.minZ, p.minZ);
        stats.maxX = std::max(stats.maxX, p.maxX);
        stats.maxY = std::max(stats.maxY, p.maxY);
        stats.maxZ = std::max(stats.maxZ, p.maxZ);
        stats.maxRadius = std::max(stats.maxRadius, p.maxRadius);

        stats.centroidMinX = std::min(stats.centroidMinX, p.centroidX);
        stats.centroidMinY = std::min(stats.centroidMinY, p.centroidY);
        stats.centroidMinZ = std::min(stats.centroidMinZ, p.centroidZ);
        stats.centroidMaxX = std::max(stats.centroidMaxX, p.centroidX);
        stats.centroidMaxY = std::max(stats.centroidMaxY, p.centroidY);
        stats.centroidMaxZ = std::max(stats.centroidMaxZ, p.centroidZ);
    }

    return stats;
}

float centroidValue(const BuildContext& ctx, uint32_t idx, int axis) {
    switch (axis) {
    case 1: return ctx.primitives[idx].centroidY;
    case 2: return ctx.primitives[idx].centroidZ;
    default: return ctx.primitives[idx].centroidX;
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

BVHData buildBVH(const PrimitiveBounds* primitives, size_t count,
                 const BVHBuildOptions& options) {
    BVHData data;
    if (!primitives || count == 0) return data;
    if (count > std::numeric_limits<uint32_t>::max()) return data;

    BuildContext ctx;
    ctx.primitives = primitives;
    ctx.options = options;
    ctx.workingIndices.resize(count);
    std::iota(ctx.workingIndices.begin(), ctx.workingIndices.end(), 0u);
    ctx.result.nodes.reserve(count * 2);
    ctx.result.primitiveIndices.reserve(count);

    buildNode(ctx, 0, count);
    return std::move(ctx.result);
}

BVHData buildSphereBVH(const float* posX,
                       const float* posY,
                       const float* posZ,
                       const float* radii,
                       size_t atomCount,
                       const BVHBuildOptions& options) {
    if (!posX || !posY || !posZ || !radii || atomCount == 0) {
        return {};
    }

    std::vector<PrimitiveBounds> bounds(atomCount);
    for (size_t i = 0; i < atomCount; ++i) {
        float r = radii[i];
        bounds[i] = {
            posX[i] - r, posY[i] - r, posZ[i] - r,
            posX[i] + r, posY[i] + r, posZ[i] + r,
            posX[i], posY[i], posZ[i],
            r
        };
    }
    return buildBVH(bounds.data(), atomCount, options);
}

} // namespace atom::render
