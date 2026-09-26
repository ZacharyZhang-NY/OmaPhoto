#include "UI/OmarchyTheme.h"
#include "Logging.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStyleFactory>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <sys/stat.h>

namespace {
// The file's assignments: quoted values, and keys without one.
struct Assignments {
    QMap<QString, QString> values;
    QSet<QString> malformed;

    QString value(const QString &key) const
    {
        if (malformed.contains(key))
            throw std::runtime_error("colors.toml: " + key.toStdString() + " is no quoted string");
        if (!values.contains(key))
            throw std::runtime_error("colors.toml lacks " + key.toStdString());
        return values.value(key);
    }
};

bool isQuote(QChar character)
{
    return character == QLatin1Char('"') || character == QLatin1Char('\'');
}

// A key or value with its matching quotes taken off.
QString unquoted(const QString &text)
{
    const bool quoted = text.size() >= 2 && isQuote(text.front()) && text.back() == text.front();
    return quoted ? text.mid(1, text.size() - 2) : text;
}

Assignments assignments(const QString &toml)
{
    Assignments result;
    for (const QString &each : toml.split(QLatin1Char('\n'))) {
        const QString line = each.trimmed();
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char('[')) || equals < 0)
            continue;
        const QString key = unquoted(line.left(equals).trimmed());
        const QString rest = line.mid(equals + 1).trimmed();
        const qsizetype close = rest.isEmpty() || !isQuote(rest.front()) ? -1 : rest.indexOf(rest.front(), 1);
        const QString after = close < 0 ? QString() : rest.mid(close + 1).trimmed();
        if (close < 0 || !(after.isEmpty() || after.startsWith(QLatin1Char('#'))))
            result.malformed.insert(key);
        else
            result.values.insert(key, rest.mid(1, close - 1));
    }
    return result;
}

QColor colour(const Assignments &found, const QString &key)
{
    static const QRegularExpression hex(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    const QString value = found.value(key);
    if (!hex.match(value).hasMatch())
        throw std::runtime_error("colors.toml: " + key.toStdString() + " is no #rrggbb colour: " + value.toStdString());
    return QColor(value);
}
}

OmarchyColors OmarchyColors::parse(const QString &toml)
{
    // Flat `key = "value"` lines; recognised keys well formed.
    const Assignments values = assignments(toml);
    const QString mode = values.value(QStringLiteral("mode"));
    if (mode != QLatin1String("dark") && mode != QLatin1String("light"))
        throw std::runtime_error("colors.toml: mode must be dark or light, not " + mode.toStdString());
    return {.dark = mode == QLatin1String("dark"),
            .accent = colour(values, QStringLiteral("accent")),
            .selection = colour(values, QStringLiteral("selection")),
            .muted = colour(values, QStringLiteral("muted")),
            .background = colour(values, QStringLiteral("background")),
            .darkBackground = colour(values, QStringLiteral("dark_background")),
            .darkerBackground = colour(values, QStringLiteral("darker_background")),
            .lighterBackground = colour(values, QStringLiteral("lighter_background")),
            .foreground = colour(values, QStringLiteral("foreground")),
            .darkForeground = colour(values, QStringLiteral("dark_foreground")),
            .lightForeground = colour(values, QStringLiteral("light_foreground")),
            .brightForeground = colour(values, QStringLiteral("bright_foreground")),
            .red = colour(values, QStringLiteral("red")),
            .warning = colour(values, values.values.contains(QStringLiteral("orange")) || values.malformed.contains(QStringLiteral("orange"))
                                          ? QStringLiteral("orange")
                                          : QStringLiteral("yellow"))};
}

OmarchyColors OmarchyColors::builtInDark()
{
    // Swift's grays: the window at 0.14, fields at 0.105.
    return {.dark = true,
            .accent = QColor(0x0a, 0x84, 0xff),
            .selection = QColor(0x3a, 0x3a, 0x3a),
            .muted = QColor(0x4a, 0x4a, 0x4a),
            .background = QColor(0x24, 0x24, 0x24),
            .darkBackground = QColor(0x1e, 0x1e, 0x1e),
            .darkerBackground = QColor(0x1b, 0x1b, 0x1b),
            .lighterBackground = QColor(0x30, 0x30, 0x30),
            .foreground = QColor(0xe5, 0xe5, 0xe5),
            .darkForeground = QColor(0x8a, 0x8a, 0x8a),
            .lightForeground = QColor(0xcc, 0xcc, 0xcc),
            .brightForeground = QColor(0xff, 0xff, 0xff),
            .red = QColor(0xff, 0x45, 0x3a),
            .warning = QColor(0xff, 0x9f, 0x0a)};
}

QPalette OmarchyColors::palette() const
{
    QPalette result;
    result.setColor(QPalette::Window, background);
    result.setColor(QPalette::WindowText, foreground);
    result.setColor(QPalette::Base, darkerBackground);
    result.setColor(QPalette::AlternateBase, darkBackground);
    result.setColor(QPalette::Text, foreground);
    result.setColor(QPalette::Button, lighterBackground);
    result.setColor(QPalette::ButtonText, foreground);
    result.setColor(QPalette::Highlight, accent);
    result.setColor(QPalette::HighlightedText, background);
    result.setColor(QPalette::Link, accent);
    result.setColor(QPalette::PlaceholderText, darkForeground);
    result.setColor(QPalette::BrightText, warning);
    result.setColor(QPalette::Mid, muted);
    result.setColor(QPalette::Dark, darkerBackground);
    result.setColor(QPalette::Light, lighterBackground);
    result.setColor(QPalette::ToolTipBase, darkBackground);
    result.setColor(QPalette::ToolTipText, foreground);
    for (const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        result.setColor(QPalette::Disabled, role, muted);
    return result;
}

QString OmarchyTheme::defaultDirectory()
{
    return QDir::homePath() + QStringLiteral("/.local/state/omarchy/current");
}

OmarchyTheme::OmarchyTheme(const QString &directory, QObject *parent)
    : QObject(parent), m_directory(directory), m_colors(OmarchyColors::builtInDark()), m_watcher(this), m_settle(this)
{
    // Fusion follows the palette on every desktop.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    // A switch removes, moves, writes: one load after the last.
    m_settle.setSingleShot(true);
    m_settle.setInterval(100);
    connect(&m_settle, &QTimer::timeout, this, &OmarchyTheme::apply);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_settle, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_settle, qOverload<>(&QTimer::start));
    // The built-in dark stands until a file is read whole.
    QApplication::setPalette(m_colors.palette());
    apply();
}

void OmarchyTheme::apply()
{
    const QString path = m_directory + QStringLiteral("/theme/colors.toml");
    watch();
    QFile file(path);
    OmarchyColors loaded = OmarchyColors::builtInDark();
    // Only a path leading nowhere is absent; barred keeps.
    struct stat status;
    if (::stat(QFile::encodeName(path).constData(), &status) != 0 && (errno == ENOENT || errno == ENOTDIR)) {
        qCInfo(lcApp).noquote() << "no Omarchy theme at" << path << "- built-in dark";
    } else if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcApp).noquote() << "Omarchy theme unreadable, keeping the palette:" << file.errorString() << path;
        return;
    } else {
        // A read cut short must not pass for the file.
        const QByteArray bytes = file.readAll();
        if (file.error() != QFileDevice::NoError || bytes.size() != file.size()) {
            qCWarning(lcApp).noquote() << "Omarchy theme unreadable, keeping the palette: read" << bytes.size() << "of" << file.size() << "bytes" << path;
            return;
        }
        try {
            loaded = OmarchyColors::parse(QString::fromUtf8(bytes));
        } catch (const std::runtime_error &error) {
            qCWarning(lcApp).noquote() << "Omarchy theme unreadable, keeping the palette:" << error.what();
            return;
        }
        QFile name(m_directory + QStringLiteral("/theme.name"));
        const QString called = name.open(QIODevice::ReadOnly) ? QString::fromUtf8(name.readAll()).trimmed() : QStringLiteral("(unnamed)");
        qCInfo(lcApp).noquote() << "Omarchy theme" << called << (loaded.dark ? "dark" : "light") << "from" << path;
    }
    if (loaded == m_colors)
        return;
    m_colors = loaded;
    QApplication::setPalette(m_colors.palette());
}

void OmarchyTheme::watch()
{
    // Removed paths leave the watcher: add them back each load.
    QStringList wanted{m_directory, m_directory + QStringLiteral("/theme"), m_directory + QStringLiteral("/theme/colors.toml")};
    // A file's watch follows its inode: renew it each load.
    if (m_watcher.files().contains(wanted.last()))
        m_watcher.removePath(wanted.last());
    for (const QString &path : wanted) {
        if (!m_watcher.files().contains(path) && !m_watcher.directories().contains(path))
            m_watcher.addPath(path);
    }
    // Missing or barred, the state directory's nearest watchable ancestor tells.
    QString ancestor = m_directory;
    while (!m_watcher.directories().contains(m_directory) && ancestor.contains(QLatin1Char('/'))) {
        ancestor = ancestor.section(QLatin1Char('/'), 0, -2);
        const QString candidate = ancestor.isEmpty() ? QStringLiteral("/") : ancestor;
        if (m_watcher.directories().contains(candidate) || m_watcher.addPath(candidate)) {
            wanted.prepend(candidate);
            break;
        }
    }
    for (const QString &stale : m_watcher.directories()) {
        if (!wanted.contains(stale))
            m_watcher.removePath(stale);
    }
}
