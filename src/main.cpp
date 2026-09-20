#include <QApplication>
#include <QIcon>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#include <QDebug>

#include "core/Application.h"
#include "python/PythonRuntime.h"
#include <cstdlib>
#include <cstdio>
#include <string>
#ifndef _WIN32
#include <unistd.h>
#endif

int main(int argc, char* argv[])
{
    const bool shellWorker = argc == 2 && std::string(argv[1]) == "--python-shell-worker";
#ifndef _WIN32
    if (shellWorker && setpgid(0, 0) != 0) {
        std::perror("Could not isolate Python worker process group");
        return 1;
    }
#endif
    // Initialize Python interpreter BEFORE Qt
    // This ensures Python is ready for ASE file reading
    if (!atom::python::PythonRuntime::instance().initialize()) {
        qCritical() << "Failed to initialize Python runtime";
        return -1;
    }
    if (shellWorker) {
        const int result = atom::python::runInteractiveShellWorker();
        // The worker owns daemon I/O and user-created threads. Process exit is
        // intentional; never finalize Python underneath a running thread.
        std::_Exit(result);
    }

#ifdef Q_OS_MACOS
    // Use Metal as the primary graphics API on macOS
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
    qInfo() << "Using Metal graphics API";
#else
    // Set up OpenGL surface format for other platforms
    // Request OpenGL 4.1 Core Profile
    QSurfaceFormat format;
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    // Force OpenGL as the graphics API (required for QQuickFramebufferObject)
    // Must be called before QApplication is created
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
#endif

    // Enable high DPI scaling
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    // Prevent Qt from importing the OS dark/light mode palette so the UI
    // always renders with the Fusion light theme regardless of system appearance.
    // Must be called before QApplication is constructed.
    QApplication::setDesktopSettingsAware(false);

    // Create Qt application (QApplication instead of QGuiApplication for QFileDialog support)
    QApplication app(argc, argv);

    // Set application metadata
    app.setApplicationName("ATOM-STUDIO");
    app.setApplicationVersion(ATOM_STUDIO_VERSION);
    app.setOrganizationName("ATOM-STUDIO");
    app.setOrganizationDomain("atomstudio.app");
    app.setWindowIcon(QIcon(":/branding/app-logo.png"));

    qInfo() << "Starting ATOM-STUDIO v" << app.applicationVersion();
    qInfo() << "Qt version:" << qVersion();

    int result = -1;
    {
        // Destroy controllers and join file-I/O workers before finalizing Python.
        atom::Application atomApp;
        if (atomApp.initialize()) result = app.exec();
        else qCritical() << "Failed to initialize application";
    }

    // Finalize Python interpreter after Qt event loop ends
    atom::python::PythonRuntime::instance().finalize();

    return result;
}
