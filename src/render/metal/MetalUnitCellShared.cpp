#include "MetalUnitCellShared.h"
#include "../common/Camera.h"
#include "../common/RenderSettings.h"
#include "../../data/Structure.h"
#include <QMatrix4x4>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace atom::render::metal {

namespace {

static simd_float4x4 qMatToSimd(const QMatrix4x4& m) {
    simd_float4x4 result;
    std::memcpy(&result, m.constData(), 16 * sizeof(float));
    return result;
}

// Remap projection matrix from OpenGL depth [-1,1] to Metal depth [0,1].
static simd_float4x4 remapDepthToMetal(const QMatrix4x4& proj) {
    QMatrix4x4 bias;
    bias(2, 2) = 0.5f;
    bias(2, 3) = 0.5f;
    return qMatToSimd(bias * proj);
}

} // namespace

void buildUnitCylinderMesh(int segments,
                           std::vector<float>& vertices,
                           std::vector<uint32_t>& indices) {
    vertices.clear();
    indices.clear();

    const float pi = 3.14159265358979323846f;
    for (int i = 0; i <= segments; ++i) {
        const float angle = (2.0f * pi * i) / segments;
        const float x = std::cos(angle);
        const float y = std::sin(angle);

        // Unit cylinder aligned to +Z from z=0 to z=1.
        vertices.push_back(x);
        vertices.push_back(y);
        vertices.push_back(0.0f);

        vertices.push_back(x);
        vertices.push_back(y);
        vertices.push_back(1.0f);
    }

    for (int i = 0; i < segments; ++i) {
        const int b0 = i * 2;
        const int t0 = i * 2 + 1;
        const int b1 = (i + 1) * 2;
        const int t1 = (i + 1) * 2 + 1;

        indices.push_back(b0);
        indices.push_back(b1);
        indices.push_back(t0);

        indices.push_back(t0);
        indices.push_back(b1);
        indices.push_back(t1);
    }
}

void buildUnitSphereMesh(int latSegments,
                         int lonSegments,
                         std::vector<float>& vertices,
                         std::vector<uint32_t>& indices) {
    vertices.clear();
    indices.clear();

    const float pi = 3.14159265358979323846f;

    for (int lat = 0; lat <= latSegments; ++lat) {
        const float v = static_cast<float>(lat) / latSegments;
        const float theta = v * pi;
        const float sinTheta = std::sin(theta);
        const float cosTheta = std::cos(theta);

        for (int lon = 0; lon <= lonSegments; ++lon) {
            const float u = static_cast<float>(lon) / lonSegments;
            const float phi = u * (2.0f * pi);
            const float sinPhi = std::sin(phi);
            const float cosPhi = std::cos(phi);

            vertices.push_back(sinTheta * cosPhi);
            vertices.push_back(cosTheta);
            vertices.push_back(sinTheta * sinPhi);
        }
    }

    const int stride = lonSegments + 1;
    for (int lat = 0; lat < latSegments; ++lat) {
        for (int lon = 0; lon < lonSegments; ++lon) {
            const uint32_t i0 = static_cast<uint32_t>(lat * stride + lon);
            const uint32_t i1 = static_cast<uint32_t>((lat + 1) * stride + lon);
            const uint32_t i2 = i0 + 1;
            const uint32_t i3 = i1 + 1;

            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i2);

            indices.push_back(i2);
            indices.push_back(i1);
            indices.push_back(i3);
        }
    }
}

bool buildUnitCellInstances(const data::Structure* structure, UnitCellInstanceData& outData) {
    if (!structure || !structure->hasLattice()) {
        return false;
    }

    const auto& lattice = structure->lattice();
    const auto& mat = lattice.matrix;

    const float ax = static_cast<float>(mat[0][0]);
    const float ay = static_cast<float>(mat[0][1]);
    const float az = static_cast<float>(mat[0][2]);
    const float bx = static_cast<float>(mat[1][0]);
    const float by = static_cast<float>(mat[1][1]);
    const float bz = static_cast<float>(mat[1][2]);
    const float cx = static_cast<float>(mat[2][0]);
    const float cy = static_cast<float>(mat[2][1]);
    const float cz = static_cast<float>(mat[2][2]);

    const std::array<simd_float3, kUnitCellJointCount> corners = {{
        simd_make_float3(0.0f, 0.0f, 0.0f),
        simd_make_float3(ax, ay, az),
        simd_make_float3(bx, by, bz),
        simd_make_float3(cx, cy, cz),
        simd_make_float3(ax + bx, ay + by, az + bz),
        simd_make_float3(ax + cx, ay + cy, az + cz),
        simd_make_float3(bx + cx, by + cy, bz + cz),
        simd_make_float3(ax + bx + cx, ay + by + cy, az + bz + cz),
    }};

    const std::array<uint32_t, kUnitCellEdgeCount * 2> edgeIndices = {{
        0, 1,   0, 2,   0, 3,
        1, 4,   1, 5,
        2, 4,   2, 6,
        3, 5,   3, 6,
        4, 7,   5, 7,   6, 7
    }};

    const simd_float4 white = simd_make_float4(1.0f, 1.0f, 1.0f, 1.0f);

    for (int i = 0; i < kUnitCellEdgeCount; ++i) {
        outData.edges[i].start = corners[edgeIndices[i * 2 + 0]];
        outData.edges[i].end = corners[edgeIndices[i * 2 + 1]];
        outData.edges[i].startColor = white;
        outData.edges[i].endColor = white;
    }

    for (int i = 0; i < kUnitCellJointCount; ++i) {
        outData.joints[i].positionAndRadius = simd_make_float4(corners[i], 1.0f);
        outData.joints[i].color = white;
    }

    return true;
}

UnitCellStyle makeUnitCellStyle(const RenderSettings& settings) {
    const QColor color = settings.unitCellColor;
    return UnitCellStyle{
        std::max(settings.unitCellThickness, 0.001f),
        simd_make_float4(color.redF(), color.greenF(), color.blueF(), 1.0f)
    };
}

RTUnitCellUniforms makeRTUnitCellUniforms(const Camera& camera,
                                          const RenderSettings& settings,
                                          int atomCount,
                                          int bvhNodeCount,
                                          float occlusionBias) {
    RTUnitCellUniforms unitCell{};
    unitCell.viewProjectionMatrix = remapDepthToMetal(camera.viewProjectionMatrix());

    const QVector3D camPos = camera.position();
    unitCell.cameraPosition = simd_make_float3(camPos.x(), camPos.y(), camPos.z());
    unitCell.atomScale = settings.atomScale;
    unitCell.atomCount = atomCount;
    unitCell.bvhNodeCount = bvhNodeCount;
    unitCell.occlusionBias = occlusionBias;

    const UnitCellStyle style = makeUnitCellStyle(settings);
    unitCell.unitCellRadius = style.radius;
    unitCell.unitCellColor = style.color;

    unitCell.isPerspective = camera.isPerspective() ? 1 : 0;
    const QVector3D fwd = camera.forwardVector();
    unitCell.cameraForwardX = fwd.x();
    unitCell.cameraForwardY = fwd.y();
    unitCell.cameraForwardZ = fwd.z();

    return unitCell;
}

} // namespace atom::render::metal
