#include "Application.h"

#include <QCoreApplication>
#include <QQmlContext>
#include <QQuickStyle>
#include <QDebug>

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
    // Future: register custom QML types here
    // Example: qmlRegisterType<VulkanViewport>("AtomStudio", 1, 0, "VulkanViewport");
}

} // namespace atom
