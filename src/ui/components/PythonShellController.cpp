#include "PythonShellController.h"
#include "InteractiveShellWindow.h"
#include "InteractiveShellTutorialWindow.h"
#include "PythonEnvironmentManager.h"
#include "PythonPackagesWindow.h"
#include "StructureModel.h"
#include "../../io/StructureSnapshot.h"
#include "../../data/Structure.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlEngine>
#include <QStandardPaths>
#ifndef Q_OS_WIN
#include <signal.h>
#else
#define NOMINMAX
#include <windows.h>
#endif

namespace atom::ui {
PythonShellController::PythonShellController(StructureModel* model, QString program, QObject* parent, QString environmentRoot)
    : QObject(parent), m_model(model),
      m_program(program.isEmpty() ? QCoreApplication::applicationFilePath() : std::move(program)),
      m_directory(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)) {
    if (!QDir(m_directory).exists()) m_directory = QDir::homePath();
    m_environment = new PythonEnvironmentManager(m_program, this, std::move(environmentRoot));
    connect(m_environment, &PythonEnvironmentManager::prepared, this, [this] {
        if (m_startRequested) { m_startRequested = false; startPreparedWorker(); }
    });
    connect(m_environment, &PythonEnvironmentManager::failed, this, [this] {
        if (m_startRequested) {
            m_startRequested = false;
            setStatus(m_environment->status());
            if (m_running) finishRun(false);
        }
    });
    connect(m_environment, &PythonEnvironmentManager::output, this, [this](const QString& text) { emit output(text, false); });
    connect(m_environment, &PythonEnvironmentManager::aboutToModify, this, [this] {
        terminateWorker();
        setStatus(tr("Environment changed. Run starts a fresh Python session; document geometry is retained."));
    });
    connect(this, &PythonShellController::stateChanged, this, [this] { m_environment->setSessionRunning(m_running); });
    m_flushTimer.setInterval(100);
#ifdef Q_OS_WIN
    connect(&m_process, &QProcess::started, this, [this] {
        if (m_job) CloseHandle(m_job);
        m_job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(m_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                                     static_cast<DWORD>(m_process.processId()));
        if (process) { AssignProcessToJobObject(m_job, process); CloseHandle(process); }
    });
#endif
    connect(&m_flushTimer, &QTimer::timeout, this, &PythonShellController::flushFrames);
    m_stopTimer.setSingleShot(true);
    m_stopTimer.setInterval(2000);
    connect(&m_stopTimer, &QTimer::timeout, this, [this] {
        emit output(tr("Calculation did not stop promptly. Restarting Python; last published geometry is retained.\n"), true);
        restart();
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &PythonShellController::readOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        emit output(QString::fromUtf8(m_process.readAllStandardError()), true);
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            setStatus(tr("Could not start Python: %1").arg(m_process.errorString()));
            emit output(m_status + '\n', true);
            if (m_running) finishRun(false);
        }
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
        readOutput();
        m_ready = false;
        m_synced.clear();
        m_stopTimer.stop();
        if (m_running) finishRun(false);
        if (!m_shuttingDown) setStatus(tr("Python exited. Run or Restart starts a new session."));
    });
    const auto changed = [this] {
        emit structuresChanged();
        if (m_ready && !m_running) syncWorkspace();
    };
    connect(model, &StructureModel::structuresChanged, this, changed);
    connect(model, &StructureModel::documentGeometryChanged, this, changed);
    connect(model, &StructureModel::activeStructureChanged, this, &PythonShellController::structuresChanged);
    connect(model, &QObject::destroyed, this, &PythonShellController::shutdown);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &PythonShellController::shutdown);
}

PythonShellController::~PythonShellController() { shutdown(); delete m_window; delete m_tutorial; delete m_packages; }

PythonShellController* PythonShellController::create(QQmlEngine* engine, QJSEngine*) {
    auto* model = StructureModel::instance();
    if (!model) model = StructureModel::create(engine, nullptr);
    return new PythonShellController(model, {}, engine);
}

void PythonShellController::setStatus(const QString& status) {
    m_status = status;
    emit stateChanged();
}

QVariantList PythonShellController::structures() const {
    QVariantList result;
    if (!m_model) return result;
    for (const auto& doc : m_model->documents())
        result.append(QVariantMap{{"id", doc->id}, {"name", QString("STRUCT_%1").arg(doc->id)},
            {"atoms", static_cast<qlonglong>(doc->current->atomCount())},
            {"file", QFileInfo(QString::fromStdString(doc->current->sourcePath())).fileName()},
            {"active", doc->id == m_model->activeId()}});
    return result;
}

void PythonShellController::setDirectory(const QString& directory) {
    if (!m_running && QDir(directory).exists()) {
        m_directory = QDir(directory).absolutePath();
        emit stateChanged();
    }
}

void PythonShellController::openWindow() {
    if (!m_window) m_window = new InteractiveShellWindow(this);
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
    start();
}

void PythonShellController::start() {
    if (m_process.state() != QProcess::NotRunning || m_startRequested) return;
    if (m_environment->busy()) {
        setStatus(tr("Wait for the environment operation to finish, then Run again."));
        if (m_running) finishRun(false);
        return;
    }
    m_startRequested = true;
    // A Run request can prepare its environment before the worker exists.
    m_environment->setSessionRunning(false);
    m_environment->ensure();
    setStatus(tr("Preparing Python environment…"));
}

void PythonShellController::startPreparedWorker() {
    m_shuttingDown = false;
    m_ready = false;
    m_synced.clear();
    m_readBuffer.clear();
    m_process.setProgram(m_environment->python());
    m_process.setArguments({"-I", "-u", m_environment->workerScript()});
    m_process.setProcessEnvironment(m_environment->processEnvironment());
    m_process.setWorkingDirectory(m_directory);
    m_process.start();
    setStatus(tr("Starting Python — %1…").arg(m_environment->name()));
}

void PythonShellController::openPackages() {
    if (!m_packages) m_packages = new PythonPackagesWindow(this);
    m_packages->show(); m_packages->raise(); m_packages->activateWindow();
    if (!m_environment->busy()) m_environment->refresh();
}

void PythonShellController::openEnvironmentTerminal() {
    if (!m_packages) m_packages = new PythonPackagesWindow(this);
    m_packages->show(); m_packages->raise(); m_packages->activateWindow();
    m_environment->openTerminal(m_directory);
}

void PythonShellController::openTutorial() {
    if (!m_tutorial) m_tutorial = new InteractiveShellTutorialWindow;
    m_tutorial->show();
    m_tutorial->raise();
    m_tutorial->activateWindow();
}

void PythonShellController::send(QJsonObject message) {
    if (m_process.state() == QProcess::Running)
        m_process.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
}

void PythonShellController::syncWorkspace() {
    if (!m_model) return;
    QJsonArray documents, ids;
    for (const auto& doc : m_model->documents()) {
        ids.append(doc->id);
        if (m_synced.contains(doc->id) && m_synced[doc->id] == doc->revision) continue;
        documents.append(QJsonObject{{"id", doc->id}, {"current", io::structureSnapshot(*doc->current)},
                                     {"original", io::structureSnapshot(*doc->raw)}});
        m_synced[doc->id] = doc->revision;
    }
    send({{"type", "sync"}, {"documents", documents}, {"ids", ids}});
}

void PythonShellController::run(const QString& code) {
    if (!m_model || m_running || code.trimmed().isEmpty()) return;
    if (m_model->switchingLocked()) {
        emit output(tr("Wait for image export to finish before running code.\n"), true);
        return;
    }
    m_code = code;
    m_running = true;
    m_finishing = m_stopping = false;
    m_runFailed = false;
    m_model->setEditsLocked(true);
    m_frames.clear();
    ++m_run;
    if (m_ready) dispatchRun();
    else start();
    emit stateChanged();
}

void PythonShellController::dispatchRun() {
    syncWorkspace();
    m_expected.clear();
    for (const auto& doc : m_model->documents()) m_expected[doc->id] = doc->revision;
    send({{"type", "run"}, {"run", m_run}, {"code", m_code},
          {"directory", m_directory}, {"fps", m_fps}});
    m_flushTimer.setInterval(1000 / m_fps);
    setStatus(tr("Running…"));
}

void PythonShellController::readOutput() {
    m_readBuffer += m_process.readAllStandardOutput();
    qsizetype end;
    while ((end = m_readBuffer.indexOf('\n')) >= 0) {
        const auto line = m_readBuffer.left(end);
        m_readBuffer.remove(0, end + 1);
        constexpr auto prefix = "@ATOM_SHELL@";
        if (!line.startsWith(prefix)) {
            if (m_ready) emit output(QString::fromUtf8(line) + '\n', false);
            continue;
        }
        QJsonParseError error;
        auto doc = QJsonDocument::fromJson(line.mid(qstrlen(prefix)), &error);
        if (error.error == QJsonParseError::NoError && doc.isObject()) handle(doc.object());
        else emit output(tr("Invalid Python response: %1\n").arg(error.errorString()), true);
    }
}

void PythonShellController::handle(const QJsonObject& message) {
    if (!m_model) return;
    const auto type = message["type"].toString();
    if (type == "ready") {
        m_ready = true;
        emit output(tr("Python %1 · ASE %2\nUse STRUCT_N or studio.add(atoms). Imports and variables persist between runs.\n")
                        .arg(message["python"].toString(), message["ase"].toString()), false);
        if (m_stopping) finishRun(false);
        else if (m_running) dispatchRun();
        else { syncWorkspace(); setStatus(tr("Ready")); }
        return;
    }
    if (type == "output" || type == "sync_error") {
        emit output(message["text"].toString(), type == "sync_error" || message["channel"] == "stderr");
        if (type == "sync_error") { m_synced.clear(); stop(); }
        return;
    }
    if (!m_running || message["run"].toInteger() != m_run) return;
    if (type == "frame") {
        if (m_expected.contains(message["id"].toInteger())) m_frames[message["id"].toInteger()] = message;
        if (!m_flushTimer.isActive()) m_flushTimer.start();
        if (message["final"].toBool()) flushFrames();
    } else if (type == "done") {
        m_finishing = true;
        m_success = message["success"].toBool() && !m_runFailed;
        m_stopTimer.stop();
        flushFrames();
    } else if (type == "add" || type == "show") {
        QJsonObject response{{"type", "response"}, {"request", message["request"]}};
        try {
            if (type == "add") {
                const auto structure = io::structureFromSnapshot(message["snapshot"].toObject());
                const qint64 id = m_model->addStructure(structure, false);
                m_expected[id] = m_model->document(id)->revision;
                m_synced[id] = m_expected[id];
                response["id"] = id;
            } else {
                const qint64 id = message["id"].toInteger();
                if (!m_model->document(id)) throw std::runtime_error("Structure no longer exists");
                for (int i = 0; i < m_model->structureCount(); ++i)
                    if (m_model->documents()[i]->id == id) m_model->setActiveIndex(i);
            }
        } catch (const std::exception& error) { response["error"] = QString::fromUtf8(error.what()); }
        send(response);
    }
}

void PythonShellController::flushFrames() {
    if (!m_model || m_model->switchingLocked()) return;
    for (auto it = m_frames.begin(); it != m_frames.end();) {
        const auto id = it.key();
        const auto message = it.value();
        if (!m_finishing && !message["final"].toBool() && id == m_model->activeId() && m_model->liveFramePending()) {
            ++it;
            continue;
        }
        try {
            auto structure = io::structureFromSnapshot(message["snapshot"].toObject());
            if (!m_model->applyShellStructure(id, m_expected.value(id), std::move(structure)))
                throw std::runtime_error("Structure changed since this run started; stale update rejected");
            m_expected[id] = m_model->document(id)->revision;
            m_synced[id] = m_expected[id];
        } catch (const std::exception& error) {
            emit output(QString::fromUtf8(error.what()) + '\n', true);
            m_runFailed = true;
            m_success = false;
        }
        send({{"type", "ack"}, {"id", id}});
        it = m_frames.erase(it);
    }
    if (m_finishing && m_frames.isEmpty()) finishRun(m_success);
    if (m_frames.isEmpty()) m_flushTimer.stop();
}

void PythonShellController::finishRun(bool success) {
    m_stopTimer.stop();
    m_frames.clear();
    m_flushTimer.stop();
    // Rejected/unpublished Python edits do not change native document revisions.
    // Force a restore from the last accepted geometry after failure or Stop, and
    // queue it before signals can start another run. _sync preserves ASE aliases
    // and calculators while replacing the registered objects' structure data.
    if (!success) m_synced.clear();
    if (m_ready) syncWorkspace();
    m_running = m_finishing = m_stopping = false;
    if (m_model) m_model->setEditsLocked(false);
    setStatus(success ? tr("Finished") : tr("Stopped or failed — unpublished structure edits discarded"));
    emit runFinished(success);
}

void PythonShellController::stop() {
    if (!m_running || m_stopping) return;
    m_stopping = true;
    send({{"type", "stop"}});
#ifndef Q_OS_WIN
    if (m_ready && m_process.processId() > 0) ::kill(-m_process.processId(), SIGINT);
#endif
    m_stopTimer.start();
    setStatus(tr("Stopping…"));
}

void PythonShellController::terminateWorker() {
    m_startRequested = false;
    m_stopTimer.stop();
    if (m_process.state() != QProcess::NotRunning) {
#ifndef Q_OS_WIN
        if (m_process.processId() > 0) ::kill(-m_process.processId(), SIGKILL);
#endif
        m_process.kill();
        m_process.waitForFinished(1000);
    }
    m_ready = false;
    m_synced.clear();
#ifdef Q_OS_WIN
    if (m_job) { CloseHandle(m_job); m_job = nullptr; }
#endif
}

void PythonShellController::restart() {
    terminateWorker();
    if (m_running) finishRun(false);
    emit output(tr("\nPython session restarted. Variables and calculators were cleared; document geometry was retained.\n"), false);
    start();
}

void PythonShellController::shutdown() {
    m_shuttingDown = true;
    m_flushTimer.stop();
    terminateWorker();
    if (m_window) m_window->hide();
    if (m_tutorial) m_tutorial->hide();
    if (m_packages) m_packages->hide();
    m_environment->cancel();
}
}
