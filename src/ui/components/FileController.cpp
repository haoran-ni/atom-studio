#include "FileController.h"
#include "StructureModel.h"
#include "../../io/AsyncFileLoader.h"
#include "../../data/Structure.h"
#include "../../python/ASEReader.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QQmlEngine>
#include <QDebug>
#include <QTimer>
#include <exception>
#include <utility>

namespace atom::ui {

FileController* FileController::s_instance = nullptr;

FileController::FileController(QObject* parent, std::unique_ptr<io::AsyncFileLoader> loader)
    : QObject(parent)
    , m_loader(loader ? std::move(loader) : std::make_unique<io::AsyncFileLoader>(this))
    , m_exporter(std::make_unique<io::AsyncStructureExporter>(this))
{
    // Connect loader signals
    connect(m_loader.get(), &io::AsyncFileLoader::loadingStarted,
            this, &FileController::onLoadingStarted);
    connect(m_loader.get(), &io::AsyncFileLoader::progressChanged,
            this, &FileController::onProgressChanged);
    connect(m_loader.get(), &io::AsyncFileLoader::loadingFinished,
            this, &FileController::onLoadingFinished);
    connect(m_loader.get(), &io::AsyncFileLoader::loadingFailed,
            this, &FileController::onLoadingFailed);
    connect(m_loader.get(), &io::AsyncFileLoader::loadingCancelled,
            this, &FileController::onLoadingCancelled);

    connect(m_exporter.get(), &io::AsyncStructureExporter::isExportingChanged,
            this, &FileController::isExportingChanged);
    connect(m_exporter.get(), &io::AsyncStructureExporter::exportStarted,
            this, &FileController::structureExportStarted);
    connect(m_exporter.get(), &io::AsyncStructureExporter::exportFinished,
            this, &FileController::structureExported);
    connect(m_exporter.get(), &io::AsyncStructureExporter::exportFailed,
            this, &FileController::structureExportFailed);

    s_instance = this;
}

FileController::~FileController() {
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

FileController* FileController::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) {
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)

    if (!s_instance) {
        s_instance = new FileController();
    }
    return s_instance;
}

bool FileController::isLoading() const {
    return m_isLoading;
}

float FileController::loadProgress() const {
    return m_loadProgress;
}

QString FileController::loadStatus() const {
    return m_loadStatus;
}

QString FileController::currentFilePath() const {
    return m_currentFilePath;
}

QString FileController::fileFilter() const {
    atom::python::ASEReader reader;
    return QString::fromStdString(reader.fileDialogFilter());
}

bool FileController::isExporting() const {
    return m_exporter->isExporting();
}

QStringList FileController::structureExportFormats() const {
    QStringList formats;
    for (const auto& type : io::structureFileTypes()) formats.append(type.id);
    return formats;
}

void FileController::openSaveStructureDialog(const QString& format) {
    if (isExporting()) return;
    const auto* type = io::structureFileType(format);
    if (!type) {
        emit structureExportFailed(tr("Unsupported structure export format: %1").arg(format));
        return;
    }
    const auto* model = StructureModel::instance();
    if (!model || !model->hasStructure()) {
        emit structureExportFailed(tr("There are no atoms to export."));
        return;
    }
    const auto structure = model->structure();
    if (type->format == io::StructureFileFormat::Poscar && !structure->hasLattice()) {
        emit structureExportFailed(tr("POSCAR export requires a valid lattice. This structure has no lattice."));
        return;
    }
    const QFileInfo source(QString::fromStdString(structure->sourcePath()));
    const QString directory = source.exists() ? source.absolutePath()
        : QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    const QString stem = source.completeBaseName().isEmpty() ? QStringLiteral("structure")
                                                            : source.completeBaseName();
    const QString name = type->format == io::StructureFileFormat::Poscar
        ? QStringLiteral("POSCAR") : stem + "_export" + type->suffix;
    QFileDialog dialog(nullptr, tr("Export Current Structure"), directory, type->filter);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix(type->suffix.mid(1));
    dialog.selectFile(name);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
    exportStructure(dialog.selectedFiles().first(), format);
}

void FileController::exportStructure(const QString& filePath, const QString& format) {
    if (filePath.isEmpty()) return;
    if (isExporting()) {
        emit structureExportFailed(tr("A structure export is already in progress."));
        return;
    }
    const auto* type = io::structureFileType(format);
    if (!type) {
        emit structureExportFailed(tr("Unsupported structure export format: %1").arg(format));
        return;
    }
    const auto* model = StructureModel::instance();
    if (!model || !model->hasStructure()) {
        emit structureExportFailed(tr("There are no atoms to export."));
        return;
    }
    try {
        auto snapshot = io::StructureExportData::fromStructure(*model->structure());
        m_exporter->exportStructure(filePath, type->format, std::move(snapshot));
    } catch (const std::exception& error) {
        emit structureExportFailed(tr("Could not prepare the structure for export: %1")
                                       .arg(QString::fromUtf8(error.what())));
    }
}

void FileController::openFileDialog() {
    QString startDir = QStandardPaths::writableLocation(
        QStandardPaths::HomeLocation);

    QStringList filePaths = QFileDialog::getOpenFileNames(
        nullptr,
        tr("Import Atomic Structures"),
        startDir,
        fileFilter()
    );

    loadFiles(filePaths);
}

void FileController::loadFile(const QString& filePath) {
    loadFiles({filePath});
}

void FileController::loadFiles(const QStringList& filePaths) {
    for (const auto& path : filePaths) {
        if (!path.isEmpty()) m_pendingFiles.enqueue(path);
    }
    if (!m_isLoading && !m_pendingFiles.isEmpty()) startNextLoad();
}

void FileController::loadFileUrls(const QList<QUrl>& fileUrls) {
    QStringList paths;
    for (const auto& url : fileUrls) {
        if (url.isLocalFile()) paths.append(url.toLocalFile());
    }
    loadFiles(paths);
}

void FileController::startNextLoad() {
    if (m_pendingFiles.isEmpty()) {
        m_isLoading = false;
        emit isLoadingChanged();
        return;
    }
    m_loader->loadFile(m_pendingFiles.dequeue());
}

void FileController::continueLoading() {
    m_isLoading = !m_pendingFiles.isEmpty();
    emit isLoadingChanged();
    if (m_isLoading) QTimer::singleShot(0, this, &FileController::startNextLoad);
}

void FileController::openSaveImageDialog(const QString& format, bool includeAxes,
                                       bool transparentBackground) {
    QString filter;
    if (format == ".png")
        filter = tr("PNG Image (*.png)");
    else if (format == ".jpg")
        filter = tr("JPEG Image (*.jpg *.jpeg)");
    else
        filter = tr("All Files (*)");

    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
                          + "/output_image" + format;
    QString path = QFileDialog::getSaveFileName(nullptr, tr("Export Image"), defaultPath, filter);
    if (!path.isEmpty())
        emit saveImagePathSelected(path, format, includeAxes, transparentBackground);
}

void FileController::loadFileUrl(const QUrl& fileUrl) {
    loadFileUrls({fileUrl});
}

void FileController::cancelLoad() {
    m_pendingFiles.clear();
    if (m_isLoading) {
        m_loader->cancel();
    }
}

void FileController::onLoadingStarted(const QString& filePath) {
    m_isLoading = true;
    m_loadProgress = 0.0f;
    m_loadStatus = tr("Loading...");
    m_currentFilePath = filePath;

    emit loadProgressChanged();
    emit loadStatusChanged();
    emit currentFilePathChanged();
    emit isLoadingChanged();
    emit loadingStarted(filePath);
}

void FileController::onProgressChanged(float progress, const QString& message) {
    m_loadProgress = progress;
    m_loadStatus = message;

    emit loadProgressChanged();
    emit loadStatusChanged();
}

void FileController::onLoadingFinished(std::shared_ptr<data::Structure> structure) {
    m_loadProgress = 1.0f;
    m_loadStatus = tr("Loaded");

    emit loadProgressChanged();
    emit loadStatusChanged();

    // Directly update StructureModel since std::shared_ptr can't pass through QML
    StructureModel::instance()->addStructure(structure);

    emit structureLoaded(structure);

    qInfo() << "Loaded structure with" << structure->atomCount() << "atoms from"
            << m_currentFilePath;
    continueLoading();
}

void FileController::onLoadingFailed(const QString& error) {
    m_loadProgress = 0.0f;
    m_loadStatus = tr("Failed: %1").arg(error);

    emit loadProgressChanged();
    emit loadStatusChanged();
    emit loadFailed(error);

    qWarning() << "Failed to load file:" << error;
    continueLoading();
}

void FileController::onLoadingCancelled() {
    m_loadProgress = 0.0f;
    m_loadStatus = tr("Cancelled");

    emit loadProgressChanged();
    emit loadStatusChanged();
    continueLoading();
}

} // namespace atom::ui
