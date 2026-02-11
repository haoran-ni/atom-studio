#pragma once

#include "MetalTypes.h"
#include <array>
#include <cstdint>
#include <vector>

namespace atom::data {
class Structure;
}

namespace atom::render {
class Camera;
struct RenderSettings;
}

namespace atom::render::metal {

constexpr int kUnitCellEdgeCount = 12;
constexpr int kUnitCellJointCount = 8;

struct UnitCellStyle {
    float radius = 0.001f;
    simd_float4 color{};
};

struct UnitCellInstanceData {
    std::array<BondInstance, kUnitCellEdgeCount> edges{};
    std::array<SphereInstance, kUnitCellJointCount> joints{};
};

void buildUnitCylinderMesh(int segments,
                           std::vector<float>& vertices,
                           std::vector<uint32_t>& indices);

void buildUnitSphereMesh(int latSegments,
                         int lonSegments,
                         std::vector<float>& vertices,
                         std::vector<uint32_t>& indices);

bool buildUnitCellInstances(const data::Structure* structure, UnitCellInstanceData& outData);

UnitCellStyle makeUnitCellStyle(const RenderSettings& settings);

RTUnitCellUniforms makeRTUnitCellUniforms(const Camera& camera,
                                          const RenderSettings& settings,
                                          int atomCount,
                                          int bvhNodeCount,
                                          float occlusionBias = 0.001f);

} // namespace atom::render::metal
