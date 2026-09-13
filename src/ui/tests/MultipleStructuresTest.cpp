#include "components/StructureModel.h"
#include "components/FileController.h"
#include "components/OpenGLViewport.h"
#ifdef ATOM_HAS_METAL
#include "components/MetalViewport.h"
#endif
#include "common/Camera.h"
#include "Structure.h"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
#include <stdexcept>
#include <functional>

using namespace atom;
namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool waitFor(const std::function<bool()>& predicate) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 10000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    return predicate();
}
std::shared_ptr<data::Structure> structure(float offset = 0) {
    auto s = std::make_shared<data::Structure>();
    s->addAtom(offset, 0, 0, 6);
    s->addAtom(offset + 1.2f, 0, 0, 8);
    s->addAtom(offset, 4, 0, 1);
    s->setSourcePath("/tmp/same.xyz");
    auto& cell = s->lattice();
    cell.defined = true;
    cell.matrix = {{{10, 1, 0}, {0, 10, 0}, {0, 0, 10}}};
    return s;
}
void documents() {
    ui::StructureModel model;
    const auto input = structure();
    model.addStructure(input);
    check(model.activeId() == 0 && model.structureCount() == 1, "First ID must be zero");
    check(model.originalStructure()->name() == "STRUCT_0_RAW", "Raw name");
    check(model.structureName() == "STRUCT_0_CURRENT", "Current name");
    check(model.originalStructure().get() != input.get(), "Document must own its original");
    model.applySharedAppearance(0, .1f);
    model.ensureBonds(1.1f);
    check(waitFor([&] { return model.bondCount() == 1; }), "Initial bonds");
    model.setSelectionMode(1);
    model.toggleAtomSelection(0);
    model.toggleBondSelection(0);
    model.applyAtomColorToSelection(Qt::magenta);
    model.applyAtomScaleToSelection(1.5f, 1);
    model.applyBondRadiusToSelection(.35f);
    const auto first = model.structure();
    const auto original = model.originalStructure();
    model.addStructure(input);
    check(model.activeId() == 1 && model.structure() != first, "Duplicate import must be independent");
    model.applySharedAppearance(1, .2f);
    const auto second = model.structure();
    model.setActiveIndex(0);
    model.applySharedAppearance(1, .2f);
    check(model.structure() == first && model.replicationX() == 1, "Switch must restore the original working copy");
    check(model.selectionMode() == 1 && model.selectedAtomCount() == 1, "Selection must persist");
    check(first->color(0).r == 1 && first->color(0).b == 1, "Custom atom color must persist across shared scheme changes");
    check(first->radius(0) > original->radius(0), "Custom atom size must persist");
    check(first->bonds().radius(0) == .35f, "Custom bond radius must persist");
    check(original->bonds().empty() && original->atomCount() == 3, "Original must remain immutable");
    model.clearSelection();
    model.toggleAtomSelection(2);
    check(model.deleteSelectedObjects(), "Deletion");
    check(model.atomCount() == 2 && first->atomCount() == 3, "Deletion must replace only active current copy");
    model.applySharedAppearance(0, .3f);
    check(model.structure()->color(0).b == 1 && model.structure()->bonds().radius(0) == .35f, "Overrides must survive deletion compaction");
    model.setActiveIndex(1);
    check(model.structure() == second, "Inactive edits must persist");
    model.resetToOriginal();
    check(model.atomCount() == 3 && model.structureName() == "STRUCT_1_CURRENT", "Reset active only, retain name");
    model.setActiveIndex(0);
    check(model.atomCount() == 2, "Reset must not change another structure");
    // A selected bond must not reappear just because the user switches away/back.
    model.clearSelection();
    model.toggleBondSelection(0);
    check(model.deleteSelectedObjects() && model.bondCount() == 0, "Bond deletion");
    model.setActiveIndex(1);
    model.setActiveIndex(0);
    model.ensureBonds(1.1f);
    QCoreApplication::processEvents();
    check(model.bondCount() == 0, "Switch regenerated a deleted bond");
    model.setSwitchingLocked(true);
    model.addStructure(structure(20));
    check(model.activeId() == 0 && model.structureCount() == 3, "Import during export must defer activation");
    model.setSwitchingLocked(false);
    check(model.activeId() == 2, "Deferred activation");
    model.setActiveIndex(-1);
    model.setActiveIndex(99);
    check(model.activeId() == 2, "Invalid indices");
    model.clear();
    model.addStructure(structure());
    check(model.activeId() == 3, "IDs must never be reused within a session");
}


void globalReplication() {
    ui::StructureModel model;
    model.addStructure(structure());
    model.addStructure(structure());
    model.setSelectionMode(1);
    model.toggleAtomSelection(0);
    model.deleteSelectedObjects();
    int updates = 0;
    QObject::connect(&model, &ui::StructureModel::structureUpdated, &model, [&] { ++updates; });
    model.replicateCell(2, 3, 1);
    check(updates == 1, "Global replication must update the viewport once");
    for (int index : {0, 1}) {
        model.setActiveIndex(index);
        check(model.atomCount() == 18 && model.replicationX() == 2 && model.replicationY() == 3,
              "Replication must rebuild every periodic structure from raw");
        check(model.structureName() == QString("STRUCT_%1_CURRENT").arg(index), "Replication must retain IDs");
        check(model.originalStructure()->atomCount() == 3 && model.originalStructure()->lattice().matrix[0][0] == 10,
              "Global replication modified raw input");
        check(model.structure()->lattice().matrix[0][0] == 20 && model.structure()->lattice().matrix[1][1] == 30,
              "Global replication did not scale each cell");
    }
    model.addStructure(structure());
    check(model.atomCount() == 18 && model.replicationY() == 3, "New imports must inherit global replication");
    model.setSelectionMode(1);
    model.toggleAtomSelection(0);
    model.deleteSelectedObjects();
    check(model.atomCount() == 17, "Replicated atom deletion");
    model.resetToOriginal();
    check(model.atomCount() == 18 && model.replicationY() == 3, "Reset must preserve global replication");
    model.setActiveIndex(0);
    check(model.atomCount() == 18, "Reset changed inactive data");
    auto molecule = structure(20);
    molecule->lattice().defined = false;
    model.addStructure(molecule);
    check(model.atomCount() == 3 && !model.hasUnitCell() && model.hasReplicableStructures(),
          "Nonperiodic imports must remain unreplicated");
    model.setSelectionMode(1);
    model.toggleAtomSelection(0);
    model.deleteSelectedObjects();
    const auto editedMolecule = model.structure();
    model.replicateCell(1, 1, 1);
    check(model.structure() == editedMolecule && model.atomCount() == 2,
          "Global replication must leave nonperiodic edits intact");
    for (int index : {0, 1, 2}) {
        model.setActiveIndex(index);
        check(model.atomCount() == 3 && model.replicationX() == 1, "Global replication must shrink from raw");
    }
    model.replicateCell(0, 1, 1);
    model.replicateCell(100, 1, 1);
    check(model.atomCount() == 3 && model.replicationX() == 1, "Invalid replication was applied");
    model.clear();
    check(!model.hasReplicableStructures() && model.replicationY() == 1, "Clear must reset global replication");
}

void pendingBondEdits() {
    ui::StructureModel model;
    model.addStructure(structure());
    model.ensureBonds(1.1f);
    // Delete the distant atom before the queued result can reach the model.
    model.setSelectionMode(1);
    model.toggleAtomSelection(2);
    model.deleteSelectedObjects();
    check(waitFor([&] { return !model.isDetectingBonds(); }), "Post-deletion bond detection stalled");
    check(model.bondCount() == 1, "Deleting during import prevented surviving bonds from being detected");
    model.ensureBonds(.1f);
    model.ensureBonds(1.1f);
    check(waitFor([&] { return !model.isDetectingBonds(); }), "Reverted cutoff detection stalled");
    check(model.bondCount() == 1, "Reverting the cutoff accepted an obsolete result");
}

class ControlledLoader : public io::AsyncFileLoader {
public:
    QStringList started;
    void loadFile(const QString& path) override {
        started.append(path);
        emit loadingStarted(path);
    }
    void cancel() override { emit loadingCancelled(); }
    void succeed() { emit loadingFinished(structure()); }
    void fail() { emit loadingFailed("Unreadable input"); }
};
void imports() {
    ui::StructureModel model;
    auto loader = std::make_unique<ControlledLoader>();
    auto* control = loader.get();
    ui::FileController files(nullptr, std::move(loader));
    files.loadFiles({"first.xyz", "bad.xyz"});
    files.loadFileUrls({QUrl::fromLocalFile("/tmp/third.xyz"), QUrl("https://invalid.example/file")});
    check(control->started.size() == 1 && files.isLoading(), "Imports must be queued");
    control->succeed();
    check(waitFor([&] { return control->started.size() == 2; }), "Queue must advance after success");
    check(model.activeId() == 0, "Successful import ID");
    control->fail();
    check(waitFor([&] { return control->started.size() == 3; }), "Queue must advance after failure");
    check(model.structureCount() == 1 && model.activeId() == 0, "Failure must preserve active structure");
    control->succeed();
    check(model.activeId() == 1 && !files.isLoading(), "Failed imports must not consume IDs");
    files.loadFiles({"cancel.xyz", "never.xyz"});
    files.cancelLoad();
    QCoreApplication::processEvents();
    check(control->started.size() == 4 && model.structureCount() == 2 && !files.isLoading(), "Cancellation must clear queued requests");
    files.loadFile("again.xyz");
    control->succeed();
    check(model.activeId() == 2, "Imports must resume after cancellation");
}

template<class Viewport> void viewport() {
    ui::StructureModel model;
    Viewport view;
    QCoreApplication::processEvents();
    auto first = structure();
    first->lattice().defined = false;
    model.addStructure(first);
    const auto centerData = first->geometricCenter();
    const QVector3D center(centerData[0], centerData[1], centerData[2]);
    check((view.camera().target() - center).length() < 1e-4f, "First structure not centered");
    view.setViewDirection(2);
    view.setCameraDistance(37);
    view.setFieldOfView(61);
    view.setAtomScale(1.7f);
    view.setEnableAO(true);
    view.setBondRadius(.23f);
    const auto camera = view.camera();
    const auto current = model.structure();
    auto translated = structure(100);
    translated->lattice().defined = false;
    model.addStructure(translated);
    check((view.camera().target() - center - QVector3D(100, 0, 0)).length() < 1e-4f,
          "Imported structure must use its own center");
    check(view.camera().orientation() == camera.orientation() && view.cameraDistance() == 37,
          "Recentering changed orientation or zoom");
    model.setActiveIndex(0);
    check((view.camera().target() - camera.target()).length() < 1e-4f && view.camera().orientation() == camera.orientation() && view.camera().projectionMatrix() == camera.projectionMatrix(), "Perspective camera changed on switch/import");
    check(view.atomScale() == 1.7f && view.enableAO(), "Shared view settings changed");
    view.setIsPerspective(false);
    view.setOrthographicScale(9);
    const auto ortho = view.camera();
    model.setActiveIndex(1);
    check((view.camera().target() - center - QVector3D(100, 0, 0)).length() < 1e-4f
              && view.orthographicScale() == 9, "Orthographic switch lost center or scale");
    model.setActiveIndex(0);
    check((view.camera().target() - ortho.target()).length() < 1e-4f && view.camera().orientation() == ortho.orientation() && view.camera().projectionMatrix() == ortho.projectionMatrix(), "Orthographic camera changed on switch");
    check(waitFor([&] { return model.bondCount() == 1; }), "Active bond detection did not complete");
    check(current->bonds().radius(0) == .23f, "Shared bond radius missing");
    // A -> B -> A while work is pending must reject the obsolete request even
    // though the current pointer becomes equal to the old request's pointer.
    for (int i = 0; i < 20; ++i) {
        model.setActiveIndex(i % 2);
        view.setBondScale(i % 2 ? .1f : 1.1f);
    }
    model.setActiveIndex(0);
    view.setBondScale(.1f);
    check(waitFor([&] { return model.bondCount() == 0; }), "Obsolete bond result was accepted");
    model.addStructure(structure());
    auto largerCell = structure();
    largerCell->lattice().matrix[0][0] = 20;
    model.addStructure(largerCell);
    check((view.camera().target() - QVector3D(10, 5.5f, 5)).length() < 1e-4f, "Periodic center must follow its cell");
    model.replicateCell(2, 1, 1);
    const float globalScale = view.orthographicScale();
    check(globalScale >= 40 * .6f, "Global fit must accommodate the largest replicated structure");
    check((view.camera().target() - QVector3D(20, 6, 5)).length() < 1e-4f, "Replication did not recenter camera");
    model.setActiveIndex(2);
    check(view.orthographicScale() == globalScale && model.atomCount() == 6,
          "Switch lost globally replicated geometry or camera scale");
    check((view.camera().target() - QVector3D(10, 6, 5)).length() < 1e-4f, "Replicated structure has wrong center");
    view.setAutoFitOnLoad(false);
    model.replicateCell(3, 1, 1);
    check(view.orthographicScale() == globalScale, "Replication ignored disabled auto-fit");
    model.clear();
    check(view.atomCount() == 0, "Clear did not detach viewport");
}
}
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    try {
        documents();
        globalReplication();
        pendingBondEdits();
        imports();
        viewport<ui::OpenGLViewport>();
#ifdef ATOM_HAS_METAL
        viewport<ui::MetalViewport>();
#endif
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    std::cout << "Structure isolation, IDs, import queue, camera sharing, and stale bond rejection passed\n";
}
