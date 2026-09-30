#include "IO/ProjectController.h"
#include "Document/ProjectWorkspace.h"
#include "IO/ProjectStore.h"
#include "Logging.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QtConcurrent>

bool ProjectController::isProject(const QUrl &url)
{
    return QFileInfo(url.adjusted(QUrl::StripTrailingSlash).path()).suffix().toLower() == QLatin1String("comp");
}

bool ProjectController::samePlace(const std::optional<QString> &path, const QString &other)
{
    // Nothing there resolves to nothing: compare the names then.
    const auto resolved = [](const QString &place) {
        const QString canonical = QFileInfo(place).canonicalFilePath();
        return canonical.isEmpty() ? QFileInfo(place).absoluteFilePath() : canonical;
    };
    return path && resolved(*path) == resolved(other);
}

ProjectController::ProjectController(EditorSession &session) : session(session)
{
    externalChanges.recheck = new QTimer(this);
    externalChanges.recheck->setSingleShot(true);
    externalChanges.recheck->setTimerType(Qt::PreciseTimer);
    connect(externalChanges.recheck, &QTimer::timeout, this, &ProjectController::noteExternalChange);
}

bool ProjectController::canStart() const
{
    return session.canStartProjectOperation() && !(workspace && workspace->isManaging());
}

std::optional<QUuid> ProjectController::ownTab() const
{
    if (!workspace)
        return std::nullopt;
    for (const std::shared_ptr<ProjectTab> &tab : workspace->tabs()) {
        if (&tab->controller == this)
            return tab->id;
    }
    return std::nullopt;
}

bool ProjectController::begin()
{
    if (!session.canStartProjectOperation())
        return false;
    session.cancelCrop();
    session.commitTransform();
    session.setIsProjectBusy(true);
    return true;
}

void ProjectController::finish(const std::function<void(bool)> &done, bool value)
{
    // The caller goes on first, like a resumed Swift task.
    if (done)
        QMetaObject::invokeMethod(this, [done, value] { done(value); }, Qt::QueuedConnection);
}

void ProjectController::save(bool asNew, std::function<void(bool)> done)
{
    if (!session.document()) {
        finish(done, false);
        return;
    }
    // A save still writing finishes; this one saves what changed.
    finishWriting([this, asNew, done] {
        if (!begin()) {
            finish(done, false);
            return;
        }
        prepareSave(asNew, [this, done](std::optional<Prepared> prepared) {
            // The writer is reserved before the tools go free.
            if (prepared)
                write(*prepared, [this, done](bool saved) { finish(done, saved); });
            session.setIsProjectBusy(false);
            if (!prepared)
                finish(done, false);
        });
    });
}

void ProjectController::saveCurrent(bool asNew, std::function<void(bool)> then)
{
    if (!session.document()) {
        then(true);
        return;
    }
    prepareSave(asNew, [this, then](std::optional<Prepared> prepared) {
        if (!prepared) {
            then(false);
            return;
        }
        write(*prepared, then);
    });
}

void ProjectController::prepareSave(bool asNew, std::function<void(std::optional<Prepared>)> then)
{
    const QUuid revision = session.history.currentRevision();
    // Undo may have taken the document while this waited.
    const std::optional<ProjectSnapshot> captured = session.projectSnapshot();
    if (!captured) {
        then(std::nullopt);
        return;
    }
    const ProjectSnapshot snapshot = *captured;
    if (!asNew && session.projectPath()) {
        then(Prepared{snapshot, *session.projectPath(), revision});
        return;
    }
    auto *panel = new QFileDialog(window, asNew ? QStringLiteral("Save Project As") : QStringLiteral("Save Project"));
    panel->setAttribute(Qt::WA_DeleteOnClose);
    panel->setAcceptMode(QFileDialog::AcceptSave);
    panel->setDefaultSuffix(QStringLiteral("comp"));
    panel->selectFile(session.projectPath() ? QFileInfo(*session.projectPath()).fileName() : QStringLiteral("Untitled.comp"));
    connect(panel, &QDialog::finished, this, [this, panel, snapshot, revision, then](int result) {
        if (result != QDialog::Accepted) {
            then(std::nullopt);
            return;
        }
        // The panel only suggests the suffix; a project needs it.
        const QString chosen = panel->selectedFiles().value(0);
        if (isProject(QUrl::fromLocalFile(chosen))) {
            then(Prepared{snapshot, chosen, revision});
            return;
        }
        // The panel never saw this name: nothing there is replaced.
        const QFileInfo renamed(chosen + QStringLiteral(".comp"));
        // A link that leads nowhere is still in the way.
        if (renamed.exists() || renamed.isSymLink()) {
            showError(QStringLiteral("Couldn’t save the project"),
                      QStringLiteral("“%1” already exists. Choose another name.").arg(renamed.fileName()), [then] { then(std::nullopt); });
            return;
        }
        then(Prepared{snapshot, renamed.filePath(), revision});
    });
    panel->open();
}

void ProjectController::finishWriting(std::function<void()> then)
{
    if (!m_writing) {
        then();
        return;
    }
    m_writeWaiters.push_back(std::move(then));
}

// Writes a captured document; only that version counts as saved.
void ProjectController::write(const Prepared &prepared, std::function<void(bool)> then)
{
    m_writing = true;
    // Waiters go on from the event loop once it lands.
    const auto landed = [this, then](bool saved) {
        m_writing = false;
        // Each looks again: an earlier one may be writing.
        for (std::function<void()> &waiter : std::exchange(m_writeWaiters, {}))
            QMetaObject::invokeMethod(this, [this, waiter = std::move(waiter)] { finishWriting(waiter); }, Qt::QueuedConnection);
        then(saved);
    };
    // Our own save changes the package: its events are ours.
    externalChanges.saving = true;
    const QString destination = prepared.destination;
    const ProjectSnapshot snapshot = prepared.snapshot;
    auto *watcher = new QFutureWatcher<std::optional<QString>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, destination, revision = prepared.revision, landed] {
        watcher->deleteLater();
        if (const std::optional<QString> failure = watcher->result()) {
            // Swift's defer: the guard lasts until the alert is answered.
            showError(QStringLiteral("Couldn’t save the project"), *failure, [this, landed] {
                externalChanges.saving = false;
                landed(false);
            });
            return;
        }
        // Marked first: the path's signal shows the saved state.
        session.history.markSaved(revision);
        session.setProjectPath(destination);
        m_saveGeneration += 1;
        rememberProjectDigest(destination, [this, destination, landed] {
            watchProject(destination);
            externalChanges.saving = false;
            landed(true);
        });
    });
    // The store blocks on the disk: off the UI thread.
    watcher->setFuture(QtConcurrent::run([snapshot, destination]() -> std::optional<QString> {
        try {
            ProjectStore::save(snapshot, destination);
            return std::nullopt;
        } catch (const std::runtime_error &error) {
            return QString::fromUtf8(error.what());
        }
    }));
}

void ProjectController::load(const QString &path, std::function<void(Loaded)> then)
{
    auto *watcher = new QFutureWatcher<Loaded>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [watcher, then] {
        watcher->deleteLater();
        then(watcher->result());
    });
    watcher->setFuture(QtConcurrent::run([path]() -> Loaded {
        try {
            return {ProjectStore::load(path), QString()};
        } catch (const std::runtime_error &error) {
            return {std::nullopt, QString::fromUtf8(error.what())};
        }
    }));
}

void ProjectController::open(std::optional<QString> path, std::function<void(bool)> done)
{
    if (workspace) {
        workspace->open(path, done);
        return;
    }
    if (!begin()) {
        finish(done, false);
        return;
    }
    const auto end = [this, done](bool opened) {
        session.setIsProjectBusy(false);
        finish(done, opened);
    };
    const auto refuse = [this, end](const QString &failure) {
        showError(QStringLiteral("Couldn’t open the project"), failure, [end] { end(false); });
    };
    const auto read = [this, end, refuse](QString source) {
        // A folder's trailing slash would leave it without a name.
        while (source.endsWith(QLatin1Char('/')))
            source.chop(1);
        // A write in flight lands first: no read mid-save.
        finishWriting([this, end, refuse, source] {
            // Validated first: a corrupt project never discards the live document.
            load(source, [this, end, refuse, source](const Loaded &first) {
                if (!first.snapshot) {
                    refuse(first.failure);
                    return;
                }
                const int previousSave = m_saveGeneration;
                confirmReplacement([this, end, refuse, source, first, previousSave](bool proceed) {
                    if (!proceed) {
                        end(false);
                        return;
                    }
                    // Saving in the confirmation can replace the opened file.
                    if (m_saveGeneration == previousSave || !samePlace(session.projectPath(), source)) {
                        session.installProject(*first.snapshot, source);
                        rememberProjectDigest(source, [this, source, end] {
                            watchProject(source);
                            end(true);
                        });
                        return;
                    }
                    load(source, [this, end, refuse, source](const Loaded &again) {
                        if (!again.snapshot) {
                            refuse(again.failure);
                            return;
                        }
                        session.installProject(*again.snapshot, source);
                        rememberProjectDigest(source, [this, source, end] {
                            watchProject(source);
                            end(true);
                        });
                    });
                });
            });
        });
    };
    if (path) {
        read(*path);
        return;
    }
    // A project is a folder here: one a dialog.
    auto *panel = new QFileDialog(window, QStringLiteral("Open Project"));
    panel->setAttribute(Qt::WA_DeleteOnClose);
    panel->setFileMode(QFileDialog::Directory);
    connect(panel, &QDialog::finished, this, [panel, read, end](int result) {
        if (result != QDialog::Accepted) {
            end(false);
            return;
        }
        read(panel->selectedFiles().value(0));
    });
    panel->open();
}

void ProjectController::newCanvas()
{
    if (workspace) {
        workspace->newCanvas();
        return;
    }
    if (!begin())
        return;
    confirmReplacement([this](bool proceed) {
        session.setIsProjectBusy(false);
        if (!proceed)
            return;
        session.clearProject();
        stopWatchingProject();
    });
}

void ProjectController::close(QWidget *closing)
{
    if (const std::optional<QUuid> tab = ownTab()) {
        workspace->close(*tab);
        return;
    }
    if (!begin())
        return;
    confirmReplacement([this, closing = QPointer<QWidget>(closing)](bool proceed) {
        session.setIsProjectBusy(false);
        if (!proceed)
            return;
        session.clearProject();
        stopWatchingProject();
        if (closing)
            closing->close();
    });
}

void ProjectController::confirmQuit(std::function<void(bool)> done)
{
    if (!begin()) {
        finish(done, false);
        return;
    }
    confirmReplacement([this, done](bool proceed) {
        session.setIsProjectBusy(false);
        finish(done, proceed);
    });
}

void ProjectController::confirmReplacement(std::function<void(bool)> then)
{
    // A save still writing finishes: no file is cut short.
    finishWriting([this, then] {
        if (!session.isModified() || !session.document()) {
            then(true);
            return;
        }
        const QString name = session.projectPath() ? QFileInfo(*session.projectPath()).fileName() : QStringLiteral("Untitled");
        auto *alert = new QMessageBox(window);
        alert->setAttribute(Qt::WA_DeleteOnClose);
        alert->setIcon(QMessageBox::Warning);
        alert->setText(QStringLiteral("Save changes to %1?").arg(name));
        alert->setInformativeText(QStringLiteral("Your changes will be lost if you don’t save them."));
        const QPushButton *save = alert->addButton(QStringLiteral("Save"), QMessageBox::AcceptRole);
        alert->addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
        const QPushButton *discard = alert->addButton(QStringLiteral("Don’t Save"), QMessageBox::DestructiveRole);
        connect(alert, &QDialog::finished, this, [this, alert, save, discard, then] {
            if (alert->clickedButton() == save)
                saveCurrent(false, then);
            else
                then(alert->clickedButton() == discard);
        });
        alert->open();
    });
}

void ProjectController::showError(const QString &title, const QString &message, std::function<void()> then)
{
    qCWarning(lcIO).noquote() << title + QStringLiteral(": ") + message;
    auto *alert = new QMessageBox(window);
    alert->setAttribute(Qt::WA_DeleteOnClose);
    alert->setIcon(QMessageBox::Warning);
    alert->setText(title);
    alert->setInformativeText(message);
    alert->addButton(QStringLiteral("OK"), QMessageBox::AcceptRole);
    connect(alert, &QDialog::finished, this, std::move(then));
    alert->open();
}

void ProjectController::receive(const QList<QUrl> &urls, std::optional<QPointF> point, std::function<void()> done)
{
    if (const std::optional<QUuid> tab = ownTab()) {
        workspace->receive(urls, tab, point, done);
        return;
    }
    if (urls.isEmpty()) {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
        return;
    }
    m_incoming.push_back({urls, point, std::move(done)});
    if (m_processing)
        return;
    m_processing = true;
    QMetaObject::invokeMethod(this, &ProjectController::drainIncoming, Qt::QueuedConnection);
}

void ProjectController::drainIncoming()
{
    if (m_incoming.empty()) {
        m_processing = false;
        return;
    }
    const Incoming request = m_incoming.front();
    m_incoming.pop_front();
    // The session keeps this waiter, perhaps longer than the controller.
    session.waitForFileRequest([self = QPointer<ProjectController>(this), request] {
        if (self)
            self->handle(request);
    });
}

void ProjectController::handle(const Incoming &request)
{
    QList<QUrl> projects, images;
    for (const QUrl &url : request.urls)
        (isProject(url) ? projects : images) << url;
    const auto next = [this, done = request.done] {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
        drainIncoming();
    };
    if (projects.size() > 1) {
        showError(QStringLiteral("Open one project at a time"), QString::fromUtf8(ProjectError(ProjectError::Kind::invalid).what()), next);
        return;
    }
    // Beside a project the images go to the centre.
    const auto bringIn = [this, images, next, point = projects.isEmpty() ? request.point : std::nullopt](bool proceed) {
        if (!proceed) {
            next();
            return;
        }
        // The session keeps this callback as well.
        session.importImages(images, point, [self = QPointer<ProjectController>(this), next] {
            if (self)
                next();
        });
    };
    if (projects.isEmpty())
        bringIn(true);
    else
        open(projects.constFirst().toLocalFile(), bringIn);
}
