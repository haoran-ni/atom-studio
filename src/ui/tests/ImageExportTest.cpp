#include <QtQuickTest/quicktest.h>
#include <QQmlContext>
#include <QQmlEngine>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QQuickItem>
#include <QQuickWindow>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

class ExportFiles : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString outputPath READ outputPath CONSTANT)

public:
    QString outputPath() const { return m_directory.filePath("export.png"); }
    Q_INVOKABLE void clear() { QFile::remove(outputPath()); }
    Q_INVOKABLE bool exists() const { return QFile::exists(outputPath()); }
    Q_INVOKABLE bool dropFiles(QQuickItem* panel) {
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile("/tmp/first.xyz"), QUrl::fromLocalFile("/tmp/second.xyz")});
        const QPoint point = panel->mapToScene(QPointF(120, 80)).toPoint();
        QDragEnterEvent enter(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(panel->window(), &enter);
        QDropEvent drop(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(panel->window(), &drop);
        return drop.isAccepted();
    }
    Q_INVOKABLE QColor pixel(int x, int y) const {
        return QImage(outputPath()).pixelColor(x, y);
    }

private:
    QTemporaryDir m_directory;
};

class ImageExportSetup : public QObject {
    Q_OBJECT

public slots:
    void qmlEngineAvailable(QQmlEngine* engine) {
        // Use a controlled asynchronous viewport so stale frame delivery is
        // deterministic, without depending on GPU speed or a window server.
        engine->addImportPath(QStringLiteral(QUICK_TEST_SOURCE_DIR));
        engine->rootContext()->setContextProperty("ExportFiles", &m_files);
    }

private:
    ExportFiles m_files;
};

QUICK_TEST_MAIN_WITH_SETUP(imageexport, ImageExportSetup)

#include "ImageExportTest.moc"
