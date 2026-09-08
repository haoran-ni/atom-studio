#pragma once

#include "BVH.h"
#include "../../data/BondList.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace atom::data { class Structure; }
namespace atom::render {

// Capture on the model's owning thread, or during render synchronization while
// that thread is blocked. Workers only read the snapshot; appearance is read
// when the completed geometry is uploaded during render synchronization.
struct GeometrySnapshot {
    std::vector<float> x, y, z, radii;
    std::vector<data::Bond> bonds;
    std::vector<float> bondRadii;
    std::array<std::array<double, 3>, 3> lattice{};
};
GeometrySnapshot captureGeometry(const data::Structure* structure);

struct PreparedGeometry {
    std::vector<std::array<float, 4>> atoms;
    std::vector<std::array<float, 4>> bondStarts, bondEnds;
    std::vector<float> bondRadii;
    BVHData bvh;
    std::array<float, 3> centerMin{}, centerMax{}, sceneMin{}, sceneMax{};
    float maxAtomRadius = 0;
};
std::shared_ptr<const PreparedGeometry> prepareGeometry(const GeometrySnapshot& snapshot);

// One worker and one pending snapshot: rapid edits replace queued work. Results
// from an obsolete revision are discarded. Destruction never waits for a BVH build;
// the worker owns its state and never accesses a renderer, QObject, or live model.
class GeometryPreparation {
public:
    struct Result {
        std::shared_ptr<const PreparedGeometry> geometry;
        std::string error;
    };
    GeometryPreparation();
    ~GeometryPreparation();
    GeometryPreparation(const GeometryPreparation&) = delete;
    GeometryPreparation& operator=(const GeometryPreparation&) = delete;
    void request(GeometrySnapshot snapshot);
    bool takeLatest(Result& result);
private:
    struct State;
    std::shared_ptr<State> m_state;
};
} // namespace atom::render
