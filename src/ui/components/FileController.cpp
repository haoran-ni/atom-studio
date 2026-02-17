#include "FileController.h"
#include "StructureModel.h"
#include "../../io/AsyncFileLoader.h"
#include "../../data/Structure.h"
#include "../../python/ASEReader.h"

#include <QFileDialog>
#include <QStandardPaths>
#include <QQmlEngine>
#include <QDebug>

namespace atom::ui {

FileController* FileController::s_instance = nullptr;

FileController::FileController(QObject* parent)
    : QObject(parent)
    , m_loader(std::make_unique<io::AsyncFileLoader>(this))
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

void FileController::openFileDialog() {
    QString startDir = QStandardPaths::writableLocation(
        QStandardPaths::HomeLocation);

    QString filePath = QFileDialog::getOpenFileName(
        nullptr,
        tr("Open Atomic Structure"),
        startDir,
        fileFilter()
    );

    if (!filePath.isEmpty()) {
        loadFile(filePath);
    }
}

void FileController::loadFile(const QString& filePath) {
    if (m_isLoading) {
        qWarning() << "FileController: Already loading a file";
        return;
    }

    m_loader->loadFile(filePath);
}

void FileController::loadFileUrl(const QUrl& fileUrl) {
    loadFile(fileUrl.toLocalFile());
}

void FileController::cancelLoad() {
    if (m_isLoading) {
        m_loader->cancel();
    }
}

void FileController::onLoadingStarted(const QString& filePath) {
    m_isLoading = true;
    m_loadProgress = 0.0f;
    m_loadStatus = tr("Loading...");
    m_currentFilePath = filePath;

    emit isLoadingChanged();
    emit loadProgressChanged();
    emit loadStatusChanged();
    emit currentFilePathChanged();
}

void FileController::onProgressChanged(float progress, const QString& message) {
    m_loadProgress = progress;
    m_loadStatus = message;

    emit loadProgressChanged();
    emit loadStatusChanged();
}

void FileController::onLoadingFinished(std::shared_ptr<data::Structure> structure) {
    m_isLoading = false;
    m_loadProgress = 1.0f;
    m_loadStatus = tr("Loaded");

    emit isLoadingChanged();
    emit loadProgressChanged();
    emit loadStatusChanged();

    // Directly update StructureModel since std::shared_ptr can't pass through QML
    StructureModel::instance()->setStructure(structure);

    emit structureLoaded(structure);

    qInfo() << "Loaded structure with" << structure->atomCount() << "atoms from"
            << m_currentFilePath;
}

void FileController::onLoadingFailed(const QString& error) {
    m_isLoading = false;
    m_loadProgress = 0.0f;
    m_loadStatus = tr("Failed: %1").arg(error);

    emit isLoadingChanged();
    emit loadProgressChanged();
    emit loadStatusChanged();
    emit loadFailed(error);

    qWarning() << "Failed to load file:" << error;
}

void FileController::onLoadingCancelled() {
    m_isLoading = false;
    m_loadProgress = 0.0f;
    m_loadStatus = tr("Cancelled");

    emit isLoadingChanged();
    emit loadProgressChanged();
    emit loadStatusChanged();
}

} // namespace atom::ui
