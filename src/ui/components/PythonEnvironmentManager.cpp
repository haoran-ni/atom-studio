#include "PythonEnvironmentManager.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#ifndef Q_OS_WIN
#include <signal.h>
#include <unistd.h>
#else
#define NOMINMAX
#include <windows.h>
#endif

namespace atom::ui {
PythonEnvironmentManager::PythonEnvironmentManager(const QString& appProgram, QObject* parent, QString root)
    : QObject(parent), m_environment(QProcessEnvironment::systemEnvironment()) {
    const auto appDirectory = QFileInfo(appProgram).absolutePath();
#ifdef Q_OS_MACOS
    m_bundle = QDir(appDirectory + "/../Resources/python").absolutePath();
#else
    m_bundle = appDirectory + "/python";
#endif
    m_root = root.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + "/python/" ATOM_PYTHON_VERSION "-" + QSysInfo::buildCpuArchitecture() : root;
    m_persistSelection = root.isEmpty();
    m_name = m_persistSelection ? QSettings().value("python/environment", "default").toString() : "default";
    if (!QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_-]{0,39}$").match(m_name).hasMatch()) m_name = "default";
    // Give the Python worker a consistent default credential/cache location.
    if (!m_environment.contains("HF_HOME")) {
        const QString cache = m_environment.value("XDG_CACHE_HOME", QDir::homePath() + "/.cache");
        m_environment.insert("HF_HOME", cache + "/huggingface");
    }
    m_process.setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
    connect(&m_process, &QProcess::started, this, [this] {
        if (m_job) CloseHandle(m_job);
        m_job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(m_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(m_process.processId()));
        if (process) { AssignProcessToJobObject(m_job, process); CloseHandle(process); }
    });
#endif
#ifdef Q_OS_UNIX
    m_process.setChildProcessModifier([] { ::setpgid(0, 0); });
#endif
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &PythonEnvironmentManager::readOutput);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            reportError(tr("Could not start bundled Python: %1").arg(m_process.errorString()));
            emit failed();
        }
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus status) {
#ifdef Q_OS_WIN
            if (m_job) { CloseHandle(m_job); m_job = nullptr; }
#endif
            readOutput();
            if (!m_buffer.isEmpty()) { emit output(QString::fromUtf8(m_buffer)); m_buffer.clear(); }
            const bool success = !m_cancelled && code == 0 && status == QProcess::NormalExit && m_resultReceived;
            m_status = success ? (m_action == "ensure" ? tr("Environment ready") : tr("Finished"))
                               : tr("Operation did not complete. Review the output and retry; installed changes may remain.");
            emit stateChanged();
            if (success && m_action == "ensure") emit prepared();
            if (!success) emit failed();
        });
}

PythonEnvironmentManager::~PythonEnvironmentManager() { cancel(); m_process.waitForFinished(1000); }
QString PythonEnvironmentManager::path() const { return m_root + '/' + m_name; }
QString PythonEnvironmentManager::python() const {
#ifdef Q_OS_WIN
    return path() + "/Scripts/python.exe";
#else
    return path() + "/bin/python";
#endif
}
QStringList PythonEnvironmentManager::environments() const {
    auto result = QDir(m_root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    if (!result.contains(m_name)) result.prepend(m_name);
    return result;
}
QProcessEnvironment PythonEnvironmentManager::processEnvironment() const {
    auto environment = m_environment;
    for (const auto& key : environment.keys())
        if (key.startsWith("PYTHON") || key == "__PYVENV_LAUNCHER__" || key == "VIRTUAL_ENV") environment.remove(key);
    environment.insert("VIRTUAL_ENV", path());
    environment.insert("PATH", QFileInfo(python()).absolutePath() + QDir::listSeparator() + environment.value("PATH"));
    return environment;
}
void PythonEnvironmentManager::reportError(const QString& text) {
    m_status = text; emit output(text + '\n'); emit stateChanged();
}
void PythonEnvironmentManager::select(const QString& name) {
    if (busy() || m_sessionRunning || name == m_name) return;
    if (!QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_-]{0,39}$").match(name).hasMatch()) {
        reportError(tr("Use 1–40 letters, digits, underscores or hyphens; start with a letter or digit.")); return;
    }
    emit aboutToModify();
    m_name = name; m_packages = {};
    if (m_persistSelection) QSettings().setValue("python/environment", name);
    emit environmentChanged(); emit stateChanged(); refresh();
}
void PythonEnvironmentManager::request(const QString& action, const QStringList& arguments) {
    if (busy()) { reportError(tr("Wait for the current environment operation to finish.")); return; }
    if (m_sessionRunning && action != "list") { reportError(tr("Stop the running script before changing its environment.")); return; }
    if (action == "install" || action == "uninstall") emit aboutToModify();
    m_action = action; m_resultReceived = false; m_cancelled = false; m_buffer.clear();
    m_status = tr("%1 — %2").arg(action, m_name);
    m_process.setProcessEnvironment(processEnvironment());
#ifdef Q_OS_WIN
    m_process.setProgram(m_bundle + "/bin/python3.exe");
#else
    m_process.setProgram(m_bundle + "/bin/python3");
#endif
    m_process.setArguments(QStringList{"-I", m_bundle + "/atom_studio/environment_tool.py", action, path()} + arguments);
    m_process.start(); emit stateChanged();
}
void PythonEnvironmentManager::ensure() { request("ensure"); }
void PythonEnvironmentManager::refresh() { request("list"); }
void PythonEnvironmentManager::check() { request("check"); }
void PythonEnvironmentManager::install(const QString& package, const QString& version) {
    const auto name = package.trimmed(); const auto ver = version.trimmed();
    if (!QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_.-]*(\\[[A-Za-z0-9_,.-]+\\])?$").match(name).hasMatch()
        || (!ver.isEmpty() && !QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_.+!-]*$").match(ver).hasMatch())) {
        reportError(tr("Enter a PyPI package name and an optional exact version. For other pip options, activate this environment in your terminal; see Tutorial.")); return;
    }
    request("install", {name + (ver.isEmpty() ? QString{} : "==" + ver)});
}
void PythonEnvironmentManager::uninstall(const QString& package) {
    if (!QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_.-]*$").match(package).hasMatch()) return;
    request("uninstall", {package});
}
void PythonEnvironmentManager::readOutput() {
    m_buffer += m_process.readAllStandardOutput();
    qsizetype end;
    while ((end = m_buffer.indexOf('\n')) >= 0) {
        const auto line = m_buffer.left(end); m_buffer.remove(0, end + 1);
        if (line.startsWith("@ATOM_ENV@")) {
            QJsonParseError error;
            const auto result = QJsonDocument::fromJson(line.mid(10), &error);
            if (error.error == QJsonParseError::NoError && result.isObject()) {
                m_resultReceived = true;
                if (m_action != "ensure") m_packages = result.object()["packages"].toArray();
            }
        } else emit output(QString::fromUtf8(line) + '\n');
    }
    if (m_buffer.size() > 65536) { emit output(QString::fromUtf8(m_buffer)); m_buffer.clear(); }
}
void PythonEnvironmentManager::cancel() {
    if (!busy()) return;
    m_cancelled = true;
#ifdef Q_OS_UNIX
    if (m_process.processId() > 0) ::kill(-m_process.processId(), SIGKILL);
#elif defined(Q_OS_WIN)
    if (m_job) TerminateJobObject(m_job, 1);
#endif
    m_process.kill();
}
}
