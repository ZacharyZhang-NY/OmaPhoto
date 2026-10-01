#include "Document/ImageTrim.h"
#include "IO/CanvasResizer.h"
#include "IO/ImageExporter.h"
#include "IO/ImageResizer.h"
#include "IO/ProjectController.h"
#include "UI/CanvasSizeSheet.h"
#include "UI/ImageSizeSheet.h"
#include "UI/TrimSheet.h"
#include <QDialog>
#include <QFutureWatcher>
#include <QVBoxLayout>
#include <QtConcurrent>

QDialog *ProjectController::sheet(const QString &title)
{
    auto *dialog = new QDialog(window);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSizeConstraint(QLayout::SetFixedSize);
    return dialog;
}

void ProjectController::canvasSize(std::function<void()> done)
{
    if (!window || !session.document() || !begin()) {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
        return;
    }
    QDialog *dialog = sheet(QStringLiteral("Canvas Size"));
    auto chosen = std::make_shared<std::optional<CanvasSizeOptions>>();
    dialog->layout()->addWidget(new CanvasSizeSheet(session.document().value(), session,
                                                    [dialog, chosen](std::optional<CanvasSizeOptions> options) {
                                                        *chosen = options;
                                                        dialog->done(options ? QDialog::Accepted : QDialog::Rejected);
                                                    }, dialog));
    connect(dialog, &QDialog::finished, this, [this, chosen, done] {
        const std::optional<CanvasSizeOptions> options = *chosen;
        resizeProject(options ? [options = *options](const ProjectSnapshot &snapshot) { return CanvasResizer::resize(snapshot, options); }
                              : std::function<std::optional<ProjectSnapshot>(const ProjectSnapshot &)>(),
                      [this](const ProjectSnapshot &resized) { session.applyDocumentSize(resized, QStringLiteral("Canvas Size")); },
                      QStringLiteral("Couldn’t change canvas size"), done);
    });
    dialog->open();
}

void ProjectController::imageSize(std::function<void()> done)
{
    if (!window || !session.document() || !begin()) {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
        return;
    }
    QDialog *dialog = sheet(QStringLiteral("Image Size"));
    auto chosen = std::make_shared<std::optional<ImageSizeOptions>>();
    dialog->layout()->addWidget(new ImageSizeSheet(session.document().value(), [dialog, chosen](std::optional<ImageSizeOptions> options) {
        *chosen = options;
        dialog->done(options ? QDialog::Accepted : QDialog::Rejected);
    }, dialog));
    connect(dialog, &QDialog::finished, this, [this, chosen, done] {
        const std::optional<ImageSizeOptions> options = *chosen;
        resizeProject(options ? [options = *options](const ProjectSnapshot &snapshot) { return ImageResizer::resize(snapshot, options); }
                              : std::function<std::optional<ProjectSnapshot>(const ProjectSnapshot &)>(),
                      [this](const ProjectSnapshot &resized) { session.applyImageSize(resized); }, QStringLiteral("Couldn’t resize the image"), done);
    });
    dialog->open();
}

void ProjectController::trim(std::function<void()> done)
{
    if (!window || !session.document() || !begin()) {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
        return;
    }
    QDialog *dialog = sheet(QStringLiteral("Trim"));
    auto chosen = std::make_shared<std::optional<TrimOptions>>();
    dialog->layout()->addWidget(new TrimSheet([dialog, chosen](std::optional<TrimOptions> options) {
        *chosen = options;
        dialog->done(options ? QDialog::Accepted : QDialog::Rejected);
    }, dialog));
    connect(dialog, &QDialog::finished, this, [this, chosen, done] {
        const std::optional<TrimOptions> options = *chosen;
        resizeProject(options ? [options = *options](const ProjectSnapshot &snapshot) { return ImageTrim::trim(snapshot, options); }
                              : std::function<std::optional<ProjectSnapshot>(const ProjectSnapshot &)>(),
                      [this](const ProjectSnapshot &trimmed) { session.applyDocumentSize(trimmed, QStringLiteral("Trim")); },
                      QStringLiteral("Couldn’t trim image"), done);
    });
    dialog->open();
}

// Cancelled, nothing runs; running out of memory fails a render.
void ProjectController::resizeProject(std::function<std::optional<ProjectSnapshot>(const ProjectSnapshot &)> resize, std::function<void(const ProjectSnapshot &)> land,
                                      const QString &failure, std::function<void()> done)
{
    const auto end = [this, done] {
        session.setIsProjectBusy(false);
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
    };
    const auto explain = [this, failure, end](const QString &message) { showError(failure, message, end); };
    const QString render = QString::fromUtf8(ExportError(ExportError::Kind::render).what());
    // Taken and started before anything changes; running out explains.
    std::optional<ProjectSnapshot> snapshot;
    QFutureWatcher<Resized> *watcher = nullptr;
    try {
        if (resize)
            snapshot = session.projectSnapshot();
        if (snapshot) {
            watcher = new QFutureWatcher<Resized>(this);
            connect(watcher, &QFutureWatcherBase::finished, this, [watcher, land, explain, end, render] {
                watcher->deleteLater();
                try {
                    const Resized result = watcher->future().takeResult();
                    if (!result.failure.isEmpty()) {
                        explain(result.failure);
                        return;
                    }
                    // Swift's trim with nothing left changes nothing.
                    if (result.snapshot)
                        land(*result.snapshot);
                } catch (const std::bad_alloc &) {
                    explain(render);
                    return;
                }
                end();
            });
            watcher->setFuture(QtConcurrent::run([snapshot = std::move(*snapshot), resize]() -> Resized {
                try {
                    return {resize(snapshot), QString()};
                } catch (const std::runtime_error &error) {
                    return {std::nullopt, QString::fromUtf8(error.what())};
                }
            }));
        }
    } catch (const std::bad_alloc &) {
        delete watcher;
        explain(render);
        return;
    }
    if (!snapshot)
        end();
}
