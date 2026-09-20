#pragma once
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QJsonArray>

namespace atom::ui {
class PythonEnvironmentManager : public QObject {
    Q_OBJECT
public:
    explicit PythonEnvironmentManager(const QString& appProgram, QObject* parent = nullptr,
                                      QString environmentsRoot = {});
    ~PythonEnvironmentManager() override;
    bool busy() const { return m_process.state() != QProcess::NotRunning; }
    bool sessionRunning() const { return m_sessionRunning; }
    void setSessionRunning(bool value) { m_sessionRunning = value; emit stateChanged(); }
    QString name() const { return m_name; }
    QString path() const;
    QString python() const;
    QString workerScript() const { return m_bundle + "/atom_studio/worker_entry.py"; }
    QString status() const { return m_status; }
    QStringList environments() const;
    QJsonArray packages() const { return m_packages; }
    QProcessEnvironment processEnvironment() const;
    void select(const QString& name);
    void ensure();
    void refresh();
    void install(const QString& package, const QString& version = {});
    void uninstall(const QString& package);
    void check();
    void cancel();
signals:
    void stateChanged();
    void output(const QString& text);
    void prepared();
    void failed();
    void aboutToModify();
    void environmentChanged();
private:
    void request(const QString& action, const QStringList& arguments = {});
    void readOutput();
    void reportError(const QString& text);
    QString m_bundle, m_root, m_name, m_status, m_action;
    QProcess m_process;
    QByteArray m_buffer;
    QJsonArray m_packages;
    QProcessEnvironment m_environment;
    bool m_sessionRunning = false;
    bool m_resultReceived = false;
    bool m_cancelled = false;
    bool m_persistSelection = true;
#ifdef Q_OS_WIN
    void* m_job = nullptr;
#endif
};
}
