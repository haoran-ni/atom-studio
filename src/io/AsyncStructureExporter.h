#pragma once

#include "StructureFileWriter.h"
#include <QFutureWatcher>
#include <QObject>

namespace atom::io {

class AsyncStructureExporter : public QObject {
    Q_OBJECT

public:
    explicit AsyncStructureExporter(QObject* parent = nullptr);
    ~AsyncStructureExporter() override;
    bool isExporting() const { return m_isExporting; }
    void exportStructure(const QString& path, StructureFileFormat format, StructureExportData snapshot);

signals:
    void isExportingChanged();
    void exportStarted(const QString& path);
    void exportFinished(const QString& path);
    void exportFailed(const QString& error);

private:
    QFutureWatcher<StructureWriteResult> m_watcher;
    QString m_path;
    bool m_isExporting = false;
};

} // namespace atom::io
