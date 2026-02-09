#include "Application.h"

#include <QCoreApplication>
#include <QQmlContext>
#include <QQuickStyle>
#include <QDebug>

// UI components
#include "../ui/components/OpenGLViewport.h"
#ifdef ATOM_HAS_METAL
#include "../ui/components/MetalViewport.h"
#endif
#include "../ui/components/FileController.h"
#include "../ui/components/StructureModel.h"

namespace atom {

Application::Application(QObject* parent)
    : QObject(parent)
    , m_engine(std::make_unique<QQmlApplicationEngine>())
{
}

Application::~Application() = default;

bool Application::initialize()
{
    // Set the Quick Controls style
    QQuickStyle::setStyle("Fusion");

    // Register custom QML types
    registerQmlTypes();

    // Set up QML context
    setupQmlContext();

    // Load the main QML file
    const QUrl url(QStringLiteral("qrc:/qml/Main.qml"));

    // Connect to check for loading errors
    QObject::connect(
        m_engine.get(),
        &QQmlApplicationEngine::objectCreationFailed,
        this,
        []() {
            qCritical() << "Failed to create QML objects";
            QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection
    );

    m_engine->load(url);

    if (m_engine->rootObjects().isEmpty()) {
        qCritical() << "No root objects loaded from QML";
        return false;
    }

    qInfo() << "ATOM-STUDIO initialized successfully";
    emit initialized();
    return true;
}

void Application::setupQmlContext()
{
    QQmlContext* context = m_engine->rootContext();

    // Expose application version to QML
    context->setContextProperty("appVersion", QStringLiteral("0.1.0"));
    context->setContextProperty("appName", QStringLiteral("ATOM-STUDIO"));

    // Future: expose more application state/controllers here
}

void Application::registerQmlTypes()
{
    // Register UI components
    qmlRegisterType<ui::OpenGLViewport>("AtomStudio", 1, 0, "OpenGLViewport");
#ifdef ATOM_HAS_METAL
    qmlRegisterType<ui::MetalViewport>("AtomStudio", 1, 0, "MetalViewport");
#endif
    qmlRegisterSingletonType<ui::FileController>("AtomStudio", 1, 0, "FileController",
        ui::FileController::create);
    qmlRegisterSingletonType<ui::StructureModel>("AtomStudio", 1, 0, "StructureModel",
        ui::StructureModel::create);
}

} // namespace atom
