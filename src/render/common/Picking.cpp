#include "Picking.h"

#include "BondRenderData.h"
#include "Camera.h"
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
    const float c = QVector3D::dotProduct(oc, oc) - radius * radius;
    const float disc = b * b - c;
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
                               int viewportHeight) {
    PickResult best;
    float bestT = std::numeric_limits<float>::max();

    if (!structure || structure->atomCount() == 0) return best;

    QVector3D rayOrigin;
    QVector3D rayDirection;
    if (!buildPickRay(camera, screenX, screenY, viewportWidth, viewportHeight,
                      rayOrigin, rayDirection)) {
        return best;
    }

    if (settings.showAtoms) {
        const float* px = structure->positionsX();
        const float* py = structure->positionsY();
        const float* pz = structure->positionsZ();
        const float* radii = structure->radii();
        for (size_t i = 0; i < structure->atomCount(); ++i) {
            const QVector3D center(px[i], py[i], pz[i]);
            const float radius = radii[i] * settings.atomScale;
            const float t = intersectSphere(rayOrigin, rayDirection, center, radius);
            if (t > 0.0f && t < bestT) {
                bestT = t;
                best.type = PickObjectType::Atom;
                best.index = i;
                best.rayDistance = t;
            }
        }
    }

    if (settings.showBonds && structure->bonds().bondCount() > 0) {
        const std::vector<BondRenderSegment> bonds = collectBondRenderSegments(structure);
        for (size_t i = 0; i < bonds.size(); ++i) {
            const BondRenderSegment& bond = bonds[i];
            const QVector3D start(bond.startX, bond.startY, bond.startZ);
            const QVector3D end(bond.endX, bond.endY, bond.endZ);
            float rayT = 0.0f;
            float distanceSquared = 0.0f;
            if (!raySegmentDistance(rayOrigin, rayDirection, start, end, rayT, distanceSquared)) {
                continue;
            }

            const float radius = std::max(bond.bondRadius, 0.03f);
            if (distanceSquared <= radius * radius && rayT < bestT) {
                bestT = rayT;
                best.type = PickObjectType::Bond;
                best.index = i;
                best.rayDistance = rayT;
            }
        }
    }

    return best;
}

} // namespace atom::render
