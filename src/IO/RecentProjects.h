#pragma once
#include <QObject>
#include <QStringList>

// Swift's RecentProjects: newest first, kept in QSettings.
class RecentProjects : public QObject {
    Q_OBJECT
public:
    static RecentProjects &shared();
    // At most this many, as macOS keeps by default.
    static constexpr int limit = 10;

    // The projects that still exist, newest first.
    const QStringList &paths() const { return m_paths; }
    void note(const QString &path);
    void clear();
    // Drops the projects since moved or deleted.
    void refresh();

signals:
    void changed();

private:
    RecentProjects();
    void store(const QStringList &paths);

    QStringList m_paths;
};
