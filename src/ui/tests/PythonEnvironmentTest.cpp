#include "components/PythonShellController.h"
#include "components/PythonEnvironmentManager.h"
#include "components/StructureModel.h"
#include "Structure.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QThread>
#include <QUrl>
#include <iostream>
#include <stdexcept>

using namespace atom::ui;
static void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
static bool waitFor(const std::function<bool()>& condition) {
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < 30000) { QCoreApplication::processEvents(); QThread::msleep(5); }
    return condition();
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 4) return 1;
    qputenv("PIP_NO_INDEX", "1"); qputenv("PIP_FIND_LINKS", QUrl::fromLocalFile(QString::fromLocal8Bit(argv[3])).toEncoded());
    try {
        StructureModel model;
        auto structure = std::make_shared<atom::data::Structure>(); structure->addAtom(0, 0, 0, 29);
        model.addStructure(structure);
        PythonShellController shell(&model, QString::fromLocal8Bit(argv[1]), nullptr, QString::fromLocal8Bit(argv[2]));
        auto* environment = shell.environment();
        QString output; QObject::connect(&shell, &PythonShellController::output, [&](const QString& text, bool) { output += text; std::cout << text.toStdString(); });
        int runs = 0; bool success = false;
        QObject::connect(&shell, &PythonShellController::runFinished, [&](bool ok) { ++runs; success = ok; });
        const auto run = [&](const QString& code) {
            const int before = runs; shell.run(code);
            require(waitFor([&] { return runs > before; }) && success, "Worker run failed");
        };
        run("keep = 17");
        shell.openPackages();
        require(waitFor([&] { return !environment->busy(); }), "Package listing timed out");
        require(!environment->packages().isEmpty(), "Bundled packages not listed");
        run("assert keep == 17");
        QWidget* window = nullptr;
        for (auto* widget : QApplication::topLevelWidgets()) if (widget->objectName() == "pythonPackagesWindow") window = widget;
        require(window, "Package window missing");
        auto* name = window->findChild<QLineEdit*>("pythonPackageName");
        name->setText("atom-env-fixture");
        window->findChild<QPushButton*>("pythonPackageInstall")->click();
        require(waitFor([&] { return !environment->busy(); }), "Package install timed out");
        require(environment->status() == "Finished", "Package install failed");
        run("assert 'keep' not in globals()\nimport atom_env_fixture\nSTRUCT_0.positions[0, 0] = atom_env_fixture.VALUE");
        require(model.structure()->precisePosition(0)[0] == 42, "Installed package did not modify loaded structure");
        environment->setSessionRunning(true);
        environment->install("atom-env-fixture");
        require(!environment->busy(), "Install accepted during a calculation");
        environment->setSessionRunning(false);
        environment->install("--bad-option");
        require(!environment->busy(), "Invalid requirement accepted");
        environment->install("atom-studio-missing-test-package");
        require(waitFor([&] { return !environment->busy(); }), "Failed install hung");
        require(environment->status().startsWith("Operation did not complete"), "Install failure not reported");
        environment->refresh(); environment->cancel();
        require(waitFor([&] { return !environment->busy(); }), "Cancel hung");
        environment->refresh();
        require(waitFor([&] { return !environment->busy(); }), "Environment did not recover after cancel");
        environment->uninstall("atom-env-fixture");
        require(waitFor([&] { return !environment->busy(); }) && environment->status() == "Finished", "Uninstall failed");
        run("import importlib.util\nassert importlib.util.find_spec('atom_env_fixture') is None\nassert STRUCT_0.positions[0, 0] == 42");
        environment->select("second");
        require(waitFor([&] { return !environment->busy(); }), "Environment switch timed out");
        run("assert STRUCT_0.positions[0, 0] == 42");
        shell.shutdown();
        std::cout << "PASS: package UI install, live document edit, restart, selection, operation guards, failure, cancellation, uninstall\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
