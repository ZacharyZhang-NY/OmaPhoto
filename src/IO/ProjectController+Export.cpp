#include "Document/ProjectWorkspace.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectController.h"
#include "UI/JPEGExportSheet.h"
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLayout>
#include <QtConcurrent>

namespace {
// Swift's suggestion: the project's name, else Untitled.
QString suggested(const std::optional<QString> &project, const QString &suffix)
{
    return (project ? ProjectTab::nameWithoutSuffix(*project) : QStringLiteral("Untitled")) + suffix;
}
}

// Swift's NSSavePanel for one type, whose suffix a name needs.
void ProjectController::exportPanel(const QString &title, const QString &filter, const QStringList &suffixes, const QString &name, const QString &failure,
                                    std::function<void(std::optional<QString>)> then)
{
    auto *panel = new QFileDialog(window, title);
    panel->setAttribute(Qt::WA_DeleteOnClose);
    panel->setAcceptMode(QFileDialog::AcceptSave);
    panel->setNameFilter(filter);
    panel->setDefaultSuffix(suffixes.first());
    panel->selectFile(name);
    // Moved, never copied: `then` may hold a whole snapshot.
    connect(panel, &QDialog::finished, this, [this, panel, suffixes, failure, then = std::move(then)](int result) mutable {
        if (result != QDialog::Accepted) {
            then(std::nullopt);
            return;
        }
        const QString chosen = panel->selectedFiles().value(0);
        if (suffixes.contains(QFileInfo(chosen).suffix().toLower())) {
            then(chosen);
            return;
        }
        // The panel never saw this name: nothing there is replaced.
        const QFileInfo renamed(chosen + QLatin1Char('.') + suffixes.first());
        if (renamed.exists() || renamed.isSymLink()) {
            showError(failure, QStringLiteral("“%1” already exists. Choose another name.").arg(renamed.fileName()),
                      [then = std::move(then)]() mutable { then(std::nullopt); });
            return;
        }
        then(renamed.filePath());
    });
    panel->open();
}

// Encoding and the disk block: off the UI thread.
void ProjectController::exportWork(std::function<void()> work, const QString &failure, std::function<void()> end)
try {
    auto *watcher = new QFutureWatcher<std::optional<QString>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, failure, end] {
        watcher->deleteLater();
        if (const std::optional<QString> message = watcher->result()) {
            showError(failure, *message, end);
            return;
        }
        end();
    });
    watcher->setFuture(QtConcurrent::run([work = std::move(work)]() -> std::optional<QString> {
        try {
            work();
            return std::nullopt;
        } catch (const std::runtime_error &error) {
            return QString::fromUtf8(error.what());
        }
    }));
} catch (const std::bad_alloc &) {
    showError(failure, QString::fromUtf8(ExportError(ExportError::Kind::render).what()), end);
}

void ProjectController::exportPNG(std::function<void()> done)
{
    if (!session.document() || !begin()) {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
        return;
    }
    const auto end = [this, done] {
        session.setIsProjectBusy(false);
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
    };
    const QString failure = QStringLiteral("Couldn’t export PNG");
    std::optional<ProjectSnapshot> snapshot;
    try {
        snapshot = session.projectSnapshot();
    } catch (const std::bad_alloc &) {
        showError(failure, QString::fromUtf8(ExportError(ExportError::Kind::render).what()), end);
        return;
    }
    exportPanel(QStringLiteral("Export PNG"), QStringLiteral("PNG image (*.png)"), {QStringLiteral("png")}, suggested(session.projectPath(), QStringLiteral(".png")),
                failure, [this, snapshot = std::move(snapshot.value()), failure, end](std::optional<QString> path) {
                    if (!path) {
                        end();
                        return;
                    }
                    // The snapshot's copy may find no memory.
                    try {
                        exportWork([snapshot, path = *path] { ImageExporter::exportPNG(snapshot, path); }, failure, end);
                    } catch (const std::bad_alloc &) {
                        showError(failure, QString::fromUtf8(ExportError(ExportError::Kind::render).what()), end);
                    }
                });
}

void ProjectController::exportJPEG(std::function<void()> done)
{
    if (!window || !session.document() || !begin()) {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
        return;
    }
    const auto end = [this, done] {
        session.setIsProjectBusy(false);
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
    };
    const QString failure = QStringLiteral("Couldn’t export JPEG");
    const QString render = QString::fromUtf8(ExportError(ExportError::Kind::render).what());
    std::optional<ProjectSnapshot> snapshot;
    try {
        snapshot = session.projectSnapshot();
    } catch (const std::bad_alloc &) {
        showError(failure, render, end);
        return;
    }
    auto *watcher = new QFutureWatcher<Rendered>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, failure, end] {
        watcher->deleteLater();
        Rendered rendered = watcher->future().takeResult();
        if (!rendered.raster) {
            showError(failure, rendered.failure, end);
            return;
        }
        QDialog *dialog = sheet(QStringLiteral("Export JPEG"));
        auto chosen = std::make_shared<std::optional<QByteArray>>();
        dialog->layout()->addWidget(new JPEGExportSheet(std::move(*rendered.raster), [dialog, chosen](std::optional<QByteArray> data) {
            *chosen = std::move(data);
            dialog->done(*chosen ? QDialog::Accepted : QDialog::Rejected);
        }, dialog));
        connect(dialog, &QDialog::finished, this, [this, chosen, failure, end] {
            if (!*chosen) {
                end();
                return;
            }
            exportPanel(QStringLiteral("Export JPEG"), QStringLiteral("JPEG image (*.jpeg *.jpg *.jpe)"),
                        {QStringLiteral("jpeg"), QStringLiteral("jpg"), QStringLiteral("jpe")}, suggested(session.projectPath(), QStringLiteral(".jpg")), failure,
                        [this, data = **chosen, failure, end](std::optional<QString> path) {
                            if (!path) {
                                end();
                                return;
                            }
                            exportWork([data, path = *path] { ImageExporter::write(data, path); }, failure, end);
                        });
        });
        dialog->open();
    });
    watcher->setFuture(QtConcurrent::run([snapshot = std::move(snapshot.value())]() -> Rendered {
        try {
            return {ImageExporter::render(snapshot), QString()};
        } catch (const ExportError &error) {
            return {std::nullopt, QString::fromUtf8(error.what())};
        }
    }));
}
