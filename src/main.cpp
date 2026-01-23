#include <QGuiApplication>
#include <QQuickStyle>
#include <QDebug>

#include "core/Application.h"

int main(int argc, char* argv[])
{
    // Enable high DPI scaling
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    // Create Qt application
    QGuiApplication app(argc, argv);

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
