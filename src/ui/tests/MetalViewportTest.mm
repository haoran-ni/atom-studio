#import <Metal/Metal.h>
#include "components/MetalViewport.h"
#include "components/StructureModel.h"
#include "components/PythonShellController.h"
#include <QTemporaryDir>
#include "common/Camera.h"
#include "Structure.h"
#include <QGuiApplication>
#include <QQuickWindow>
#include <QQuickItemGrabResult>
#include <QElapsedTimer>
#include <QThread>
#include <QImage>
#include <QTemporaryDir>
#include <iostream>
#include <functional>

namespace {
bool waitFor(const std::function<bool()>& ready, int limit = 10000) {
    QElapsedTimer timer; timer.start();
    do {
        QCoreApplication::processEvents();
        if (ready()) return true;
        QThread::msleep(1);
    } while (timer.elapsed() < limit);
    return false;
}
bool frame(atom::ui::MetalViewport& viewport) {
    const auto token = viewport.requestFrame();
    return waitFor([&] { return viewport.frameToken() >= token; });
}
bool visibleTransition(atom::ui::MetalViewport& viewport, QQuickWindow& window) {
    const auto token = viewport.requestFrame();
    QElapsedTimer timer;
    timer.start();
    do {
        // Capture throughout preparation and GPU submission, before waiting
        // for the replacement frame. Both scenes cover the center pixel.
        const auto image = window.grabWindow();
        if (image.isNull() || image.pixelColor(image.width()/2, image.height()/2) == QColor(50,100,150)) {
            std::cerr << "Structure switch displayed a blank viewport\n";
            return false;
        }
        QCoreApplication::processEvents();
        if (viewport.frameToken() >= token) return true;
        QThread::msleep(1);
    } while (timer.elapsed() < 10000);
    std::cerr << "Visible structure transition stalled\n";
    return false;
}
}
int main(int argc, char** argv) {
    @autoreleasepool {
        if (!MTLCreateSystemDefaultDevice()) return 77;
        QGuiApplication app(argc, argv);
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
        atom::ui::StructureModel model;
        QQuickWindow window;
        window.resize(320,320); window.setColor(QColor(200,40,20));
        auto* viewport = new atom::ui::MetalViewport(window.contentItem());
        viewport->setWidth(320); viewport->setHeight(320);
        viewport->setShowViewportAxes(false); viewport->setShowUnitCell(false);
        viewport->setShowBonds(false); viewport->setEnableAO(false); viewport->setEnableShadows(false);
        viewport->setMaxRTSamples(8); viewport->setIsPerspective(false); viewport->setOrthographicScale(3);
        viewport->setViewDirection(static_cast<int>(atom::render::ViewDirection::PlusZ));
        window.show();
        if (!waitFor([&] { return window.isExposed(); })) return 1;
        for (int mode : {0,1,0,1}) {
            viewport->setRendererMode(mode);
            viewport->setBackgroundColor(QColor(20,50,100,64));
            if (!frame(*viewport)) { std::cerr << "Viewport frame request stalled\n"; return 1; }
            const auto image = window.grabWindow();
            if (image.isNull()) return 1;
            const auto color = image.pixelColor(image.width()/2, image.height()/2);
            if (std::abs(color.red()-155) > 2 || std::abs(color.green()-43) > 2 || std::abs(color.blue()-40) > 2) {
                std::cerr << "Qt did not composite the native texture alpha: " << color.name().toStdString() << '\n'; return 1;
            }
            auto grab = viewport->grabToImage();
            if (!grab || !waitFor([&] { return !grab->image().isNull(); })) return 1;
            QTemporaryDir exportDirectory;
            const auto path = exportDirectory.filePath("transparent.png");
            if (!grab->image().save(path)) return 1;
            const QImage exported(path);
            const auto exportedColor = exported.pixelColor(exported.width()/2, exported.height()/2);
            if (std::abs(exportedColor.alpha()-64) > 1 || std::abs(exportedColor.red()-20) > 3) {
                std::cerr << "Viewport PNG export lost transparent background\n"; return 1;
            }
        }
        // Replace geometry faster than the background preparation can complete.
        // The final empty edit must supersede every in-progress nonempty scene.
        for (int i=0; i<10; ++i) {
            auto structure = std::make_shared<atom::data::Structure>();
            for (int a=0; a<2000; ++a) structure->addAtom((a%20)*.2f,((a/20)%20)*.2f,0,6);
            viewport->setEditedStructure(structure);
            QCoreApplication::processEvents();
            viewport->setRendererMode(i%2);
        }
        viewport->setEditedStructure(nullptr);
        viewport->setBackgroundColor(QColor(50,100,150));
        if (!frame(*viewport)) { std::cerr << "Latest geometry revision stalled\n"; return 1; }
        auto image = window.grabWindow();
        if (image.isNull() || image.pixelColor(image.width()/2,image.height()/2) != QColor(50,100,150)) {
            std::cerr << "An obsolete geometry revision was displayed\n"; return 1;
        }
        auto single = std::make_shared<atom::data::Structure>(); single->addAtom(0,0,0,6);
        viewport->setEditedStructure(single);
        viewport->setRendererMode(1);
        if (!frame(*viewport) || !waitFor([&] { return viewport->sampleCount() == 8; })) return 1;
        if (!frame(*viewport)) return 1;
        viewport->setRendererMode(0);
        if (!frame(*viewport)) return 1;
        bool lostSamples = false;
        const auto sampleConnection = QObject::connect(viewport, &atom::ui::MetalViewport::sampleCountChanged,
            &app, [&] { lostSamples |= viewport->sampleCount() < 8; });
        viewport->setRendererMode(1);
        if (!frame(*viewport) || viewport->sampleCount() != 8 || lostSamples) {
            std::cerr << "Renderer switch lost a converged scene\n"; return 1;
        }
        QObject::disconnect(sampleConnection);
        // Allow the final scene-graph frame to drain, then ensure convergence
        // does not sustain a display loop in a real QQuickWindow.
        waitFor([] { return false; }, 100);
        int swaps = 0;
        const auto swapConnection = QObject::connect(&window, &QQuickWindow::frameSwapped,
            &app, [&] { ++swaps; }, Qt::QueuedConnection);
        waitFor([] { return false; }, 200);
        QObject::disconnect(swapConnection);
        if (swaps > 1) { std::cerr << "Converged viewport continued rendering\n"; return 1; }
        window.resize(400,280); viewport->setWidth(400); viewport->setHeight(280);
        if (!frame(*viewport)) return 1;
        // Separate imports with identical geometry must still restart RT.
        // The active viewport is reused, including its exact camera matrices.
        for (int mode : {0, 1}) {
            viewport->setRendererMode(mode);
            auto carbon = std::make_shared<atom::data::Structure>();
            carbon->addAtom(0, 0, 0, 6);
            model.addStructure(carbon);
            if (!frame(*viewport)) return 1;
            if (mode == 1 && !waitFor([&] { return viewport->sampleCount() == 8; })) return 1;
            const auto camera = viewport->camera();
            model.addStructure(carbon);
            if (viewport->sampleCount() != 0) {
                std::cerr << "New import retained ray tracing samples\n"; return 1;
            }
            if (!visibleTransition(*viewport, window)) return 1;
            if (mode == 1 && !waitFor([&] { return viewport->sampleCount() == 8; })) return 1;
            model.setActiveIndex(model.activeIndex() - 1);
            if (viewport->sampleCount() != 0 || viewport->camera().viewMatrix() != camera.viewMatrix()
                    || viewport->camera().projectionMatrix() != camera.projectionMatrix()) {
                std::cerr << "Structure switch changed camera or retained samples\n"; return 1;
            }
            if (!visibleTransition(*viewport, window)) return 1;
            if (mode == 1 && !waitFor([&] { return viewport->sampleCount() == 8; })) return 1;
        }
        // Drive actual bundled Python previews through the model and Metal, in
        // both raster and RT. Presentation acknowledgements must allow a stream
        // to progress while the camera stays fixed and final RT converges.
        if (argc > 1) {
            QTemporaryDir environments;
            atom::ui::PythonShellController shell(&model, QString::fromLocal8Bit(argv[1]), nullptr, environments.path());
            QString pythonErrors;
            QObject::connect(&shell, &atom::ui::PythonShellController::output,
                [&](const QString& text, bool error) { if (error) pythonErrors += text; });
            bool finished = false, succeeded = false;
            QObject::connect(&shell, &atom::ui::PythonShellController::runFinished,
                [&](bool success) { finished = true; succeeded = success; });
            for (int mode : {0, 1}) {
                viewport->setRendererMode(mode);
                if (!frame(*viewport)) return 1;
                const auto camera = viewport->camera();
                int presentations = 0;
                const auto presentation = QObject::connect(viewport, &atom::ui::MetalViewport::frameTokenChanged,
                    [&] { ++presentations; });
                finished = false;
                shell.run(QString("import time\nfor step in range(8):\n    STRUCT_%1.positions[0, 0] = step * 0.04\n    studio.update(STRUCT_%1)\n    time.sleep(0.15)\n").arg(model.activeId()));
                if (!waitFor([&] { return finished; }, 20000) || !succeeded || presentations < 3) {
                    std::cerr << "Python to Metal stream stalled: " << pythonErrors.toStdString() << '\n';
                    return 1;
                }
                QObject::disconnect(presentation);
                if (!frame(*viewport) || model.liveFramePending() ||
                    std::abs(model.structure()->precisePosition(0)[0] - .28) > 1e-12 ||
                    viewport->camera().viewMatrix() != camera.viewMatrix() ||
                    viewport->camera().projectionMatrix() != camera.projectionMatrix()) {
                    std::cerr << "Live preview changed camera or lost final geometry\n"; return 1;
                }
                if (mode == 1 && !waitFor([&] { return viewport->sampleCount() == 8; })) return 1;
            }
            shell.shutdown();
        }
        // Switch faster than preparation/GPU completion; final empty document wins.
        auto empty = std::make_shared<atom::data::Structure>();
        model.addStructure(empty);
        for (int i = 0; i < 12; ++i) model.setActiveIndex(i % 2);
        model.setActiveIndex(model.structureCount() - 1);
        if (!frame(*viewport)) return 1;
        image = window.grabWindow();
        if (image.pixelColor(image.width()/2, image.height()/2) != QColor(50,100,150)) {
            std::cerr << "Old structure output survived an empty structure switch\n"; return 1;
        }
        std::cout << "Qt Metal viewport alpha, requests, rapid edits, mode/structure switches, and resize passed\n";
        return 0;
    }
}
