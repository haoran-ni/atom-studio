#include "BVH.h"
#include <algorithm>
#include <cfloat>
#include <future>
#include <limits>
#include <numeric>
#include <thread>
#include <utility>

namespace atom::render {

namespace {

constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;

// Parallel subtree builds only pay off once the build itself is measurable;
// below this primitive count the serial path is already sub-millisecond.
constexpr size_t kMinPrimsForParallelBuild = 32768;

size_t nodeCapacity(size_t count, uint32_t leafSize) {
    size_t leaves = 1;
    const size_t minimumLeaves = (count + std::max(1u, leafSize) - 1) / std::max(1u, leafSize);
    while (leaves < minimumLeaves) leaves *= 2;
    return leaves * 2 - 1;
}

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
    // Shared index array; contexts operate on disjoint [begin, end) ranges,
    // so parallel subtree builds may partition it concurrently.
    uint32_t* indices = nullptr;
    BVHData result;
};

NodeBuildStats computeStats(const BuildContext& ctx, size_t begin, size_t end) {
    NodeBuildStats stats;

    for (size_t i = begin; i < end; ++i) {
        const uint32_t idx = ctx.indices[i];
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

uint32_t emplaceNode(BuildContext& ctx, const NodeBuildStats& stats) {
    const uint32_t nodeIndex = static_cast<uint32_t>(ctx.result.nodes.size());
    ctx.result.nodes.emplace_back();
    ctx.result.nodes[nodeIndex].minAndMaxRadius = {stats.minX, stats.minY, stats.minZ, stats.maxRadius};
    ctx.result.nodes[nodeIndex].maxAndPad = {stats.maxX, stats.maxY, stats.maxZ, 0.0f};
    return nodeIndex;
}

void makeLeaf(BuildContext& ctx, uint32_t nodeIndex, size_t begin, size_t end) {
    const size_t primitiveCount = end - begin;
    const uint32_t first = static_cast<uint32_t>(ctx.result.primitiveIndices.size());
    ctx.result.primitiveIndices.reserve(ctx.result.primitiveIndices.size() + primitiveCount);
    for (size_t i = begin; i < end; ++i) {
        ctx.result.primitiveIndices.push_back(ctx.indices[i]);
    }
    ctx.result.nodes[nodeIndex].meta = {kInvalidIndex, kInvalidIndex, first, static_cast<uint32_t>(primitiveCount)};
}

// Picks the widest centroid axis; returns {axis, extent}.
std::pair<int, float> pickSplitAxis(const NodeBuildStats& stats) {
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
    return {splitAxis, maxExtent};
}

// Median-partitions [begin, end) on the given axis; returns the split point.
size_t partitionRange(BuildContext& ctx, size_t begin, size_t end, int splitAxis) {
    const size_t mid = begin + (end - begin) / 2;
    std::nth_element(ctx.indices + begin, ctx.indices + mid, ctx.indices + end,
                     [&](uint32_t a, uint32_t b) {
                         const float ca = centroidValue(ctx, a, splitAxis);
                         const float cb = centroidValue(ctx, b, splitAxis);
                         if (ca == cb) return a < b;
                         return ca < cb;
                     });
    return mid;
}

uint32_t buildNode(BuildContext& ctx, size_t begin, size_t end) {
    const NodeBuildStats stats = computeStats(ctx, begin, end);
    const size_t primitiveCount = end - begin;

    // Use index-based access (not a reference) because recursive buildNode()
    // calls below may push new nodes, invalidating any reference into the vector.
    const uint32_t nodeIndex = emplaceNode(ctx, stats);

    const auto [splitAxis, maxExtent] = pickSplitAxis(stats);
    const uint32_t leafSize = std::max(1u, ctx.options.leafSize);

    if (primitiveCount <= leafSize || maxExtent < 1e-7f) {
        makeLeaf(ctx, nodeIndex, begin, end);
        return nodeIndex;
    }

    const size_t mid = partitionRange(ctx, begin, end, splitAxis);
    const uint32_t leftChild = buildNode(ctx, begin, mid);
    const uint32_t rightChild = buildNode(ctx, mid, end);
    ctx.result.nodes[nodeIndex].meta = {leftChild, rightChild, 0u, 0u};

    return nodeIndex;
}

// ---------------------------------------------------------------------------
// Parallel build: serial skeleton split → parallel subtree builds → ordered
// merge with node/primitive index fix-up. Deterministic by construction: the
// skeleton performs the same splits as the serial path and subtrees are
// appended in fixed task order, independent of completion order.
// ---------------------------------------------------------------------------

struct SubtreeTask {
    uint32_t parentNode = 0;
    int childSlot = 0;  // 0 = left (meta[0]), 1 = right (meta[1])
    size_t begin = 0;
    size_t end = 0;
};

// Splits ranges down to maxDepth, emitting skeleton nodes into ctx.result and
// recording subtree-build tasks for the leftover ranges.
uint32_t buildSkeleton(BuildContext& ctx, std::vector<SubtreeTask>& tasks,
                       size_t begin, size_t end, int depth, int maxDepth) {
    const NodeBuildStats stats = computeStats(ctx, begin, end);
    const size_t primitiveCount = end - begin;
    const uint32_t nodeIndex = emplaceNode(ctx, stats);

    const auto [splitAxis, maxExtent] = pickSplitAxis(stats);
    const uint32_t leafSize = std::max(1u, ctx.options.leafSize);

    if (primitiveCount <= leafSize || maxExtent < 1e-7f) {
        makeLeaf(ctx, nodeIndex, begin, end);
        return nodeIndex;
    }

    const size_t mid = partitionRange(ctx, begin, end, splitAxis);

    if (depth + 1 >= maxDepth) {
        // Children become parallel subtree builds; indices patched at merge.
        ctx.result.nodes[nodeIndex].meta = {kInvalidIndex, kInvalidIndex, 0u, 0u};
        tasks.push_back({nodeIndex, 0, begin, mid});
        tasks.push_back({nodeIndex, 1, mid, end});
        return nodeIndex;
    }

    const uint32_t leftChild = buildSkeleton(ctx, tasks, begin, mid, depth + 1, maxDepth);
    const uint32_t rightChild = buildSkeleton(ctx, tasks, mid, end, depth + 1, maxDepth);
    ctx.result.nodes[nodeIndex].meta = {leftChild, rightChild, 0u, 0u};
    return nodeIndex;
}

int parallelSplitDepth(size_t count, const BVHBuildOptions& options) {
    if (count < kMinPrimsForParallelBuild) return 0;

    uint32_t taskTarget = options.maxParallelTasks;
    if (taskTarget == 0) {
        const unsigned hw = std::thread::hardware_concurrency();
        taskTarget = (hw > 0) ? std::min(hw, 16u) : 4u;
    }
    if (taskTarget <= 1) return 0;

    int depth = 0;
    while ((1u << depth) < taskTarget && depth < 4) {
        ++depth;
    }
    return depth;  // 2^depth subtree tasks (≤ 16)
}

void buildParallel(BuildContext& ctx, size_t count, int maxDepth) {
    std::vector<SubtreeTask> tasks;
    tasks.reserve(static_cast<size_t>(1) << maxDepth);
    buildSkeleton(ctx, tasks, 0, count, 0, maxDepth);

    std::vector<BVHData> subtreeResults(tasks.size());
    std::vector<std::future<void>> futures;
    futures.reserve(tasks.size());
    for (size_t t = 0; t < tasks.size(); ++t) {
        futures.push_back(std::async(std::launch::async, [&ctx, &tasks, &subtreeResults, t]() {
            const SubtreeTask& task = tasks[t];
            BuildContext subCtx;
            subCtx.primitives = ctx.primitives;
            subCtx.options = ctx.options;
            subCtx.indices = ctx.indices;  // disjoint range — safe to share
            const size_t taskCount = task.end - task.begin;
            subCtx.result.nodes.reserve(nodeCapacity(taskCount, ctx.options.leafSize));
            subCtx.result.primitiveIndices.reserve(taskCount);
            buildNode(subCtx, task.begin, task.end);
            subtreeResults[t] = std::move(subCtx.result);
        }));
    }
    for (auto& future : futures) {
        future.get();
    }

    // Merge in task order: append each subtree with index fix-up and patch
    // the skeleton parent's child slot to the subtree root.
    for (size_t t = 0; t < tasks.size(); ++t) {
        const uint32_t nodeOffset = static_cast<uint32_t>(ctx.result.nodes.size());
        const uint32_t primOffset = static_cast<uint32_t>(ctx.result.primitiveIndices.size());
        const BVHData& subtree = subtreeResults[t];

        for (BVHNodeGPU node : subtree.nodes) {
            if (node.meta[3] > 0) {
                node.meta[2] += primOffset;  // leaf: shift first-primitive index
            } else {
                node.meta[0] += nodeOffset;  // interior: shift child indices
                node.meta[1] += nodeOffset;
            }
            ctx.result.nodes.push_back(node);
        }
        ctx.result.primitiveIndices.insert(ctx.result.primitiveIndices.end(),
                                           subtree.primitiveIndices.begin(),
                                           subtree.primitiveIndices.end());

        ctx.result.nodes[tasks[t].parentNode].meta[static_cast<size_t>(tasks[t].childSlot)] = nodeOffset;
    }
}

} // namespace

BVHData buildBVH(const PrimitiveBounds* primitives, size_t count,
                 const BVHBuildOptions& options) {
    BVHData data;
    if (!primitives || count == 0) return data;
    if (count > std::numeric_limits<uint32_t>::max()) return data;

    std::vector<uint32_t> indices(count);
    std::iota(indices.begin(), indices.end(), 0u);

    BuildContext ctx;
    ctx.primitives = primitives;
    ctx.options = options;
    ctx.indices = indices.data();
    ctx.result.nodes.reserve(nodeCapacity(count, options.leafSize));
    ctx.result.primitiveIndices.reserve(count);

    const int maxDepth = parallelSplitDepth(count, options);
    if (maxDepth > 0) {
        buildParallel(ctx, count, maxDepth);
    } else {
        buildNode(ctx, 0, count);
    }
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
