#include "common/PreparedGeometry.h"
#include "common/BondRenderData.h"
#include "common/Picking.h"
#include "common/Camera.h"
#include "common/RenderSettings.h"
#include "Structure.h"
#include <chrono>
#include <iostream>
#include <random>
#include <thread>

using namespace atom;
namespace {
bool check(bool ok, const char* message) {
    if (!ok) std::cerr << message << '\n';
    return ok;
}
bool wait(render::GeometryPreparation& worker, render::GeometryPreparation::Result& result) {
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < limit) {
        if (worker.takeLatest(result)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
}
int main() {
    data::Structure structure;
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> position(-10,10), radius(.01f, .8f), pixel(0,600);
    structure.lattice().matrix = {{{20,0,0},{2,20,0},{1,2,20}}};
    for (int i = 0; i < 250; ++i) {
        structure.addAtom(position(rng), position(rng), position(rng), 6);
        structure.radii()[i] = radius(rng);
        if (i) {
            structure.bonds().addBond(i-1, i, i%4 == 0, i%5 == 0 ? -1 : 0, i%3 == 0);
            structure.bonds().setRadius(i-1, radius(rng));
        }
    }
    const auto snapshot = render::captureGeometry(&structure);
    auto prepared = render::prepareGeometry(snapshot);
    const auto packed = render::packBondRenderData(&structure, render::BondPositionPacking::XYZW4);
    for (size_t i = 0; i < prepared->bondStarts.size(); ++i) for (size_t axis = 0; axis < 4; ++axis) {
        if (!check(prepared->bondStarts[i][axis] == packed.startPositions[i*4+axis] &&
                   prepared->bondEnds[i][axis] == packed.endPositions[i*4+axis], "Periodic bond packing changed")) return 1;
    }
    render::Camera camera;
    camera.setDistance(45); camera.setOrthoScale(25); camera.setAspectRatio(1);
    render::RenderSettings settings;
    for (bool perspective : {false, true}) for (bool atoms : {false, true})
    for (bool bonds : {false, true}) for (float scale : {.1f, 1.f, 3.f}) {
        camera.setProjection(perspective);
        camera.orbit(17, 13);
        settings.showAtoms = atoms; settings.showBonds = bonds; settings.atomScale = scale;
        for (int i = 0; i < 800; ++i) {
            const float x = pixel(rng), y = pixel(rng);
            const auto linear = render::pickStructureObject(&structure, camera, settings, x, y, 600, 600);
            const auto indexed = render::pickStructureObject(&structure, camera, settings, x, y, 600, 600, prepared.get());
            if (!check(linear.type == indexed.type && linear.index == indexed.index &&
                       linear.rayDistance == indexed.rayDistance, "BVH picking differs from linear picking")) return 1;
        }
    }
    // Snapshot ownership and coalescing: only the final revision may be published.
    render::GeometryPreparation worker;
    worker.request(snapshot);
    for (int i = 0; i < 32; ++i) {
        data::Structure replacement;
        replacement.addAtom(float(i),0,0,6);
        worker.request(render::captureGeometry(&replacement));
    }
    render::GeometryPreparation::Result latest;
    if (!check(wait(worker, latest) && latest.geometry && latest.geometry->atoms.size() == 1 &&
               latest.geometry->atoms[0][0] == 31, "Worker published an obsolete revision")) return 1;
    // Failure is delivered, and a subsequent valid edit recovers normally.
    auto invalid = snapshot; invalid.bonds[0].atomIndex1 = 999999;
    worker.request(std::move(invalid));
    if (!check(wait(worker, latest) && !latest.geometry && !latest.error.empty(), "Worker lost preparation failure")) return 1;
    worker.request({});
    if (!check(wait(worker, latest) && latest.geometry && latest.geometry->bvh.nodes.empty(), "Empty scene failed")) return 1;
    for (int i = 0; i < 8; ++i) {
        render::GeometryPreparation destroyedDuringBuild;
        destroyedDuringBuild.request(snapshot);
    }
    // Coincident primitives preserve the original ascending-index tie break.
    data::Structure coincident;
    for (int i = 0; i < 40; ++i) coincident.addAtom(0,0,0,6);
    auto ties = render::prepareGeometry(render::captureGeometry(&coincident));
    camera.setPresetView(render::ViewDirection::PlusZ); camera.setProjection(false);
    settings.showAtoms = true; settings.showBonds = false; settings.atomScale = 1;
    const auto hit = render::pickStructureObject(&coincident, camera, settings, 300,300,600,600,ties.get());
    if (!check(hit.type == render::PickObjectType::Atom && hit.index == 0, "Coincident atom tie break changed")) return 1;
    // BVH storage reserve covers the actual balanced tree, rather than 2N nodes.
    std::vector<render::PrimitiveBounds> bounds(100000, {-1,-1,-1,1,1,1,0,0,0,1});
    auto bvh = render::buildBVH(bounds.data(), bounds.size());
    if (!check(bvh.nodes.capacity() < bounds.size(), "BVH still reserves per-primitive internal nodes")) return 1;
    std::cout << "Prepared geometry, worker lifecycle, and 19200 indexed picking comparisons passed\n";
}
