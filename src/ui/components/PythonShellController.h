#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QJsonObject>
#include <QMap>
#include <QPointer>
#include <QVariantList>

class QQmlEngine;
class QJSEngine;
namespace atom::ui {
class StructureModel;
class InteractiveShellWindow;
class InteractiveShellTutorialWindow;
class PythonEnvironmentManager;
class PythonPackagesWindow;

class PythonShellController : public QObject {
    Q_OBJECT
public:
    explicit PythonShellController(StructureModel* model, QString program = {}, QObject* parent = nullptr, QString environmentRoot = {});
    ~PythonShellController() override;
    static PythonShellController* create(QQmlEngine*, QJSEngine*);
    bool running() const { return m_running; }
    bool ready() const { return m_ready; }
    QString status() const { return m_status; }
    QVariantList structures() const;
    QString directory() const { return m_directory; }
    void setDirectory(const QString& directory);
    int fps() const { return m_fps; }
    void setFps(int fps) { m_fps = qBound(1, fps, 30); }
    Q_INVOKABLE void openWindow();
    Q_INVOKABLE void openTutorial();
    Q_INVOKABLE void openPackages();
    Q_INVOKABLE void openEnvironmentTerminal();
    PythonEnvironmentManager* environment() const { return m_environment; }
    void start();
    void run(const QString& code);
    void stop();
    void restart();
    void shutdown();
signals:
    void stateChanged();
    void structuresChanged();
    void output(const QString& text, bool error);
    void runFinished(bool success);
private:
    void send(QJsonObject message);
    void readOutput();
    void handle(const QJsonObject& message);
    void syncWorkspace();
    void dispatchRun();
    void flushFrames();
    void finishRun(bool success);
    void terminateWorker();
    void setStatus(const QString& status);
    void startPreparedWorker();
    PythonEnvironmentManager* m_environment;
    bool m_startRequested = false;
    QPointer<StructureModel> m_model;
    QProcess m_process;
    QTimer m_flushTimer;
    QTimer m_stopTimer;
    QByteArray m_readBuffer;
    QString m_program;
    QString m_directory;
    QString m_status = "Python session has not started";
    QString m_code;
    QMap<qint64, quint64> m_synced;
    QMap<qint64, quint64> m_expected;
    QMap<qint64, QJsonObject> m_frames;
    qint64 m_run = 0;
    int m_fps = 10;
    bool m_ready = false;
    bool m_running = false;
    bool m_stopping = false;
    bool m_finishing = false;
    bool m_success = false;
    bool m_runFailed = false;
    bool m_shuttingDown = false;
    QPointer<InteractiveShellWindow> m_window;
    QPointer<InteractiveShellTutorialWindow> m_tutorial;
    QPointer<PythonPackagesWindow> m_packages;
#ifdef Q_OS_WIN
    void* m_job = nullptr;
#endif
};
}
