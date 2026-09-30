#include "IO/RecentProjects.h"
#include <QFileInfo>
#include <QGuiApplication>
#include <QSettings>

namespace {
const QString key = QStringLiteral("recentProjects");

// One place however it was reached, as Finder keeps it.
QString resolved(const QString &path)
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    return canonical.isEmpty() ? QFileInfo(path).absoluteFilePath() : canonical;
}
}

RecentProjects &RecentProjects::shared()
{
    static RecentProjects recent;
    return recent;
}

RecentProjects::RecentProjects()
{
    refresh();
    // Checked again on activation; a GUI-less run has none.
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(app, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive)
                refresh();
        });
    }
}

void RecentProjects::note(const QString &path)
{
    const QString place = resolved(path);
    QStringList stored = QSettings().value(key).toStringList();
    stored.removeAll(place);
    stored.prepend(place);
    store(stored.mid(0, limit));
}

void RecentProjects::clear()
{
    store({});
}

void RecentProjects::refresh()
{
    QStringList existing;
    for (const QString &path : QSettings().value(key).toStringList()) {
        if (QFileInfo::exists(path))
            existing << path;
    }
    if (existing == m_paths)
        return;
    m_paths = existing;
    emit changed();
}

void RecentProjects::store(const QStringList &paths)
{
    QSettings().setValue(key, paths);
    refresh();
}
