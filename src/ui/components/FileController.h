#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include "../../io/AsyncFileLoader.h"

namespace atom::data {
class AtomicStructure;
}

namespace atom::ui {

/**
 * @brief QML singleton for file operations
 *
 * Provides file dialog integration and async file loading.
 */
class FileController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)
    Q_PROPERTY(float loadProgress READ loadProgress NOTIFY loadProgressChanged)
    Q_PROPERTY(QString loadStatus READ loadStatus NOTIFY loadStatusChanged)
    Q_PROPERTY(QString currentFilePath READ currentFilePath NOTIFY currentFilePathChanged)
    Q_PROPERTY(QString fileFilter READ fileFilter CONSTANT)

public:
    explicit FileController(QObject* parent = nullptr);
    ~FileController() override;

    static FileController* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    bool isLoading() const;
    float loadProgress() const;
    QString loadStatus() const;
    QString currentFilePath() const;
    QString fileFilter() const;

public slots:
    /**
     * @brief Open a file dialog and load selected file
     */
    void openFileDialog();

    /**
     * @brief Load a file by path
     */
    void loadFile(const QString& filePath);

    /**
     * @brief Load a file by URL
     */
    void loadFileUrl(const QUrl& fileUrl);

    /**
     * @brief Cancel current loading operation
     */
    void cancelLoad();

signals:
    void isLoadingChanged();
    void loadProgressChanged();
    void loadStatusChanged();
    void currentFilePathChanged();

    /**
     * @brief Emitted when a structure is successfully loaded
     */
    void structureLoaded(std::shared_ptr<atom::data::AtomicStructure> structure);

    /**
     * @brief Emitted when loading fails
     */
    void loadFailed(const QString& error);

private slots:
    void onLoadingStarted(const QString& filePath);
    void onProgressChanged(float progress, const QString& message);
    void onLoadingFinished(std::shared_ptr<atom::data::AtomicStructure> structure);
    void onLoadingFailed(const QString& error);
    void onLoadingCancelled();

private:
    std::unique_ptr<io::AsyncFileLoader> m_loader;
    QString m_currentFilePath;
    QString m_loadStatus;
    float m_loadProgress = 0.0f;
    bool m_isLoading = false;

    static FileController* s_instance;
};

} // namespace atom::ui
