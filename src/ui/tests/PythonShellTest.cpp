#include "components/PythonShellController.h"
#include "components/StructureModel.h"
#include "Structure.h"
#include "StructureSnapshot.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QPlainTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace atom;
static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static bool waitFor(const std::function<bool()>& condition, int timeout = 15000) {
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents(); QThread::msleep(5);
    }
    return condition();
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QPalette dark = app.palette();
    dark.setColor(QPalette::WindowText, Qt::white);
    dark.setColor(QPalette::ButtonText, Qt::white);
    dark.setColor(QPalette::Window, Qt::black);
    app.setPalette(dark);
    if (argc < 2) return 1;
    try {
        ui::StructureModel model;
        auto input = std::make_shared<data::Structure>();
        input->addAtom(0, 0, 0, 29); input->addAtom(2, 0, 0, 29); input->addAtom(4, 0, 0, 29);
        input->setPrecisePosition(0, 0.123456789012345, 0, 0);
        input->lattice().defined = true;
        input->lattice().matrix = {{{10,0,0},{0,10,0},{0,0,10}}};
        model.addStructure(input); model.addStructure(input);
        QTemporaryDir environments;
        ui::PythonShellController shell(&model, QString::fromLocal8Bit(argv[1]), nullptr, environments.path());
        QString output;
        QObject::connect(&shell, &ui::PythonShellController::output, [&](const QString& text, bool) {
            output += text; std::cout << text.toStdString() << std::flush;
        });
        bool success = false;
        int finished = 0, frames = 0;
        QObject::connect(&shell, &ui::PythonShellController::runFinished, [&](bool result) { success = result; ++finished; });
        QObject::connect(&model, &ui::StructureModel::documentGeometryChanged, [&] { ++frames; });
        // This controller test supplies presentation acknowledgements; actual Metal
        // frame delivery is tested separately by the viewport integration test.
        QTimer presentation;
        QObject::connect(&presentation, &QTimer::timeout, &model, &ui::StructureModel::finishLiveFrame);
        presentation.start(30);
        shell.openWindow();
        check(waitFor([&] { return shell.ready(); }), "Worker failed to start");
        const auto run = [&](const QString& code, bool expected = true, int timeout = 15000) {
            const int before = finished;
            shell.run(code);
            check(model.editsLocked(), "Geometry edits were not locked");
            check(waitFor([&] { return finished > before; }, timeout), "Python run timed out");
            check(success == expected, "Unexpected Python run result");
            check(!model.editsLocked(), "Geometry edits remained locked");
        };
        run("alias = STRUCT_0\nalias.positions[0, 2] = 1.23456789012345\nprint('persistent session')");
        check(model.activeId() == 1, "Updating an inactive document switched the viewport");
        check(std::abs(model.document(0)->current->precisePosition(0)[2] - 1.23456789012345) < 1e-14, "Position precision lost");
        check(model.document(0)->raw->position(0)[2] == 0, "Python modified original geometry");
        run("assert alias is STRUCT_0\nassert STRUCT_0.positions[0, 2] == 1.23456789012345\nstudio.show(STRUCT_0)");
        check(model.activeId() == 0, "studio.show failed");
        model.ensureBonds(1.1f); // No viewport in this controller-only fixture.
        check(waitFor([&] { return !model.isDetectingBonds(); }), "Initial bond detection stalled");
        check(model.structure()->bonds().findBond(0, 1) >= 0, "Expected copper bond missing");
        model.setSelectionMode(2);
        model.toggleBondSelection(model.structure()->bonds().findBond(0, 1));
        check(model.deleteSelectedObjects(), "Could not delete a bond");
        run("STRUCT_0.positions[0, 1] += .01");
        check(waitFor([&] { return !model.isDetectingBonds(); }), "Updated bond detection stalled");
        check(model.structure()->bonds().findBond(0, 1) < 0, "Python update resurrected a deleted bond");
        model.setSelectionMode(1); model.toggleAtomSelection(0);
        model.applyAtomColorToSelection(QColor("magenta"));
        run("STRUCT_0 = STRUCT_0[[2, 0]]");
        check(model.atomCount() == 2 && model.structure()->atomSelected(1), "Reordering lost atom identity");
        check(model.structure()->color(1).r == 1 && model.structure()->color(1).b == 1, "Reordering lost color override");
        model.resetToOriginal();
        run("assert len(STRUCT_0) == 3\nassert STRUCT_0.positions[0, 2] == 0");
        const int beforeFrames = frames;
        run("import time\nfor i in range(8):\n    STRUCT_0.positions[:, 2] += .1\n    studio.update(STRUCT_0)\n    time.sleep(.12)");
        check(frames - beforeFrames >= 4, "Intermediate updates were not delivered");
        run("from ase.calculators.emt import EMT\nrecovery_alias = STRUCT_0\nrecovery_calc = EMT()\nSTRUCT_0.calc = recovery_calc\nexpected_0 = STRUCT_0.positions.copy()\nexpected_1 = STRUCT_1.positions.copy()");
        const auto beforeError = model.structure()->precisePosition(0);
        output.clear();
        run("time.sleep(.12)\nSTRUCT_0.positions[0, 0] = np.nan\nstudio.update(STRUCT_0)", false);
        check(output.contains("ValueError: STRUCT_0: Positions must be finite"),
              "Live validation error did not identify its structure");
        check(model.structure()->precisePosition(0) == beforeError, "Invalid geometry replaced the last valid state");
        run("assert recovery_alias is STRUCT_0\nassert STRUCT_0.calc is recovery_calc\nassert np.array_equal(recovery_alias.positions, expected_0)");
        const auto beforeFinalError = model.document(0)->current->precisePosition(0);
        const auto beforeInactiveError = model.document(1)->current->precisePosition(0);
        output.clear();
        run("STRUCT_0.positions[0, 2] += 1\nSTRUCT_1.positions[0, 0] = np.inf", false);
        check(output.contains("ValueError: STRUCT_1: Positions must be finite"),
              "Final validation error did not identify the invalid inactive structure");
        check(model.document(0)->current->precisePosition(0) == beforeFinalError,
              "Final validation published part of a failed run");
        check(model.document(1)->current->precisePosition(0) == beforeInactiveError,
              "Invalid inactive geometry replaced the last valid state");
        run("assert np.array_equal(STRUCT_0.positions, expected_0)\nassert np.array_equal(STRUCT_1.positions, expected_1)");
        check(model.document(0)->current->precisePosition(0) == beforeFinalError,
              "An unrelated later run published previously discarded edits");
        run("STRUCT_0.positions[0, 2] += .25\nstudio.update(STRUCT_0)\naccepted_positions = STRUCT_0.positions.copy()\ntime.sleep(.12)\nSTRUCT_0.positions[0, 0] = 1e16\nstudio.update(STRUCT_0)", false);
        check(std::abs(model.document(0)->current->precisePosition(0)[2] - beforeFinalError[2] - .25) < 1e-14,
              "Recovery discarded a valid live frame before the error");
        run("assert np.array_equal(recovery_alias.positions, accepted_positions)\nassert STRUCT_0.calc is recovery_calc");
        run("STRUCT_1 = None", false);
        run("assert isinstance(STRUCT_1, Atoms)\nassert np.array_equal(STRUCT_1.positions, expected_1)");
        run("raise ValueError('test traceback')", false);
        check(output.contains("ValueError: test traceback"), "Traceback was not reported");
        run("from ase.build import bulk\nfrom ase.calculators.emt import EMT\nfrom ase.optimize import BFGS\ncu = studio.add(bulk('Cu', cubic=True) * (2, 2, 2))\ncu.rattle(stdev=.1, seed=7)\ncu.calc = EMT()\nopt = BFGS(cu, logfile='-')\nopt.attach(studio.update, interval=1, atoms=cu)\nopt.run(fmax=.05, steps=100)\nassert np.linalg.norm(cu.get_forces(), axis=1).max() < .05", true, 30000);
        check(model.structureCount() == 3 && model.atomCount() == 32, "Relaxation did not create and publish a document");
        check(model.structure()->hasEnergy(), "Cached energy missing from relaxation result");
        shell.run("before_stop = cu.positions.copy()\ncu.positions[0, 0] = np.nan\nprint('loop-started')\nwhile True:\n    pass");
        check(waitFor([&] { return output.contains("loop-started"); }), "Loop failed to start");
        auto count = model.atomCount(); model.resetToOriginal(); model.replicateCell(2, 2, 2);
        check(model.atomCount() == count, "Conflicting GUI edit was accepted during execution");
        shell.stop();
        check(waitFor([&] { return !shell.running(); }, 6000), "Stop failed");
        run("assert cu is STRUCT_2\nassert np.array_equal(cu.positions, before_stop)\nassert len(cu) == 32\nprint('recovered')");
#ifndef Q_OS_WIN
        shell.run("import signal, time\nsignal.signal(signal.SIGINT, signal.SIG_IGN)\nprint('blocking-started')\ntime.sleep(30)");
        check(waitFor([&] { return output.contains("blocking-started"); }), "Blocking call failed to start");
        shell.stop();
        check(waitFor([&] { return !shell.running() && shell.ready(); }, 10000), "Forced restart failed");
        run("assert 'cu' not in globals()\nassert len(STRUCT_2) == 32");
#endif
        // Invalid IPC payload must not coerce arbitrary JSON to coordinates.
        auto malformed = io::structureSnapshot(*model.structure());
        auto positions = malformed["positions"].toArray(); positions[0] = QJsonArray{"bad", 0, 0}; malformed["positions"] = positions;
        bool rejected = false;
        try { io::structureFromSnapshot(malformed); } catch (const std::exception&) { rejected = true; }
        check(rejected, "Malformed transport data accepted");
        auto noCell = model.structure()->clone();
        noCell->lattice().defined = false;
        const auto noCellSnapshot = io::structureSnapshot(*noCell);
        for (const auto& row : noCellSnapshot["cell"].toArray())
            for (const auto& coordinate : row.toArray())
                check(coordinate.toDouble() == 0, "Snapshot invented a cell for a structure without a lattice");
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (widget->objectName() != "interactiveShellWindow") continue;
            check(widget->palette().color(QPalette::WindowText).lightness() < 100,
                  "Shell text inherited a dark-system palette");
            check(widget->findChild<QLabel*>("shellStatus")->palette().color(QPalette::WindowText).lightness() < 100,
                  "Shell child label inherited a dark-system palette");
            check(widget->findChild<QPlainTextEdit*>("shellEditor") != nullptr, "Editor missing");
            check(widget->findChild<QPushButton*>("shellRun")->isEnabled(), "Run button remained disabled");
            widget->grab().save("/tmp/atom-python-shell.png");
            widget->close(); shell.openWindow();
        }
        shell.shutdown();
        std::cout << "PASS: persistent session, documents, precision, IDs, streaming, EMT relaxation, cancellation and restart\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
