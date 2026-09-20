#include "components/StructureModel.h"
#include "components/AtomPropertiesModel.h"
#include "components/FileController.h"
#include "components/OpenGLViewport.h"
#ifdef ATOM_HAS_METAL
#include "components/MetalViewport.h"
#endif
#include "common/Camera.h"
#include "Structure.h"
#include <QGuiApplication>
#include <QHoverEvent>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
#include <stdexcept>
#include <functional>
#include <cmath>

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


void persistentObjectStrokes() {
    ui::StructureModel model;
    model.addStructure(structure());
    model.ensureBonds(1.1f);
    check(waitFor([&] { return !model.isDetectingBonds() && model.bondCount() == 1; }), "Stroke bonds setup");
    model.setSelectionMode(2);
    check(model.toggleMoleculeSelectionFromAtom(0), "Select molecule for strokes");
    check(model.selectedAtomCount() == 2 && model.selectedBondCount() == 1, "Molecule selection scope");
    check(model.applyStrokeWidthToSelection(.24f) && model.applyStrokeColorToSelection(Qt::blue), "Apply molecule strokes");
    const auto verifySaved = [&] {
        auto s = model.structure();
        check(s->stroke(0).width == .24f && s->stroke(1).width == .24f && s->stroke(0).color.b == 1,
              "Selected atoms lost their saved strokes");
        check(s->bonds().stroke(0).width == .24f && s->bonds().stroke(0).color.b == 1,
              "Selected bond lost its saved stroke");
        check(!model.originalStructure()->stroke(0).overridden(), "Stroke edit touched raw geometry");
    };
    verifySaved();
    check(!model.structure()->stroke(2).overridden(), "Stroke edit touched an unselected object");
    model.setSelectionMode(0);
    verifySaved();
    check(!model.applyStrokeWidthToSelection(.4f), "Stroke edit accepted without selection");
    model.setSelectionMode(2);
    model.toggleMoleculeSelectionFromAtom(0);
    verifySaved();
    model.setEditsLocked(true);
    check(!model.applyStrokeColorToSelection(Qt::red), "Locked stroke edit accepted");
    model.setEditsLocked(false);
    model.addStructure(structure(20));
    check(!model.structure()->stroke(0).overridden(), "Strokes leaked to another document");
    model.setActiveIndex(0);
    verifySaved();
    model.clearSelection();
    model.ensureBonds(1.2f);
    check(waitFor([&] { return !model.isDetectingBonds(); }), "Stroke bond refresh");
    verifySaved();
    // ASE reorders surviving atoms: strokes follow stable atom/bond identity.
    auto old = model.structure();
    auto reordered = std::make_shared<data::Structure>();
    for (size_t index : {size_t(1), size_t(0), size_t(2)}) {
        auto pos = old->position(index);
        auto i = reordered->addAtom(pos[0], pos[1], pos[2], old->atomicNumber(index));
        reordered->setAtomId(i, old->atomId(index));
    }
    check(model.applyShellStructure(model.activeId(), model.document(model.activeId())->revision, reordered),
          "Stroke shell update");
    check(waitFor([&] { return !model.isDetectingBonds(); }), "Stroke shell bond refresh");
    verifySaved();
    model.setSelectionMode(1);
    model.toggleAtomSelection(2);
    check(model.deleteSelectedObjects(), "Stroke deletion setup");
    verifySaved();
    model.setSelectionMode(2);
    model.toggleMoleculeSelectionFromAtom(0);
    check(model.resetSelectedObjects(.1f, 0), "Reset selected strokes");
    check(!model.structure()->stroke(0).overridden() && !model.structure()->bonds().stroke(0).overridden(),
          "Reset selected objects left stroke overrides");
}

void globalStrokeOverrides() {
    ui::StructureModel model;
    for (int i = 0; i < 2; ++i) {
        auto input = structure(i * 20);
        input->bonds().addBond(0, 1);
        model.addStructure(input);
        model.setSelectionMode(2);
        model.toggleMoleculeSelectionFromAtom(0);
        model.applyStrokeWidthToSelection(.25f);
        model.applyStrokeColorToSelection(Qt::blue);
        model.setSelectionMode(0);
    }
    model.setSwitchingLocked(true);
    check(!model.clearStrokeWidthOverrides(), "Capture lock allowed global stroke edits");
    model.setSwitchingLocked(false);
    check(model.clearStrokeWidthOverrides(), "Global stroke width reset failed");
    for (const auto& document : model.documents()) {
        const auto& s = *document->current;
        check(s.stroke(0).width < 0 && s.bonds().stroke(0).width < 0,
              "Global width missed an active or inactive object override");
        check(s.stroke(0).color.b == 1 && s.bonds().stroke(0).color.b == 1,
              "Global width erased custom stroke colors");
        check(!document->raw->stroke(0).overridden() && !document->raw->bonds().stroke(0).overridden(),
              "Global stroke edit touched raw originals");
    }
    model.setActiveIndex(0);
    model.setSelectionMode(2);
    model.toggleMoleculeSelectionFromAtom(0);
    model.applyStrokeWidthToSelection(.3f);
    model.setEditsLocked(true);
    check(!model.clearStrokeColorOverrides(), "Python lock allowed global stroke edits");
    model.setEditsLocked(false);
    check(model.clearStrokeColorOverrides(), "Global stroke color reset failed");
    for (const auto& document : model.documents()) {
        const auto& s = *document->current;
        check(s.stroke(0).color.r < 0 && s.bonds().stroke(0).color.r < 0,
              "Global color missed an active or inactive object override");
    }
    check(model.structure()->stroke(0).width == .3f && model.structure()->bonds().stroke(0).width == .3f,
          "Global color erased custom stroke widths");
    check(model.selectedAtomCount() == 2 && model.selectedBondCount() == 1,
          "Global stroke edit changed selection");
}

void atomRadiusModes() {
    ui::StructureModel model;
    auto close = [](float a, float b) { return std::abs(a - b) < 1e-5f; };
    model.setAtomRadiusType(1); // Also works before importing anything.
    model.addStructure(structure());
    check(close(model.structure()->radius(0), 1.77f), "New import must use Alvarez radii");
    check(close(model.originalStructure()->radius(0), .76f), "Radius mode modified raw input");
    model.ensureBonds(1.1f);
    check(waitFor([&] { return !model.isDetectingBonds(); }) && model.bondCount() == 1,
          "Alvarez visualization must retain covalent bond detection");
    model.setSelectionMode(1);
    model.toggleAtomSelection(0);
    model.applyAtomScaleToSelection(1.5f, 2.0f);
    check(close(model.structure()->radius(0) * 2, 1.77f * 1.5f), "Selected scaling used wrong radius type");
    int geometryChanges = 0;
    QObject::connect(&model, &ui::StructureModel::structureGeometryChanged, [&] { ++geometryChanges; });
    model.setAtomRadiusType(0);
    check(close(model.structure()->radius(0), .76f * .75f) && geometryChanges == 1,
          "Switch must preserve atom scale and invalidate viewport geometry");
    model.setAtomRadiusType(1);
    check(close(model.structure()->radius(0), 1.77f * .75f) && model.bondCount() == 1,
          "Round trip changed scale or bonds");
    model.resetSelectedObjects(.1f, 0);
    check(close(model.structure()->radius(0), 1.77f), "Selected reset lost Alvarez radius");
    model.addStructure(structure(20));
    model.setAtomRadiusType(0);
    check(close(model.documents()[0]->current->radius(0), 1.77f), "Inactive radius work must stay lazy");
    model.setActiveIndex(0);
    check(close(model.structure()->radius(0), .76f), "Activation did not apply pending radius type");
    model.setAtomRadiusType(1);
    model.resetToOriginal();
    check(close(model.structure()->radius(0), 1.77f), "Reset lost radius type");
    model.replicateCell(2, 1, 1);
    check(model.atomCount() == 6 && close(model.structure()->radius(3), 1.77f), "Replication lost radius type");
    model.setActiveIndex(1);
    check(close(model.structure()->radius(0), 1.77f), "Inactive replication lost radius type");
    model.setSelectionMode(1);
    model.toggleAtomSelection(0);
    model.applyAtomScaleToSelection(1.5f, 1);
    const auto id = model.activeId();
    const auto entry = model.document(id);
    auto incoming = entry->current->clone();
    incoming->addAtom(0, 0, 8, 6);
    incoming->updateRadiiFromElements(1, false); // ASE bridge delivers covalent defaults.
    model.setActiveIndex(0);
    model.setAtomRadiusType(0); // Leave the shell target with a pending mode change.
    check(model.applyShellStructure(id, entry->revision, std::move(incoming)), "Inactive shell update failed");
    model.setAtomRadiusType(1);
    model.setActiveIndex(1);
    check(close(model.structure()->radius(0), 1.77f * 1.5f) &&
          close(model.structure()->radius(model.atomCount() - 1), 1.77f),
          "Shell updates must preserve scaling and style new atoms in the current mode");
    model.setSwitchingLocked(true);
    model.setAtomRadiusType(0);
    check(model.atomRadiusType() == 1, "Image capture must lock radius changes");
    model.setSwitchingLocked(false);
    model.setAtomRadiusType(99);
    check(model.atomRadiusType() == 1, "Invalid radius type accepted");
}

void speciesProperties() {
    ui::StructureModel model;
    auto input = structure();
    input->setAtomicNumber(2, 6);
    // Internal IDs are opaque and may be nonsequential after Python imports.
    input->setAtomId(0, 501);
    input->setAtomId(1, 71);
    input->setAtomId(2, 9002);
    input->updateRadiiFromElements();
    model.addStructure(input);
    model.applySharedAppearance(0, .1f);
    auto* rows = model.atomProperties();
    using Roles = ui::AtomPropertiesModel;
    auto value = [&](int row, int role) { return rows->data(rows->index(row, 0), role); };
    auto close = [](double a, double b) { return std::abs(a - b) < 1e-5; };
    check(rows->rowCount() == 3 && value(0, Roles::AtomIdentifier).toInt() == 0 &&
          value(2, Roles::AtomIdentifier).toInt() == 2, "Original zero-based atom order");
    check(value(0, Roles::SpeciesName).toString() == "Carbon (C)", "Species name");
    check(model.applySpeciesRadius(6, 1.23) && model.applySpeciesColor(6, Qt::red), "Species edits");
    for (int i : {0, 2}) {
        check(close(value(i, Roles::AtomRadius).toDouble(), 1.23), "Species radius row did not update");
        check(value(i, Roles::AtomColor).value<QColor>() == QColor(Qt::red), "Species color row did not update");
    }
    check(close(model.structure()->radius(1), .66), "Other species radius changed");
    check(close(model.originalStructure()->radius(0), .76) && model.originalStructure()->color(0).r != 1,
          "Species edits changed raw input");
    check(!model.applySpeciesRadius(6, 0) && !model.applySpeciesRadius(6, -1) &&
          !model.applySpeciesRadius(6, std::numeric_limits<double>::infinity()) &&
          !model.applySpeciesRadius(6, std::numeric_limits<double>::quiet_NaN()) &&
          !model.applySpeciesRadius(118, 1), "Invalid radius accepted");
    model.ensureBonds(1.1f);
    check(waitFor([&] { return !model.isDetectingBonds(); }), "Species bond detection");
    check(model.structure()->bonds().startColor(0).r == 1, "New bonds lost species color");
    model.applySharedAppearance(1, .1f);
    check(value(0, Roles::AtomColor).value<QColor>() == QColor(Qt::red), "Shared colors erased species edit");
    model.addStructure(input);
    check(close(model.structure()->radius(0), .76), "Species edit leaked to another document");
    model.setAtomRadiusType(1);
    model.setAtomRadiusType(0);
    model.setActiveIndex(0);
    check(close(model.structure()->radius(0), .76), "Inactive A-B-A did not clear custom radii");
    check(value(0, Roles::AtomColor).value<QColor>() == QColor(Qt::red), "Radius reset erased colors");
    model.applySpeciesRadius(6, 1.23);
    model.setAtomRadiusType(1);
    check(close(value(0, Roles::AtomRadius).toDouble(), 1.77), "Radius scheme did not reset custom value");
    model.applySpeciesRadius(6, 1.23);
    model.replicateCell(2, 1, 1);
    check(rows->rowCount() == 6 && close(value(3, Roles::AtomRadius).toDouble(), 1.23) &&
          value(3, Roles::AtomColor).value<QColor>() == QColor(Qt::red), "Replication lost species edits");
    model.setSelectionMode(1);
    model.toggleAtomSelection(1);
    model.deleteSelectedObjects();
    check(rows->rowCount() == 5 && value(1, Roles::AtomIdentifier).toInt() == 2,
          "Deletion renumbered original IDs");
    const auto current = model.structure();
    auto reordered = std::make_shared<data::Structure>();
    for (size_t i = current->atomCount(); i-- > 0;) {
        auto j = reordered->addAtom(0, float(i), 0, current->atomicNumber(i));
        reordered->setAtomId(j, current->atomId(i));
    }
    reordered->addAtom(0, 10, 0, 6);
    check(model.applyShellStructure(model.activeId(), model.document(model.activeId())->revision, reordered), "Reordered shell update");
    check(value(0, Roles::AtomIdentifier).toInt() == 0 && value(1, Roles::AtomIdentifier).toInt() == 2,
          "Shell reorder changed original list ordering");
    check(close(value(5, Roles::AtomRadius).toDouble(), 1.23) &&
          value(5, Roles::AtomColor).value<QColor>() == QColor(Qt::red), "New shell atoms lost species edits");
    model.setSwitchingLocked(true);
    check(!model.applySpeciesRadius(6, 2) && !model.applySpeciesColor(6, Qt::blue), "Capture lock ignored");
    model.setSwitchingLocked(false);
    model.setEditsLocked(true);
    check(!model.applySpeciesRadius(6, 2) && !model.applySpeciesColor(6, Qt::blue), "Shell lock ignored");
    model.setEditsLocked(false);
    model.resetToOriginal();
    check(close(value(0, Roles::AtomRadius).toDouble(), 1.77) &&
          value(0, Roles::AtomColor).value<QColor>() != QColor(Qt::red), "Reset retained species edits");
    model.clear();
    check(rows->rowCount() == 0, "Cleared structure left stale atom rows");
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

template<class Viewport> void hoverIds() {
    ui::StructureModel model;
    Viewport view;
    view.setWidth(400);
    view.setHeight(240);
    view.setShowBonds(false);
    view.setIsPerspective(false);
    view.setViewDirection(static_cast<int>(render::ViewDirection::PlusZ));
    QCoreApplication::processEvents();
    auto input = std::make_shared<data::Structure>();
    input->addAtom(-4, 0, 0, 6);
    input->addAtom(0, 0, 0, 8);
    input->addAtom(4, 0, 0, 1);
    input->setAtomId(0, 501);
    input->setAtomId(1, 71);
    input->setAtomId(2, 9002);
    model.addStructure(input);
    view.setOrthographicScale(8);
    const auto hover = [&](size_t index) {
        const auto p = model.structure()->position(index);
        const auto screen = view.camera().worldToScreen(QVector3D(p[0], p[1], p[2]), 400, 240);
        const QPointF point(screen.x(), screen.y());
        QHoverEvent event(QEvent::HoverMove, point, point, point);
        QCoreApplication::sendEvent(&view, &event);
        return view.hoverStatus();
    };
    check(hover(0) == "ID: 0    |    Element: C    |    Position: (-4.0000, 0.0000, 0.0000)",
          "Hover must show the zero-based import ID before element and position");
    model.setSelectionMode(1);
    model.toggleAtomSelection(0);
    check(model.deleteSelectedObjects(), "Hover deletion setup");
    check(hover(0).startsWith("ID: 1    |    Element: O"), "Deletion renumbered hover ID");
    auto reordered = std::make_shared<data::Structure>();
    reordered->addAtom(4, 0, 0, 1);
    reordered->setAtomId(0, 9002);
    reordered->addAtom(0, 0, 0, 8);
    reordered->setAtomId(1, 71);
    check(model.applyShellStructure(model.activeId(), model.document(model.activeId())->revision, reordered),
          "Hover reorder setup");
    check(hover(0).startsWith("ID: 2    |    Element: H"), "Reordering changed hover ID");
    check(hover(1).startsWith("ID: 1    |    Element: O"), "Reordering changed surviving hover ID");
    model.addStructure(input);
    check(hover(0).startsWith("ID: 0    |    Element: C"), "Hover used the previous document's IDs");
    model.setActiveIndex(0);
    check(hover(0).startsWith("ID: 2    |    Element: H"), "Document switch lost hover IDs");
    QHoverEvent leave(QEvent::HoverLeave, QPointF(), QPointF(), QPointF());
    QCoreApplication::sendEvent(&view, &leave);
    check(view.hoverStatus().isEmpty(), "Leaving viewport retained hover text");
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
        persistentObjectStrokes();
        globalStrokeOverrides();
        atomRadiusModes();
        speciesProperties();
        globalReplication();
        pendingBondEdits();
        imports();
        viewport<ui::OpenGLViewport>();
        hoverIds<ui::OpenGLViewport>();
#ifdef ATOM_HAS_METAL
        viewport<ui::MetalViewport>();
        hoverIds<ui::MetalViewport>();
#endif
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    std::cout << "Structure isolation, IDs, import queue, camera sharing, and stale bond rejection passed\n";
}
