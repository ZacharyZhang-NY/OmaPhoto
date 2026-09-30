#pragma once
#include "Document/EditorSession.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectController+ExternalChanges.h"
#include <QList>
#include <QPointer>
#include <QUrl>
#include <QWidget>
#include <deque>
#include <functional>
#include <optional>

class ProjectWorkspace;
class QDialog;

// One project's file operations: save, open, close, drops.
class ProjectController : public QObject {
    Q_OBJECT
public:
    explicit ProjectController(EditorSession &session);

    // Its owner declares the session first: it outlives this.
    EditorSession &session;
    // Dialogs open over it; without one they stand alone.
    QPointer<QWidget> window;
    // With one, opening and closing are the workspace's to do.
    QPointer<ProjectWorkspace> workspace;

    static bool isProject(const QUrl &url);
    // One place on disk, whatever links lead to it.
    static bool samePlace(const std::optional<QString> &path, const QString &other);

    bool canStart() const;
    // Each `done` runs from the event loop, never inline.
    void save(bool asNew = false, std::function<void(bool)> done = {});
    void open(std::optional<QString> path = std::nullopt, std::function<void(bool)> done = {});
    void newCanvas();
    void close(QWidget *closing);
    void confirmQuit(std::function<void(bool)> done);
    void receive(const QList<QUrl> &urls, std::optional<QPointF> point = std::nullopt, std::function<void()> done = {});
    // Swift's sheets over the window; resizes off the UI thread.
    void canvasSize(std::function<void()> done = {});
    void imageSize(std::function<void()> done = {});
    void trim(std::function<void()> done = {});
    void exportPNG(std::function<void()> done = {});
    void exportJPEG(std::function<void()> done = {});

    // Swift's ProjectController+ExternalChanges: the package kept in step.
    ExternalChangeState externalChanges;
    void watchProject(const QString &path);
    void stopWatchingProject();
    // Remembers the package as it is now, then calls `then`.
    void rememberProjectDigest(const QString &path, std::function<void()> then);
    // Its tab came to the front: ask a held question.
    void resumeExternalChangeCheck();

private:
    struct Incoming {
        QList<QUrl> urls;
        std::optional<QPointF> point;
        std::function<void()> done;
    };
    struct Loaded {
        std::optional<ProjectSnapshot> snapshot;
        QString failure;
    };
    using Resized = Loaded;
    struct Rendered {
        std::optional<ExportRaster> raster;
        QString failure;
    };

    struct Prepared {
        ProjectSnapshot snapshot;
        QString destination;
        QUuid revision;
    };

    bool begin();
    void finish(const std::function<void(bool)> &done, bool value);
    void saveCurrent(bool asNew, std::function<void(bool)> then);
    // Swift's prepareSave: the document now, and where it goes.
    void prepareSave(bool asNew, std::function<void(std::optional<Prepared>)> then);
    void write(const Prepared &prepared, std::function<void(bool)> then);
    // A save still writing finishes first; then `then` runs.
    void finishWriting(std::function<void()> then);
    void load(const QString &path, std::function<void(Loaded)> then);
    void confirmReplacement(std::function<void(bool)> then);
    void showError(const QString &title, const QString &message, std::function<void()> then);
    // Swift's beginSheet: window-modal over the window; Escape cancels.
    QDialog *sheet(const QString &title);
    // Off the UI thread, then landed, skipped or explained.
    void resizeProject(std::function<std::optional<ProjectSnapshot>(const ProjectSnapshot &)> resize, std::function<void(const ProjectSnapshot &)> land,
                       const QString &failure, std::function<void()> done);
    void exportPanel(const QString &title, const QString &filter, const QStringList &suffixes, const QString &name, const QString &failure,
                     std::function<void(std::optional<QString>)> then);
    void exportWork(std::function<void()> work, const QString &failure, std::function<void()> end);
    void drainIncoming();
    void handle(const Incoming &request);
    std::optional<QUuid> ownTab() const;
    void noteExternalChange();
    void checkExternalChange();
    bool isFrontmost() const;
    void scheduleRecheck();
    void reloadFromDisk(const QString &path, std::function<void()> then);
    void askToRevert(std::function<void(bool)> then);

    int m_saveGeneration = 0;
    // Swift's `writing`: a save writes in the background.
    bool m_writing = false;
    std::vector<std::function<void()>> m_writeWaiters;
    std::deque<Incoming> m_incoming;
    bool m_processing = false;
};
