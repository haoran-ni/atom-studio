#include "CylinderMesh.h"

#include <cmath>

namespace atom::render {

void buildCappedUnitCylinderMesh(int segments,
                                 std::vector<CylinderMeshVertex>& vertices,
                                 std::vector<unsigned int>& indices) {
    vertices.clear();
    indices.clear();

    if (segments < 3) {
        return;
    }

    vertices.reserve(static_cast<size_t>((segments + 1) * 4 + 2));
    indices.reserve(static_cast<size_t>(segments) * 12);

    const float pi = 3.14159265358979323846f;
    for (int i = 0; i <= segments; ++i) {
        const float angle = (2.0f * pi * static_cast<float>(i)) / static_cast<float>(segments);
        const float x = std::cos(angle);
        const float y = std::sin(angle);

        vertices.push_back({x, y, 0.0f, x, y, 0.0f});
        vertices.push_back({x, y, 1.0f, x, y, 0.0f});
    }

    const unsigned int bottomCapStart = static_cast<unsigned int>(vertices.size());
    for (int i = 0; i <= segments; ++i) {
        const float angle = (2.0f * pi * static_cast<float>(i)) / static_cast<float>(segments);
        const float x = std::cos(angle);
        const float y = std::sin(angle);
        vertices.push_back({x, y, 0.0f, 0.0f, 0.0f, -1.0f});
    }

    const unsigned int topCapStart = static_cast<unsigned int>(vertices.size());
    for (int i = 0; i <= segments; ++i) {
        const float angle = (2.0f * pi * static_cast<float>(i)) / static_cast<float>(segments);
        const float x = std::cos(angle);
        const float y = std::sin(angle);
        vertices.push_back({x, y, 1.0f, 0.0f, 0.0f, 1.0f});
    }

    const unsigned int bottomCenter = static_cast<unsigned int>(vertices.size());
    vertices.push_back({0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f});
    const unsigned int topCenter = static_cast<unsigned int>(vertices.size());
    vertices.push_back({0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f});

    for (int i = 0; i < segments; ++i) {
        const unsigned int b0 = static_cast<unsigned int>(i * 2);
        const unsigned int t0 = static_cast<unsigned int>(i * 2 + 1);
        const unsigned int b1 = static_cast<unsigned int>((i + 1) * 2);
        const unsigned int t1 = static_cast<unsigned int>((i + 1) * 2 + 1);

        indices.push_back(b0);
        indices.push_back(b1);
        indices.push_back(t0);
        indices.push_back(t0);
        indices.push_back(b1);
        indices.push_back(t1);

        const unsigned int cb0 = bottomCapStart + static_cast<unsigned int>(i);
        const unsigned int cb1 = bottomCapStart + static_cast<unsigned int>(i + 1);
        indices.push_back(bottomCenter);
        indices.push_back(cb1);
        indices.push_back(cb0);

        const unsigned int ct0 = topCapStart + static_cast<unsigned int>(i);
        const unsigned int ct1 = topCapStart + static_cast<unsigned int>(i + 1);
        indices.push_back(topCenter);
        indices.push_back(ct0);
        indices.push_back(ct1);
    }
}

} // namespace atom::render
