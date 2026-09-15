#include "PythonEnvironmentManager.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>
#include <utility>
#ifndef Q_OS_WIN
#include <signal.h>
#include <unistd.h>
#else
#define NOMINMAX
#include <windows.h>
#endif

namespace atom::ui {
namespace {
QString quote(QString value) { return "'" + value.replace("'", "'\\''") + "'"; }
void writeFile(const QString& path, const QString& text, bool executable = false) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(text.toUtf8()) < 0 || !file.commit())
        throw std::runtime_error(QString("Cannot write %1: %2").arg(path, file.errorString()).toStdString());
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | (executable ? QFile::ExeOwner : QFile::Permissions{}));
}
}

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
    // Keep credential locations identical even if the terminal's startup files
    // use a different HF_HOME. Never write HF_TOKEN into generated scripts.
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
            m_terminalDirectory.clear(); emit failed();
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
            const auto directory = std::exchange(m_terminalDirectory, {});
            if (success && !directory.isEmpty()) terminalReady(directory);
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
        reportError(tr("Enter a PyPI package name and an optional exact version. Use the environment terminal for other pip options.")); return;
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
QString PythonEnvironmentManager::terminalPreference() const { return QSettings().value("python/terminal").toString(); }
void PythonEnvironmentManager::setTerminalPreference(const QString& value) { QSettings().setValue("python/terminal", value); }
void PythonEnvironmentManager::openTerminal(const QString& directory) {
    if (busy() || m_sessionRunning) { reportError(tr("Wait until Python and package operations are idle before opening the environment terminal.")); return; }
    m_terminalDirectory = directory.isEmpty() ? QDir::homePath() : directory;
    ensure();
}
void PythonEnvironmentManager::terminalReady(const QString& directory) {
    try {
        const auto launchDirectory = path() + "/.terminal";
#ifdef Q_OS_WIN
        const auto script = launchDirectory + "/activate.cmd";
        const auto batchValue = [](QString value) {
            if (value.contains('"') || value.contains('\r') || value.contains('\n')) throw std::runtime_error("Invalid terminal setting");
            return value.replace("%", "%%");
        };
        QString commands = "@echo off\r\ncall \"" + batchValue(QDir::toNativeSeparators(path() + "/Scripts/activate.bat"))
            + "\"\r\ncd /d \"" + batchValue(QDir::toNativeSeparators(directory)) + "\"\r\n";
        for (const auto& key : {QString("HF_HOME"), QString("HF_TOKEN_PATH"), QString("HF_HUB_CACHE")})
            if (m_environment.contains(key)) commands += "set \"" + key + '=' + batchValue(m_environment.value(key)) + "\"\r\n";
        commands += "set PYTHONHOME=\r\nset PYTHONPATH=\r\n";
        writeFile(script, commands);
        if (!QProcess::startDetached("cmd.exe", {"/V:OFF", "/K", script})) throw std::runtime_error("Cannot open Windows terminal");
#else
        QString activate = ". " + quote(path() + "/bin/activate") + "\n";
        for (const auto& key : {QString("HF_HOME"), QString("HF_TOKEN_PATH"), QString("HF_HUB_CACHE")})
            if (m_environment.contains(key)) activate += "export " + key + '=' + quote(m_environment.value(key)) + "\n";
        activate += "unset PYTHONHOME PYTHONPATH __PYVENV_LAUNCHER__\ncd " + quote(directory)
            + "\nprintf '%s\\n' " + quote("ATOM-STUDIO environment: " + path())
            + " " + quote("Install packages: python -m pip install PACKAGE")
            + " " + quote("Hugging Face login: hf auth login")
            + " " + quote("Restart the interactive Python shell after package changes.") + "\n";
        QString shell = m_environment.value("SHELL", "/bin/bash");
        QString command;
        if (QFileInfo(shell).fileName() == "zsh") {
            const QString original = m_environment.value("ZDOTDIR", QDir::homePath());
            writeFile(launchDirectory + "/.zshrc", "export ZDOTDIR=" + quote(original)
                + "\n[[ -f \"$ZDOTDIR/.zshrc\" ]] && source \"$ZDOTDIR/.zshrc\"\n" + activate);
            command = "export ZDOTDIR=" + quote(launchDirectory) + "\nexec " + quote(shell) + " -i\n";
        } else if (QFileInfo(shell).fileName() == "fish") {
            QString fish = "source " + quote(path() + "/bin/activate.fish") + "; cd " + quote(directory);
            for (const auto& key : {QString("HF_HOME"), QString("HF_TOKEN_PATH"), QString("HF_HUB_CACHE")})
                if (m_environment.contains(key)) fish += "; set -gx " + key + ' ' + quote(m_environment.value(key));
            command = "exec " + quote(shell) + " -i -C " + quote(fish) + "\n";
        } else {
            writeFile(launchDirectory + "/bashrc", "[ -f ~/.bashrc ] && . ~/.bashrc\n" + activate);
            command = "exec /bin/bash --rcfile " + quote(launchDirectory + "/bashrc") + " -i\n";
        }
        const auto script = launchDirectory + "/ATOM-STUDIO.command";
        writeFile(script, "#!/bin/sh\n" + command, true);
#ifdef Q_OS_MACOS
        const auto preference = terminalPreference();
        if (preference.isEmpty()) {
            if (!QDesktopServices::openUrl(QUrl::fromLocalFile(script))) throw std::runtime_error("Cannot open the default terminal. Choose a terminal application in Manage Packages.");
        } else if (!QProcess::startDetached("/usr/bin/open", {"-a", preference, script}))
            throw std::runtime_error("Cannot open the selected terminal");
#else
        QString terminal = terminalPreference();
        if (terminal.isEmpty())
            for (const auto& candidate : {"x-terminal-emulator", "gnome-terminal", "konsole", "xterm"}) {
                terminal = QStandardPaths::findExecutable(candidate); if (!terminal.isEmpty()) break;
            }
        if (terminal.isEmpty()) throw std::runtime_error("Choose a terminal executable in Manage Packages.");
        const auto flag = QFileInfo(terminal).fileName() == "gnome-terminal" ? "--" : "-e";
        if (!QProcess::startDetached(terminal, {flag, script})) throw std::runtime_error("Cannot open the selected terminal");
#endif
#endif
        m_status = tr("Opened external terminal for %1").arg(m_name); emit stateChanged();
    } catch (const std::exception& error) { reportError(QString::fromUtf8(error.what())); }
}
}
