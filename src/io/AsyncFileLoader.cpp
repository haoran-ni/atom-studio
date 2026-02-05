#include "AsyncFileLoader.h"
#include "FileReaderRegistry.h"
#include "../data/Structure.h"

#include <QDebug>
#include <atomic>

namespace atom::io {

/**
 * @brief Worker object that runs in the background thread
 */
class AsyncFileLoader::LoadWorker : public QObject {
    Q_OBJECT

public:
    explicit LoadWorker(QObject* parent = nullptr) : QObject(parent) {}

    std::atomic<bool> cancelFlag{false};

public slots:
    void doLoad(const QString& filePath) {
        cancelFlag = false;

        auto progressCallback = [this](float progress, std::string_view message) -> bool {
            if (cancelFlag.load()) {
                return false; // Cancel
            }
            emit progressChanged(progress, QString::fromStdString(std::string(message)));
            return true;
        };

        auto result = FileReaderRegistry::instance().readFile(
            filePath.toStdString(), progressCallback);

        if (cancelFlag.load()) {
            emit cancelled();
        } else if (result.success) {
            emit finished(std::shared_ptr<data::Structure>(
                std::move(result.structure)));
        } else {
            emit failed(QString::fromStdString(result.errorMessage));
        }
    }

signals:
    void progressChanged(float progress, const QString& message);
    void finished(std::shared_ptr<atom::data::Structure> structure);
    void failed(const QString& error);
    void cancelled();
};

AsyncFileLoader::AsyncFileLoader(QObject* parent)
    : QObject(parent)
{
}

AsyncFileLoader::~AsyncFileLoader() {
    cancel();
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
    }
}

void AsyncFileLoader::loadFile(const QString& filePath) {
    if (m_loading) {
        qWarning() << "AsyncFileLoader: Already loading a file";
        return;
    }

    m_loading = true;
    m_cancelRequested = false;
    m_progress = 0.0f;
    m_statusMessage = "Starting...";

    emit loadingStarted(filePath);

    // Create worker thread
    m_workerThread = new QThread(this);
    m_worker = new LoadWorker();
    m_worker->moveToThread(m_workerThread);

    // Connect signals
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &AsyncFileLoader::loadingStarted, m_worker, &LoadWorker::doLoad);
    connect(m_worker, &LoadWorker::progressChanged,
            this, &AsyncFileLoader::onWorkerProgress);
    connect(m_worker, &LoadWorker::finished,
            this, &AsyncFileLoader::onWorkerFinished);
    connect(m_worker, &LoadWorker::failed,
            this, &AsyncFileLoader::onWorkerFailed);
    connect(m_worker, &LoadWorker::cancelled,
            this, &AsyncFileLoader::onWorkerCancelled);

    m_workerThread->start();
    QMetaObject::invokeMethod(m_worker, "doLoad", Qt::QueuedConnection,
                              Q_ARG(QString, filePath));
}

void AsyncFileLoader::cancel() {
    if (!m_loading) return;

    m_cancelRequested = true;
    if (m_worker) {
        m_worker->cancelFlag = true;
    }
}

void AsyncFileLoader::onWorkerProgress(float progress, const QString& message) {
    m_progress = progress;
    m_statusMessage = message;
    emit progressChanged(progress, message);
}

void AsyncFileLoader::onWorkerFinished(std::shared_ptr<data::Structure> structure) {
    m_loading = false;
    m_progress = 1.0f;
    m_statusMessage = "Complete";

    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
        m_worker = nullptr;
    }

    emit loadingFinished(structure);
}

void AsyncFileLoader::onWorkerFailed(const QString& error) {
    m_loading = false;
    m_statusMessage = error;

    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
        m_worker = nullptr;
    }

    emit loadingFailed(error);
}

void AsyncFileLoader::onWorkerCancelled() {
    m_loading = false;
    m_statusMessage = "Cancelled";

    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
        m_worker = nullptr;
    }

    emit loadingCancelled();
}

} // namespace atom::io

#include "AsyncFileLoader.moc"
