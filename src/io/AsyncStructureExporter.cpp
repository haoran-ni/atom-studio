#include "AsyncStructureExporter.h"
#include <QtConcurrent>
#include <exception>
#include <utility>

namespace atom::io {

AsyncStructureExporter::AsyncStructureExporter(QObject* parent) : QObject(parent) {
    connect(&m_watcher, &QFutureWatcher<StructureWriteResult>::finished, this, [this]() {
        const auto result = m_watcher.result();
        const QString path = m_path;
        m_isExporting = false;
        emit isExportingChanged();
        if (result.success) emit exportFinished(path);
        else emit exportFailed(result.error);
    });
}

AsyncStructureExporter::~AsyncStructureExporter() {
    m_watcher.waitForFinished();
}

void AsyncStructureExporter::exportStructure(const QString& path, StructureFileFormat format,
                                            StructureExportData snapshot) {
    if (m_isExporting) {
        emit exportFailed(tr("A structure export is already in progress."));
        return;
    }
    m_path = path;
    m_isExporting = true;
    emit isExportingChanged();
    emit exportStarted(path);
    m_watcher.setFuture(QtConcurrent::run([path, format, snapshot = std::move(snapshot)]() {
        try {
            return writeStructureFile(path, format, snapshot);
        } catch (const std::exception& error) {
            return StructureWriteResult{false, QString::fromUtf8(error.what())};
        } catch (...) {
            return StructureWriteResult{false, QObject::tr("An unexpected error occurred while exporting the structure.")};
        }
    }));
}

} // namespace atom::io
