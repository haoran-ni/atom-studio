#include <QApplication>
#include <QIcon>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#include <QDebug>

#include "core/Application.h"
#include "python/PythonRuntime.h"

int main(int argc, char* argv[])
{
    // Initialize Python interpreter BEFORE Qt
    // This ensures Python is ready for ASE file reading
    if (!atom::python::PythonRuntime::instance().initialize()) {
        qCritical() << "Failed to initialize Python runtime";
        return -1;
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
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("ATOM-STUDIO");
    app.setOrganizationDomain("atomstudio.app");
    app.setWindowIcon(QIcon(":/branding/app-logo.png"));

    qInfo() << "Starting ATOM-STUDIO v" << app.applicationVersion();
    qInfo() << "Qt version:" << qVersion();

    // Create and initialize the application
    atom::Application atomApp;

    if (!atomApp.initialize()) {
        qCritical() << "Failed to initialize application";
        atom::python::PythonRuntime::instance().finalize();
        return -1;
    }

    // Run the event loop
    int result = app.exec();

    // Finalize Python interpreter after Qt event loop ends
    atom::python::PythonRuntime::instance().finalize();

    return result;
}
