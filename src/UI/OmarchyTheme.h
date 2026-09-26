#pragma once
#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QPalette>
#include <QTimer>

// Omarchy's colors.toml, read into the colours the chrome uses.
struct OmarchyColors {
    bool dark;
    QColor accent;
    QColor selection;
    QColor muted;
    QColor background;
    QColor darkBackground;
    QColor darkerBackground;
    QColor lighterBackground;
    QColor foreground;
    QColor darkForeground;
    QColor lightForeground;
    QColor brightForeground;
    QColor red;
    // `orange` where the theme has it, else `yellow`.
    QColor warning;

    // Throws std::runtime_error naming the missing or malformed key.
    static OmarchyColors parse(const QString &toml);
    // The macOS look, for a desktop without Omarchy.
    static OmarchyColors builtInDark();
    QPalette palette() const;
    friend bool operator==(const OmarchyColors &, const OmarchyColors &) = default;
};

// Applies the current theme to the application and follows switches.
class OmarchyTheme : public QObject {
    Q_OBJECT
public:
    static QString defaultDirectory();
    explicit OmarchyTheme(const QString &directory = defaultDirectory(), QObject *parent = nullptr);

    const OmarchyColors &colors() const { return m_colors; }
    void apply();

private:
    void watch();

    const QString m_directory;
    OmarchyColors m_colors;
    QFileSystemWatcher m_watcher;
    QTimer m_settle;
};
