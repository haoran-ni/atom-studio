#pragma once

#include <QObject>
#include <QQmlApplicationEngine>
#include <memory>

namespace atom {

/**
 * @brief Core application class managing lifecycle and QML engine
 *
 * The Application class serves as the central coordinator for ATOM-STUDIO.
 * It manages:
 * - QML engine initialization and configuration
 * - Application lifecycle events
 * - Future: coordination between UI and renderer
 */
class Application : public QObject {
    Q_OBJECT

public:
    explicit Application(QObject* parent = nullptr);
    ~Application() override;

    // Non-copyable, non-movable
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    /**
     * @brief Initialize the application
     * @return true if initialization succeeded, false otherwise
     */
    bool initialize();

    /**
     * @brief Get the QML engine instance
     * @return Pointer to the QML application engine
     */
    QQmlApplicationEngine* engine() const { return m_engine.get(); }

signals:
    /**
     * @brief Emitted when the application is fully initialized
     */
    void initialized();

    /**
     * @brief Emitted when the application is about to shut down
     */
    void aboutToQuit();

private:
    /**
     * @brief Set up QML context properties and types
     */
    void setupQmlContext();

    /**
     * @brief Register custom QML types
     */
    void registerQmlTypes();

    std::unique_ptr<QQmlApplicationEngine> m_engine;
};

} // namespace atom
