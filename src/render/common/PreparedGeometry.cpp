#include "PreparedGeometry.h"
#include "../../data/Structure.h"
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace atom::render {
GeometrySnapshot captureGeometry(const data::Structure* structure) {
    GeometrySnapshot result;
    if (!structure) return result;
    const size_t count = structure->atomCount();
    if (count) {
        result.x.assign(structure->positionsX(), structure->positionsX() + count);
        result.y.assign(structure->positionsY(), structure->positionsY() + count);
        result.z.assign(structure->positionsZ(), structure->positionsZ() + count);
        result.radii.assign(structure->radii(), structure->radii() + count);
    }
    result.bonds = structure->bonds().bonds();
    result.bondRadii = structure->bonds().radii();
    result.lattice = structure->lattice().matrix;
    return result;
}

std::shared_ptr<const PreparedGeometry> prepareGeometry(const GeometrySnapshot& s) {
    auto result = std::make_shared<PreparedGeometry>();
    const size_t count = s.x.size(), bondCount = s.bonds.size();
    result->atoms.resize(count);
    result->bondStarts.resize(bondCount);
    result->bondEnds.resize(bondCount);
    result->bondRadii = s.bondRadii;
    std::vector<PrimitiveBounds> bounds(count + bondCount);
    if (count) {
        result->centerMin = result->centerMax = {s.x[0], s.y[0], s.z[0]};
    }
    for (size_t i = 0; i < count; ++i) {
        const float x = s.x[i], y = s.y[i], z = s.z[i], r = s.radii[i];
        result->atoms[i] = {x, y, z, r};
        bounds[i] = {x-r, y-r, z-r, x+r, y+r, z+r, x, y, z, r};
        for (int axis = 0; axis < 3; ++axis) {
            result->centerMin[axis] = std::min(result->centerMin[axis], result->atoms[i][axis]);
            result->centerMax[axis] = std::max(result->centerMax[axis], result->atoms[i][axis]);
        }
        result->maxAtomRadius = std::max(result->maxAtomRadius, r);
    }
    for (size_t i = 0; i < bondCount; ++i) {
        const auto& bond = s.bonds[i];
        auto& start = result->bondStarts[i];
        auto& end = result->bondEnds[i];
        start = result->atoms.at(bond.atomIndex1);
        end = result->atoms.at(bond.atomIndex2);
        if (bond.imageX || bond.imageY || bond.imageZ) {
            for (int axis = 0; axis < 3; ++axis) {
                end[axis] += static_cast<float>(bond.imageX * s.lattice[0][axis] +
                    bond.imageY * s.lattice[1][axis] + bond.imageZ * s.lattice[2][axis]);
            }
        }
        const float r = s.bondRadii[i];
        bounds[count+i] = {
            std::min(start[0],end[0])-r, std::min(start[1],end[1])-r, std::min(start[2],end[2])-r,
            std::max(start[0],end[0])+r, std::max(start[1],end[1])+r, std::max(start[2],end[2])+r,
            (start[0]+end[0])*.5f, (start[1]+end[1])*.5f, (start[2]+end[2])*.5f, 0
        };
    }
    result->bvh = buildBVH(bounds.data(), bounds.size());
    if (!result->bvh.nodes.empty()) {
        const auto& root = result->bvh.nodes.front();
        std::copy_n(root.minAndMaxRadius.begin(), 3, result->sceneMin.begin());
        std::copy_n(root.maxAndPad.begin(), 3, result->sceneMax.begin());
    }
    return result;
}

struct GeometryPreparation::State {
    std::mutex mutex;
    std::condition_variable wake;
    bool stopped = false;
    bool started = false;
    uint64_t revision = 0;
    std::optional<GeometrySnapshot> pending;
    std::optional<Result> ready;
};
GeometryPreparation::GeometryPreparation() : m_state(std::make_shared<State>()) {}
GeometryPreparation::~GeometryPreparation() {
    std::lock_guard lock(m_state->mutex);
    m_state->stopped = true;
    m_state->pending.reset();
    m_state->wake.notify_one();
}
void GeometryPreparation::request(GeometrySnapshot snapshot) {
    auto state = m_state;
    std::lock_guard lock(state->mutex);
    if (!state->started) {
        std::thread([state] {
            for (;;) {
                std::unique_lock lock(state->mutex);
                state->wake.wait(lock, [&] { return state->stopped || state->pending.has_value(); });
                if (state->stopped) return;
                auto snapshot = std::move(*state->pending);
                state->pending.reset();
                const auto revision = state->revision;
                lock.unlock();
                Result result;
                try { result.geometry = prepareGeometry(snapshot); }
                catch (const std::exception& e) { result.error = e.what(); }
                lock.lock();
                if (state->stopped) return;
                if (state->revision == revision) state->ready = std::move(result);
            }
        }).detach();
        state->started = true;
    }
    ++state->revision;
    state->ready.reset();
    state->pending = std::move(snapshot);
    state->wake.notify_one();
}
bool GeometryPreparation::takeLatest(Result& result) {
    std::lock_guard lock(m_state->mutex);
    if (!m_state->ready) return false;
    result = std::move(*m_state->ready);
    m_state->ready.reset();
    return true;
}
} // namespace atom::render
