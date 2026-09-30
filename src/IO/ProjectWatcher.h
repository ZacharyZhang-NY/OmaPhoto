#pragma once
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <functional>
#include <optional>
#include <sys/types.h>

// Swift's ProjectWatcher: tells its owner the package changed on disk.
class ProjectWatcher : public QObject {
public:
    ProjectWatcher(const QString &path, std::function<void()> onChange, QObject *parent = nullptr);

    const QString path;
    // Quiet time before reporting, so one save reports once.
    static constexpr int coalescing = 300;

private:
    QStringList watchedPaths() const;
    void arm();
    void noteEvent();
    // The package folder's device and inode, when it exists.
    std::optional<std::pair<dev_t, ino_t>> identity() const;

    const std::function<void()> m_onChange;
    QFileSystemWatcher m_watcher;
    // Qt watches no IN_MOVE_SELF: a renamed package shows above.
    QFileSystemWatcher m_parent;
    std::optional<std::pair<dev_t, ino_t>> m_identity;
    // Named children, so tests read whether they run.
    QTimer *const m_delivery;
    QTimer *const m_rearm;
    int m_rearmTries = 0;
};
