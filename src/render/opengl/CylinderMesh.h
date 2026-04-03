#pragma once

#include <vector>

namespace atom::render {

struct CylinderMeshVertex {
    float positionX;
    float positionY;
    float positionZ;
    float normalX;
    float normalY;
    float normalZ;
};

void buildCappedUnitCylinderMesh(int segments,
                                 std::vector<CylinderMeshVertex>& vertices,
                                 std::vector<unsigned int>& indices);

} // namespace atom::render
