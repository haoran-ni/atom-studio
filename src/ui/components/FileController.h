#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QQueue>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include "../../io/AsyncFileLoader.h"
#include "../../io/AsyncStructureExporter.h"

namespace atom::data {
class Structure;
}

namespace atom::ui {

/**
 * @brief QML singleton for file operations
 *
 * Provides file dialogs, async loading, and export of the current structure.
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
    Q_PROPERTY(bool isExporting READ isExporting NOTIFY isExportingChanged)
    Q_PROPERTY(QStringList structureExportFormats READ structureExportFormats CONSTANT)

public:
    explicit FileController(QObject* parent = nullptr,
                            std::unique_ptr<io::AsyncFileLoader> loader = {});
    ~FileController() override;

    static FileController* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    bool isLoading() const;
    float loadProgress() const;
    QString loadStatus() const;
    QString currentFilePath() const;
    QString fileFilter() const;
    bool isExporting() const;
    QStringList structureExportFormats() const;

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
    void loadFileUrls(const QList<QUrl>& fileUrls);
    void loadFiles(const QStringList& filePaths);

    /**
     * @brief Cancel current loading operation
     */
    void cancelLoad();

    /**
     * @brief Open a save-image dialog and emit saveImagePathSelected on confirmation
     */
    Q_INVOKABLE void openSaveImageDialog(const QString& format, bool includeAxes,
                                        bool transparentBackground);

    Q_INVOKABLE void openSaveStructureDialog(const QString& format);
    // Captures the working structure at this call, including all geometry edits.
    Q_INVOKABLE void exportStructure(const QString& filePath, const QString& format);

signals:
    void isLoadingChanged();
    void loadProgressChanged();
    void loadStatusChanged();
    void currentFilePathChanged();
    void isExportingChanged();
    void structureExportStarted(const QString& filePath);
    void structureExported(const QString& filePath);
    void structureExportFailed(const QString& error);

    /**
     * @brief Emitted when file loading begins.
     */
    void loadingStarted(const QString& filePath);

    /**
     * @brief Emitted when a structure is successfully loaded
     */
    void structureLoaded(std::shared_ptr<atom::data::Structure> structure);

    /**
     * @brief Emitted when loading fails
     */
    void loadFailed(const QString& error);

    /**
     * @brief Emitted when the user confirms an image save path
     */
    void saveImagePathSelected(const QString& filePath, const QString& format, bool includeAxes,
                               bool transparentBackground);

private slots:
    void onLoadingStarted(const QString& filePath);
    void onProgressChanged(float progress, const QString& message);
    void onLoadingFinished(std::shared_ptr<atom::data::Structure> structure);
    void onLoadingFailed(const QString& error);
    void onLoadingCancelled();

private:
    std::unique_ptr<io::AsyncFileLoader> m_loader;
    std::unique_ptr<io::AsyncStructureExporter> m_exporter;
    QQueue<QString> m_pendingFiles;
    void startNextLoad();
    void continueLoading();
    QString m_currentFilePath;
    QString m_loadStatus;
    float m_loadProgress = 0.0f;
    bool m_isLoading = false;

    static FileController* s_instance;
};

} // namespace atom::ui
