#include <QApplication>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#include <QDebug>

#include "core/Application.h"

int main(int argc, char* argv[])
{
    // Set up OpenGL surface format for macOS compatibility
    // Request OpenGL 4.1 Core Profile (the max supported on macOS)
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

    // Enable high DPI scaling
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    // Create Qt application (QApplication instead of QGuiApplication for QFileDialog support)
    QApplication app(argc, argv);

    // Set application metadata
    app.setApplicationName("ATOM-STUDIO");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("ATOM-STUDIO");
    app.setOrganizationDomain("atomstudio.app");

    qInfo() << "Starting ATOM-STUDIO v" << app.applicationVersion();
    qInfo() << "Qt version:" << qVersion();

    // Create and initialize the application
    atom::Application atomApp;

    if (!atomApp.initialize()) {
        qCritical() << "Failed to initialize application";
        return -1;
    }

    // Run the event loop
    return app.exec();
}
