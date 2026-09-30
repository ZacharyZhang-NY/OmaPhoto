#include "IO/ProjectWatcher.h"
#include <QFile>
#include <QFileInfo>
#include <sys/stat.h>

ProjectWatcher::ProjectWatcher(const QString &path, std::function<void()> onChange, QObject *parent)
    : QObject(parent), path(path), m_onChange(std::move(onChange)), m_delivery(new QTimer(this)), m_rearm(new QTimer(this))
{
    m_delivery->setObjectName(QStringLiteral("delivery"));
    m_delivery->setSingleShot(true);
    // Swift's sleeps never end early; a coarse timer may.
    m_delivery->setTimerType(Qt::PreciseTimer);
    m_delivery->setInterval(coalescing);
    connect(m_delivery, &QTimer::timeout, this, [this] { m_onChange(); });
    // A swapped-in package retires the watched inodes: arm by path.
    m_rearm->setObjectName(QStringLiteral("rearm"));
    m_rearm->setInterval(100);
    m_rearm->setTimerType(Qt::PreciseTimer);
    connect(m_rearm, &QTimer::timeout, this, [this] {
        arm();
        m_rearmTries += 1;
        if (m_watcher.files().size() + m_watcher.directories().size() == watchedPaths().size() || m_rearmTries >= 20)
            m_rearm->stop();
    });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &ProjectWatcher::noteEvent);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &ProjectWatcher::noteEvent);
    // The parent's other entries are not ours: only another package.
    connect(&m_parent, &QFileSystemWatcher::directoryChanged, this, [this] {
        if (identity() != m_identity)
            noteEvent();
    });
    m_parent.addPath(QFileInfo(path).absolutePath());
    arm();
}

QStringList ProjectWatcher::watchedPaths() const
{
    return {path, path + QStringLiteral("/manifest.json"), path + QStringLiteral("/images")};
}

std::optional<std::pair<dev_t, ino_t>> ProjectWatcher::identity() const
{
    struct stat status;
    if (::stat(QFile::encodeName(path).constData(), &status) != 0)
        return std::nullopt;
    return std::pair(status.st_dev, status.st_ino);
}

void ProjectWatcher::arm()
{
    m_identity = identity();
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);
    for (const QString &each : watchedPaths()) {
        if (QFileInfo::exists(each))
            m_watcher.addPath(each);
    }
}

void ProjectWatcher::noteEvent()
{
    m_rearmTries = 0;
    m_rearm->start();
    m_delivery->start();
}
