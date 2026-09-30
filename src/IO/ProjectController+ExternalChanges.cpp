#include "Document/ProjectWorkspace.h"
#include "IO/ProjectController.h"
#include "IO/ProjectDigest.h"
#include "Logging.h"
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QtConcurrent>

namespace {
// Off the UI thread; a half-written package gives none.
QFuture<std::optional<QByteArray>> digestOf(const QString &path)
{
    return QtConcurrent::run([path]() -> std::optional<QByteArray> {
        try {
            return ProjectDigest::compute(path);
        } catch (const std::runtime_error &error) {
            qCDebug(lcIO) << "no digest for" << path << error.what();
            return std::nullopt;
        }
    });
}
}

void ProjectController::watchProject(const QString &path)
{
    if (externalChanges.watcher && externalChanges.watcher->path == path)
        return;
    externalChanges.watcher = std::make_unique<ProjectWatcher>(path, [this] { noteExternalChange(); });
}

void ProjectController::stopWatchingProject()
{
    externalChanges.watcher.reset();
    externalChanges.knownDigest.reset();
    externalChanges.recheck->stop();
    externalChanges.pending = false;
}

void ProjectController::rememberProjectDigest(const QString &path, std::function<void()> then)
{
    auto *watcher = new QFutureWatcher<std::optional<QByteArray>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, then] {
        watcher->deleteLater();
        externalChanges.knownDigest = watcher->result();
        then();
    });
    watcher->setFuture(digestOf(path));
}

void ProjectController::resumeExternalChangeCheck()
{
    if (externalChanges.pending)
        noteExternalChange();
}

void ProjectController::noteExternalChange()
{
    externalChanges.pending = true;
    if (externalChanges.checking)
        return;
    externalChanges.checking = true;
    checkExternalChange();
}

// Swift's loop: each pass takes one pending change, else stops.
void ProjectController::checkExternalChange()
{
    if (!externalChanges.pending) {
        externalChanges.checking = false;
        return;
    }
    externalChanges.pending = false;
    const std::optional<QString> path = session.projectPath();
    if (!path || !session.document() || externalChanges.saving) {
        externalChanges.checking = false;
        return;
    }
    // Content, not dates: sync clients touch metadata alone.
    auto *watcher = new QFutureWatcher<std::optional<QByteArray>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, path = *path] {
        watcher->deleteLater();
        const std::optional<QByteArray> digest = watcher->result();
        // Our save began meanwhile: look again once it lands.
        if (externalChanges.saving) {
            externalChanges.pending = true;
            externalChanges.checking = false;
            finishWriting([this] { resumeExternalChangeCheck(); });
            return;
        }
        if (!digest || digest == externalChanges.knownDigest) {
            checkExternalChange();
            return;
        }
        // An edit in progress finishes before the document goes.
        if (!session.canStartProjectOperation() || session.transformEdit() || (workspace && workspace->isManaging())) {
            externalChanges.checking = false;
            scheduleRecheck();
            return;
        }
        if (!session.isModified()) {
            reloadFromDisk(path, [this] { checkExternalChange(); });
            return;
        }
        if (!window || !isFrontmost()) {
            externalChanges.pending = true;
            externalChanges.checking = false;
            return;
        }
        const int askedAtSave = m_saveGeneration;
        askToRevert([this, path, digest, askedAtSave](bool revert) {
            if (!revert) {
                // A save while asking remembered what it wrote.
                if (m_saveGeneration == askedAtSave)
                    externalChanges.knownDigest = digest;
                checkExternalChange();
                return;
            }
            reloadFromDisk(path, [this] { checkExternalChange(); });
        });
    });
    watcher->setFuture(digestOf(*path));
}

bool ProjectController::isFrontmost() const
{
    return !workspace || &workspace->current().controller == this;
}

// Backs off while busy: a long operation is not polled.
void ProjectController::scheduleRecheck()
{
    externalChanges.pending = true;
    const int attempt = externalChanges.recheckAttempt;
    externalChanges.recheckAttempt = std::min(attempt + 1, 7);
    externalChanges.recheck->start(250 * (1 << attempt));
}

void ProjectController::reloadFromDisk(const QString &path, std::function<void()> then)
{
    externalChanges.recheckAttempt = 0;
    session.setIsProjectBusy(true);
    // Swift's store is an actor: no read mid-write.
    finishWriting([this, path, then] {
        // A package that fails to load leaves the document alone.
        load(path, [this, path, then](const Loaded &loaded) {
            if (!loaded.snapshot || session.projectPath() != path || !session.document()) {
                if (!loaded.snapshot)
                    qCInfo(lcIO).noquote() << "the changed package did not load yet:" << loaded.failure;
                session.setIsProjectBusy(false);
                then();
                return;
            }
            session.reloadProject(*loaded.snapshot);
            // As loaded, not first seen: it may have changed.
            rememberProjectDigest(path, [this, path, then] {
                externalChanges.reloadCount += 1;
                qCInfo(lcIO).noquote() << "reloaded" << path << "after it changed on disk";
                session.setIsProjectBusy(false);
                then();
            });
        });
    });
}

void ProjectController::askToRevert(std::function<void(bool)> then)
{
    const QString name = session.projectPath() ? QFileInfo(*session.projectPath()).fileName() : QStringLiteral("Untitled");
    auto *alert = new QMessageBox(window);
    alert->setAttribute(Qt::WA_DeleteOnClose);
    alert->setIcon(QMessageBox::Warning);
    alert->setText(QStringLiteral("“%1” was changed on disk.").arg(name));
    alert->setInformativeText(QStringLiteral("Another app changed this project. You can revert to the version on disk, losing your unsaved changes, or keep what you have."));
    const QPushButton *revert = alert->addButton(QStringLiteral("Revert"), QMessageBox::AcceptRole);
    alert->addButton(QStringLiteral("Keep Mine"), QMessageBox::RejectRole);
    connect(alert, &QDialog::finished, this, [alert, revert, then] { then(alert->clickedButton() == revert); });
    alert->open();
}
