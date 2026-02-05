#pragma once

#include <QObject>
#include <QThread>
#include <QString>
#include <memory>

namespace atom::data {
class Structure;
}

namespace atom::io {

/**
 * @brief Asynchronous file loader using Qt threads
 *
 * Loads atomic structure files in a background thread,
 * emitting signals for progress and completion.
 */
class AsyncFileLoader : public QObject {
    Q_OBJECT

public:
    explicit AsyncFileLoader(QObject* parent = nullptr);
    ~AsyncFileLoader() override;

    /**
     * @brief Check if currently loading
     */
    bool isLoading() const { return m_loading; }

    /**
     * @brief Get current progress (0.0 to 1.0)
     */
    float progress() const { return m_progress; }

    /**
     * @brief Get current status message
     */
    QString statusMessage() const { return m_statusMessage; }

public slots:
    /**
     * @brief Start loading a file asynchronously
     * @param filePath Path to the file to load
     */
    void loadFile(const QString& filePath);

    /**
     * @brief Cancel current loading operation
     */
    void cancel();

signals:
    /**
     * @brief Emitted when loading starts
     */
    void loadingStarted(const QString& filePath);

    /**
     * @brief Emitted periodically during loading
     * @param progress Value from 0.0 to 1.0
     * @param message Status message
     */
    void progressChanged(float progress, const QString& message);

    /**
     * @brief Emitted when loading completes successfully
     * @param structure Loaded structure (ownership transferred)
     */
    void loadingFinished(std::shared_ptr<atom::data::Structure> structure);

    /**
     * @brief Emitted when loading fails
     * @param error Error message
     */
    void loadingFailed(const QString& error);

    /**
     * @brief Emitted when loading is cancelled
     */
    void loadingCancelled();

private:
    class LoadWorker;

    void onWorkerProgress(float progress, const QString& message);
    void onWorkerFinished(std::shared_ptr<atom::data::Structure> structure);
    void onWorkerFailed(const QString& error);
    void onWorkerCancelled();

    QThread* m_workerThread = nullptr;
    LoadWorker* m_worker = nullptr;
    bool m_loading = false;
    bool m_cancelRequested = false;
    float m_progress = 0.0f;
    QString m_statusMessage;
};

} // namespace atom::io
