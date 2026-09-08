#import <Metal/Metal.h>
#include "components/MetalViewport.h"
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
}
int main(int argc, char** argv) {
    @autoreleasepool {
        if (!MTLCreateSystemDefaultDevice()) return 77;
        QGuiApplication app(argc, argv);
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
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
        std::cout << "Qt Metal viewport alpha, requests, rapid edits, mode switches, and resize passed\n";
        return 0;
    }
}
