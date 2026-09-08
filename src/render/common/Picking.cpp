#include "Picking.h"

#include "BondRenderData.h"
#include "Camera.h"
#include "PreparedGeometry.h"
#include "RenderSettings.h"
#include "../../data/Structure.h"

#include <QMatrix4x4>
#include <QVector3D>
#include <QVector4D>
#include <algorithm>
#include <cmath>
#include <limits>

namespace atom::render {

namespace {

bool buildPickRay(const Camera& camera,
                  float screenX,
                  float screenY,
                  int viewportWidth,
                  int viewportHeight,
                  QVector3D& origin,
                  QVector3D& direction) {
    if (viewportWidth <= 0 || viewportHeight <= 0) return false;

    const float ndcX = (2.0f * screenX / static_cast<float>(viewportWidth)) - 1.0f;
    const float ndcY = 1.0f - (2.0f * screenY / static_cast<float>(viewportHeight));

    bool invertible = false;
    const QMatrix4x4 invVP = camera.viewProjectionMatrix().inverted(&invertible);
    if (!invertible) return false;

    QVector4D nearPoint = invVP * QVector4D(ndcX, ndcY, -1.0f, 1.0f);
    QVector4D farPoint = invVP * QVector4D(ndcX, ndcY, 1.0f, 1.0f);
    if (qFuzzyIsNull(nearPoint.w()) || qFuzzyIsNull(farPoint.w())) return false;

    nearPoint /= nearPoint.w();
    farPoint /= farPoint.w();

    origin = camera.isPerspective() ? camera.position() : nearPoint.toVector3D();
    direction = (farPoint.toVector3D() - nearPoint.toVector3D()).normalized();
    return direction.lengthSquared() > 1e-10f;
}

float intersectSphere(const QVector3D& origin,
                      const QVector3D& direction,
                      const QVector3D& center,
                      float radius) {
    const QVector3D oc = origin - center;
    const float b = QVector3D::dotProduct(oc, direction);
    // Avoid cancellation for small atoms viewed from a large distance.
    const QVector3D perpendicular = oc - b * direction;
    const float disc = radius * radius - perpendicular.lengthSquared();
    if (disc < 0.0f) return -1.0f;

    const float root = std::sqrt(disc);
    float t = -b - root;
    if (t > 0.001f) return t;
    t = -b + root;
    return t > 0.001f ? t : -1.0f;
}

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

bool raySegmentDistance(const QVector3D& origin,
                        const QVector3D& direction,
                        const QVector3D& start,
                        const QVector3D& end,
                        float& rayT,
                        float& distanceSquared) {
    const QVector3D segment = end - start;
    const QVector3D w0 = origin - start;
    const float a = QVector3D::dotProduct(direction, direction);
    const float b = QVector3D::dotProduct(direction, segment);
    const float c = QVector3D::dotProduct(segment, segment);
    const float d = QVector3D::dotProduct(direction, w0);
    const float e = QVector3D::dotProduct(segment, w0);

    if (c < 1e-10f || a < 1e-10f) return false;

    const float denom = a * c - b * b;
    float segmentT = 0.0f;

    if (std::abs(denom) > 1e-8f) {
        rayT = (b * e - c * d) / denom;
        segmentT = (a * e - b * d) / denom;
    } else {
        rayT = 0.0f;
        segmentT = e / c;
    }

    if (rayT < 0.0f) {
        rayT = 0.0f;
        segmentT = e / c;
    }

    segmentT = clamp01(segmentT);
    rayT = std::max(0.0f, (b * segmentT - d) / a);

    const QVector3D rayPoint = origin + direction * rayT;
    const QVector3D segmentPoint = start + segment * segmentT;
    distanceSquared = (rayPoint - segmentPoint).lengthSquared();
    return true;
}

} // namespace

PickResult pickStructureObject(const data::Structure* structure,
                               const Camera& camera,
                               const RenderSettings& settings,
                               float screenX,
                               float screenY,
                               int viewportWidth,
                               int viewportHeight,
                               const PreparedGeometry* geometry) {
    PickResult best;
    float bestT = std::numeric_limits<float>::max();

    if (!structure || structure->atomCount() == 0) return best;

    QVector3D rayOrigin;
    QVector3D rayDirection;
    if (!buildPickRay(camera, screenX, screenY, viewportWidth, viewportHeight,
                      rayOrigin, rayDirection)) {
        return best;
    }

    const size_t atomCount = structure->atomCount();
    size_t bestPrimitive = std::numeric_limits<size_t>::max();
    const auto consider = [&](size_t primitive, float t, PickObjectType type, size_t index) {
        // Match the original atom-first, ascending-index tie break regardless
        // of the order in which BVH leaves are visited.
        if (t < bestT || (t == bestT && primitive < bestPrimitive)) {
            bestT = t;
            bestPrimitive = primitive;
            best = {type, index, t};
        }
    };
    const auto testPrimitive = [&](size_t primitive) {
        if (primitive < atomCount) {
            if (!settings.showAtoms) return;
            const size_t i = primitive;
            const QVector3D center(structure->positionsX()[i], structure->positionsY()[i], structure->positionsZ()[i]);
            const float t = intersectSphere(rayOrigin, rayDirection, center, structure->radii()[i] * settings.atomScale);
            if (t > 0) consider(primitive, t, PickObjectType::Atom, i);
        } else {
            if (!settings.showBonds) return;
            const size_t i = primitive - atomCount;
            QVector3D start, end;
            float radius;
            if (geometry) {
                const auto& a = geometry->bondStarts[i];
                const auto& b = geometry->bondEnds[i];
                start = QVector3D(a[0], a[1], a[2]);
                end = QVector3D(b[0], b[1], b[2]);
                radius = geometry->bondRadii[i];
            } else {
                const auto bond = makeBondRenderSegment(*structure, structure->bonds().bond(i), i);
                start = QVector3D(bond.startX, bond.startY, bond.startZ);
                end = QVector3D(bond.endX, bond.endY, bond.endZ);
                radius = bond.bondRadius;
            }
            float rayT, distanceSquared;
            radius = std::max(radius, 0.03f);
            if (raySegmentDistance(rayOrigin, rayDirection, start, end, rayT, distanceSquared) &&
                distanceSquared <= radius * radius)
                consider(primitive, rayT, PickObjectType::Bond, i);
        }
    };
    // The caller must invalidate the cache on geometry/topology changes. Count
    // checks also protect fallback callers accidentally passing an old snapshot.
    if (geometry && (geometry->atoms.size() != atomCount ||
                     geometry->bondStarts.size() != structure->bonds().bondCount())) geometry = nullptr;
    if (!geometry || geometry->bvh.nodes.empty()) {
        for (size_t i = 0; i < atomCount + structure->bonds().bondCount(); ++i) testPrimitive(i);
    } else {
        const auto& bvh = geometry->bvh;
        const auto boxEntry = [&](uint32_t nodeIndex) {
            const auto& node = bvh.nodes[nodeIndex];
            const float padding = std::max(settings.atomScale - 1.0f, 0.0f) * node.minAndMaxRadius[3] + 0.03f;
            double near = 0, far = bestT;
            for (int axis = 0; axis < 3; ++axis) {
                const double lo = node.minAndMaxRadius[axis] - padding;
                const double hi = node.maxAndPad[axis] + padding;
                const double origin = rayOrigin[axis], direction = rayDirection[axis];
                if (direction == 0) {
                    if (origin < lo || origin > hi) return std::numeric_limits<float>::infinity();
                } else {
                    const double a = (lo - origin) / direction, b = (hi - origin) / direction;
                    near = std::max(near, std::min(a, b));
                    far = std::min(far, std::max(a, b));
                    if (near > far) return std::numeric_limits<float>::infinity();
                }
            }
            return static_cast<float>(near);
        };
        // Median splits bound depth to 32 for the builder's uint32 primitive IDs.
        struct Entry { uint32_t node; float near; };
        std::array<Entry, 64> stack;
        size_t pending = 0;
        const float rootNear = boxEntry(0);
        if (rootNear <= bestT) stack[pending++] = {0, rootNear};
        while (pending) {
            const auto entry = stack[--pending];
            if (entry.near > bestT) continue;
            const auto& meta = bvh.nodes[entry.node].meta;
            if (meta[3]) {
                for (uint32_t i = 0; i < meta[3]; ++i) testPrimitive(bvh.primitiveIndices[meta[2] + i]);
            } else {
                Entry a{meta[0], boxEntry(meta[0])}, b{meta[1], boxEntry(meta[1])};
                if (a.near < b.near) std::swap(a, b);
                if (a.near <= bestT) stack[pending++] = a;
                if (b.near <= bestT) stack[pending++] = b;
            }
        }
    }

    return best;
}

} // namespace atom::render
