#include "components/FileController.h"
#include "components/StructureModel.h"
#include "Structure.h"
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QTimer>
#include <iostream>
#include <limits>

using namespace atom;

namespace {

std::shared_ptr<data::Structure> makeStructure(bool withLattice, bool periodic) {
    auto structure = std::make_shared<data::Structure>();
    structure->addAtom(-0.2f, 0.3f, 1.1f, 8);
    structure->addAtom(1.7f, 2.4f, 0.5f, 6);
    structure->addAtom(3.2f, 0.4f, 2.6f, 1);
    structure->addAtom(0.3f, 3.1f, 1.7f, 6);
    auto& cell = structure->lattice();
    cell.defined = withLattice;
    cell.matrix = {{{4.5, 0.75, -0.2}, {0.3, 5.25, 0.4}, {-0.15, 0.65, 6.5}}};
    cell.pbc = {periodic, false, periodic};
    return structure;
}

QJsonObject describe(const data::Structure& structure) {
    QJsonArray positions, numbers, cell, pbc;
    for (size_t i = 0; i < structure.atomCount(); ++i) {
        const auto p = structure.position(i);
        positions.append(QJsonArray{p[0], p[1], p[2]});
        numbers.append(structure.atomicNumber(i));
    }
    for (const auto& vector : structure.lattice().matrix) {
        cell.append(QJsonArray{vector[0], vector[1], vector[2]});
    }
    for (bool flag : structure.lattice().pbc) pbc.append(flag);
    return {{"positions", positions}, {"numbers", numbers}, {"cell", cell}, {"pbc", pbc},
            {"has_lattice", structure.hasLattice()}};
}

struct ExportOutcome { bool success = false; QString error; };

ExportOutcome exportAndWait(ui::FileController& files, ui::StructureModel& model,
                            const QString& path, const QString& format, bool editAfterStart = false) {
    QEventLoop loop;
    bool finished = false;
    ExportOutcome outcome;
    const auto success = QObject::connect(&files, &ui::FileController::structureExported, &loop,
        [&](const QString& savedPath) { outcome.success = savedPath == path; finished = true; loop.quit(); });
    const auto failure = QObject::connect(&files, &ui::FileController::structureExportFailed, &loop,
        [&](const QString& error) { outcome.error = error; finished = true; loop.quit(); });
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    files.exportStructure(path, format);
    if (editAfterStart && model.hasStructure()) {
        model.addStructure(makeStructure(false, false));
        model.setActiveIndex(0);
        model.structure()->setPosition(0, 1000, 2000, 3000);
        model.clear(); // The worker must own an independent snapshot.
    }
    if (!finished) {
        timeout.start(15000);
        loop.exec();
    }
    QObject::disconnect(success);
    QObject::disconnect(failure);
    if (!finished || files.isExporting()) return {false, "Export did not finish"};
    return outcome;
}

bool writeBytes(const QString& path, const QByteArray& text) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
}

QByteArray readBytes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 1;
    const QDir directory(QString::fromLocal8Bit(argv[1]));
    if (!QDir().mkpath(directory.path())) return 1;
    QLocale::setDefault(QLocale(QLocale::German)); // File numbers must still use decimal points.
    ui::StructureModel model;
    ui::FileController files;
    QJsonArray fixtures;

    for (const QString& scenario : {QString("edited"), QString("no-cell"), QString("nonperiodic-cell")}) {
        for (const auto& type : io::structureFileTypes()) {
            const bool hasCell = scenario != "no-cell";
            const auto original = makeStructure(hasCell, scenario == "edited");
            model.setStructure(original);
            if (scenario == "edited") {
                model.replicateCell(2, 1, 2);
                model.setSelectionMode(1);
                model.toggleAtomSelection(1);
                model.toggleAtomSelection(13);
                if (!model.deleteSelectedObjects() || model.atomCount() != 14 || original->atomCount() != 4) return 2;
                const auto p = model.structure()->position(0);
                model.structure()->setPosition(0, p[0] - 12, p[1], p[2]); // An unwrapped position.
            }
            QJsonObject expected = describe(*model.structure());
            const QString path = directory.filePath(scenario + "-" + type.id);
            const bool rejected = !hasCell && type.format == io::StructureFileFormat::Poscar;
            if (rejected && !writeBytes(path, "keep existing file")) return 3;
            const auto result = exportAndWait(files, model, path, type.id, true);
            if (rejected) {
                if (result.success || !result.error.contains("lattice") || readBytes(path) != "keep existing file") return 4;
                continue;
            }
            if (!result.success) {
                std::cerr << path.toStdString() << ": " << result.error.toStdString() << '\n';
                return 5;
            }
            expected["path"] = path;
            expected["format"] = type.id;
            expected["scenario"] = scenario;
            fixtures.append(expected);
        }
    }

    // Validation and I/O failures must leave existing files intact.
    const QString protectedPath = directory.filePath("protected.xyz");
    if (!writeBytes(protectedPath, "keep existing file")) return 6;
    model.setStructure(makeStructure(true, true));
    model.structure()->setPosition(0, std::numeric_limits<float>::quiet_NaN(), 0, 0);
    if (exportAndWait(files, model, protectedPath, ".xyz").success || readBytes(protectedPath) != "keep existing file") return 7;
    model.setStructure(makeStructure(true, true));
    model.structure()->lattice().matrix[2] = model.structure()->lattice().matrix[1];
    if (exportAndWait(files, model, protectedPath, "POSCAR").success || readBytes(protectedPath) != "keep existing file") return 8;
    model.setStructure(makeStructure(false, false));
    if (exportAndWait(files, model, directory.filePath("missing/failure.xyz"), ".xyz").success) return 9;
    if (exportAndWait(files, model, protectedPath, "unknown").success || readBytes(protectedPath) != "keep existing file") return 10;
    model.clear();
    if (exportAndWait(files, model, protectedPath, ".xyz").success || readBytes(protectedPath) != "keep existing file") return 11;

    // A valid export atomically replaces an existing destination.
    model.setStructure(makeStructure(false, false));
    if (!exportAndWait(files, model, protectedPath, ".xyz").success || readBytes(protectedPath) == "keep existing file") return 12;
    if (!writeBytes(directory.filePath("manifest.json"), QJsonDocument(fixtures).toJson())) return 13;
    std::cout << "Current-structure snapshots, deletion/replication, validation, and safe replacement passed\n";
    return 0;
}
