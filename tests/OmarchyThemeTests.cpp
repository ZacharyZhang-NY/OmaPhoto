#include "UI/OmarchyTheme.h"
#include "Document/ProjectWorkspace.h"
#include "UI/ProjectWorkspaceView.h"
#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QMessageBox>
#include <QStyle>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QToolButton>
#include <cstdio>
#include <sys/syscall.h>
#include <unistd.h>
#include <QtTest>

// The desktop's theme: read, applied, followed through a switch.
namespace {
const char *latte = R"(# Catppuccin Latte
mode = "light"

accent = "#1e66f5"        # blue
selection = "#ccd0da"
muted = "#acb0be"
background = "#eff1f5"
dark_background = "#e3e4e8"
darker_background = "#d7d8dc"
lighter_background = "#dce0e8"
foreground = "#4c4f69"
dark_foreground = "#9ca0b0"
light_foreground = "#5c5f77"
bright_foreground = "#4c4f69"
red = "#d20f39"
yellow = "#df8e1d"
orange = "#d84e2b"
green = "#40a02b"
)";

const char *macchiato = R"(mode = "dark"
accent = "#8aadf4"
selection = "#494d64"
muted = "#5b6078"
background = "#24273a"
dark_background = "#1e2030"
darker_background = "#181926"
lighter_background = "#363a4f"
foreground = "#cad3f5"
dark_foreground = "#6e738d"
light_foreground = "#b8c0e0"
bright_foreground = "#cad3f5"
red = "#ed8796"
yellow = "#eed49f"
)";

void write(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(text.toUtf8()) < 0)
        throw std::runtime_error("could not write " + path.toStdString());
}

// While set, a colors.toml read gives 64 bytes, then fails.
bool shortReads = false;

// A window filled with the palette's window colour.
struct Swatch {
    QLabel label;
    Swatch()
    {
        label.setAutoFillBackground(true);
        label.resize(40, 40);
        label.show();
    }
    QRgb pixel() { return label.grab().toImage().pixel(20, 20); }
};
}

extern "C" ssize_t read(int descriptor, void *buffer, size_t count)
{
    char path[512] = {};
    const QByteArray link = "/proc/self/fd/" + QByteArray::number(descriptor);
    const bool theme = shortReads && readlink(link.constData(), path, sizeof(path) - 1) > 0 && QByteArray(path).endsWith("colors.toml");
    static int reads = 0;
    if (theme && ++reads > 1) {
        errno = EIO;
        return -1;
    }
    if (!theme)
        reads = 0;
    return syscall(SYS_read, descriptor, buffer, theme ? std::min<size_t>(count, 64) : count);
}

class OmarchyThemeTests : public QObject {
    Q_OBJECT
private slots:
    void theFileParsesIntoColoursAndRefusesWhatIsMissingOrMalformed();
    void lightAndDarkThemesFillThePalette();
    void withoutAThemeTheBuiltInDarkApplies();
    void aSwitchRetintsTheWindowsWhileTheyShow();
    void anUnreadableSwitchKeepsThePalette();
    void aMalformedFileAtStartupLeavesTheBuiltInDark();
    void aFileThatCannotBeOpenedKeepsThePalette();
    void anAbsentStateDirectoryIsWatchedForItsBirth();
    void aBarredStateDirectoryIsWatchedFromAbove();
    void aReadCutShortKeepsThePalette();
    void aSwitchReachesTheWindowsTabsAndCanvas();
};

void OmarchyThemeTests::theFileParsesIntoColoursAndRefusesWhatIsMissingOrMalformed()
{
    const OmarchyColors colours = OmarchyColors::parse(QString::fromLatin1(latte));
    QVERIFY(!colours.dark);
    QCOMPARE(colours.accent, QColor(0x1e, 0x66, 0xf5));
    QCOMPARE(colours.selection, QColor(0xcc, 0xd0, 0xda));
    QCOMPARE(colours.muted, QColor(0xac, 0xb0, 0xbe));
    QCOMPARE(colours.background, QColor(0xef, 0xf1, 0xf5));
    QCOMPARE(colours.darkBackground, QColor(0xe3, 0xe4, 0xe8));
    QCOMPARE(colours.darkerBackground, QColor(0xd7, 0xd8, 0xdc));
    QCOMPARE(colours.lighterBackground, QColor(0xdc, 0xe0, 0xe8));
    QCOMPARE(colours.foreground, QColor(0x4c, 0x4f, 0x69));
    QCOMPARE(colours.darkForeground, QColor(0x9c, 0xa0, 0xb0));
    QCOMPARE(colours.lightForeground, QColor(0x5c, 0x5f, 0x77));
    QCOMPARE(colours.brightForeground, QColor(0x4c, 0x4f, 0x69));
    QCOMPARE(colours.red, QColor(0xd2, 0x0f, 0x39));
    // Orange is the warning where present, else yellow.
    QCOMPARE(colours.warning, QColor(0xd8, 0x4e, 0x2b));
    const OmarchyColors dark = OmarchyColors::parse(QString::fromLatin1(macchiato));
    QVERIFY(dark.dark);
    QCOMPARE(dark.warning, QColor(0xee, 0xd4, 0x9f));
    QCOMPARE(dark.background, QColor(0x24, 0x27, 0x3a));
    const auto refused = [](const QString &toml) {
        try {
            OmarchyColors::parse(toml);
        } catch (const std::runtime_error &error) {
            return QString::fromLatin1(error.what());
        }
        return QString();
    };
    QVERIFY(refused(QString::fromLatin1(macchiato).replace("mode = \"dark\"", "mode = \"dim\"")).contains("mode"));
    QVERIFY(refused(QString::fromLatin1(macchiato).replace("accent = \"#8aadf4\"\n", "")).contains("accent"));
    QVERIFY(refused(QString::fromLatin1(macchiato).replace("#ed8796", "#ed879")).contains("red"));
    QVERIFY(refused(QString::fromLatin1(macchiato).replace("#ed8796", "ed8796")).contains("red"));
    QVERIFY(refused(QString()).contains("mode"));
    // Single quotes read the same.
    QCOMPARE(OmarchyColors::parse(QString::fromLatin1(latte).replace('"', '\'')).accent, QColor(0x1e, 0x66, 0xf5));
    QCOMPARE(OmarchyColors::parse(QString::fromLatin1(macchiato).replace('"', '\'')).dark, true);
    QVERIFY(refused(QString::fromLatin1(macchiato).replace("\"dark\"", "'dark\"")).contains("mode"));
    // Quoted keys read too; bare or half-quoted values refuse.
    QCOMPARE(OmarchyColors::parse(QString::fromLatin1(macchiato).replace("mode = ", "\"mode\" = ")).dark, true);
    QCOMPARE(OmarchyColors::parse(QString::fromLatin1(macchiato).replace("accent = ", "'accent' = ")).accent, QColor(0x8a, 0xad, 0xf4));
    QVERIFY(refused(QString::fromLatin1(macchiato).replace("mode = \"dark\"", "mode = dark")).contains("mode is no quoted"));
    QVERIFY(refused(QString::fromLatin1(latte).replace("orange = \"#d84e2b\"", "orange = 123")).contains("orange is no quoted"));
    QVERIFY(refused(QString::fromLatin1(latte).replace("orange = \"#d84e2b\"", "orange = \"#d84e2b'")).contains("orange"));
    QVERIFY(refused(QString::fromLatin1(latte).replace("red = \"#d20f39\"", "red = \"#d20f39\" \"twice\"")).contains("red is no quoted"));
    // A trailing comment and a bare unknown key are fine.
    QCOMPARE(OmarchyColors::parse(QString::fromLatin1(macchiato) + "hyprland_border = 1\n").red, QColor(0xed, 0x87, 0x96));
    // A later line wins; an unknown key is ignored.
    const OmarchyColors twice = OmarchyColors::parse(QString::fromLatin1(macchiato) + "\nred = \"#000000\"\nhyprland_active_border = \"#123456\"\n");
    QCOMPARE(twice.red, QColor(0, 0, 0));
}

void OmarchyThemeTests::lightAndDarkThemesFillThePalette()
{
    const QPalette light = OmarchyColors::parse(QString::fromLatin1(latte)).palette();
    QCOMPARE(light.color(QPalette::Window), QColor(0xef, 0xf1, 0xf5));
    QCOMPARE(light.color(QPalette::WindowText), QColor(0x4c, 0x4f, 0x69));
    QCOMPARE(light.color(QPalette::Base), QColor(0xd7, 0xd8, 0xdc));
    QCOMPARE(light.color(QPalette::AlternateBase), QColor(0xe3, 0xe4, 0xe8));
    QCOMPARE(light.color(QPalette::Text), QColor(0x4c, 0x4f, 0x69));
    QCOMPARE(light.color(QPalette::Button), QColor(0xdc, 0xe0, 0xe8));
    QCOMPARE(light.color(QPalette::ButtonText), QColor(0x4c, 0x4f, 0x69));
    QCOMPARE(light.color(QPalette::Highlight), QColor(0x1e, 0x66, 0xf5));
    QCOMPARE(light.color(QPalette::HighlightedText), QColor(0xef, 0xf1, 0xf5));
    QCOMPARE(light.color(QPalette::Link), QColor(0x1e, 0x66, 0xf5));
    QCOMPARE(light.color(QPalette::PlaceholderText), QColor(0x9c, 0xa0, 0xb0));
    QCOMPARE(light.color(QPalette::BrightText), QColor(0xd8, 0x4e, 0x2b));
    QCOMPARE(light.color(QPalette::Mid), QColor(0xac, 0xb0, 0xbe));
    QCOMPARE(light.color(QPalette::Dark), QColor(0xd7, 0xd8, 0xdc));
    QCOMPARE(light.color(QPalette::Light), QColor(0xdc, 0xe0, 0xe8));
    QCOMPARE(light.color(QPalette::ToolTipBase), QColor(0xe3, 0xe4, 0xe8));
    QCOMPARE(light.color(QPalette::ToolTipText), QColor(0x4c, 0x4f, 0x69));
    for (const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        QCOMPARE(light.color(QPalette::Disabled, role), QColor(0xac, 0xb0, 0xbe));
    const QPalette dark = OmarchyColors::parse(QString::fromLatin1(macchiato)).palette();
    QCOMPARE(dark.color(QPalette::Window), QColor(0x24, 0x27, 0x3a));
    QCOMPARE(dark.color(QPalette::HighlightedText), QColor(0x24, 0x27, 0x3a));
    QCOMPARE(dark.color(QPalette::BrightText), QColor(0xee, 0xd4, 0x9f));
    QCOMPARE(dark.color(QPalette::PlaceholderText), QColor(0x6e, 0x73, 0x8d));
}

void OmarchyThemeTests::withoutAThemeTheBuiltInDarkApplies()
{
    QTemporaryDir state;
    // Whatever style the desktop set, the palette needs Fusion.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Windows")));
    QCOMPARE(QApplication::style()->objectName(), QString("windows"));
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression("no Omarchy theme at .*colors.toml - built-in dark"));
    OmarchyTheme theme(state.path());
    QCOMPARE(theme.colors(), OmarchyColors::builtInDark());
    QCOMPARE(QApplication::palette().color(QPalette::Window), QColor(0x24, 0x24, 0x24));
    QCOMPARE(QApplication::palette().color(QPalette::Base), QColor(0x1b, 0x1b, 0x1b));
    QCOMPARE(QApplication::palette().color(QPalette::PlaceholderText), QColor(0x8a, 0x8a, 0x8a));
    QCOMPARE(QApplication::palette().color(QPalette::BrightText), QColor(0xff, 0x9f, 0x0a));
    QCOMPARE(QApplication::style()->objectName(), QString("fusion"));
    // Swift's d62a132: alerts are dark too.
    QMessageBox alert(QMessageBox::Warning, QStringLiteral("Title"), QStringLiteral("Text"));
    QCOMPARE(alert.palette().color(QPalette::Window), QColor(0x24, 0x24, 0x24));
    QCOMPARE(alert.style()->objectName(), QString("fusion"));
    Swatch swatch;
    QCOMPARE(swatch.pixel(), qRgb(0x24, 0x24, 0x24));
    // A theme appearing later is taken up, unnamed or not.
    QTest::failOnWarning(QRegularExpression("QIODevice"));
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression("Omarchy theme \\(unnamed\\) light from"));
    write(state.path() + "/theme/colors.toml", QString::fromLatin1(latte));
    QTRY_COMPARE(swatch.pixel(), qRgb(0xef, 0xf1, 0xf5));
}

void OmarchyThemeTests::aSwitchRetintsTheWindowsWhileTheyShow()
{
    QTemporaryDir state;
    write(state.path() + "/theme/colors.toml", QString::fromLatin1(macchiato));
    write(state.path() + "/theme.name", "catppuccin-macchiato\n");
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression("Omarchy theme catppuccin-macchiato dark from .*colors.toml"));
    OmarchyTheme theme(state.path());
    Swatch swatch;
    QCOMPARE(swatch.pixel(), qRgb(0x24, 0x27, 0x3a));
    // omarchy-theme-set: remove, move the next in, write the name.
    write(state.path() + "/next-theme/colors.toml", QString::fromLatin1(latte));
    QVERIFY(QDir(state.path() + "/theme").removeRecursively());
    QVERIFY(QDir().rename(state.path() + "/next-theme", state.path() + "/theme"));
    write(state.path() + "/theme.name", "catppuccin-latte\n");
    QTRY_COMPARE(swatch.pixel(), qRgb(0xef, 0xf1, 0xf5));
    QVERIFY(!theme.colors().dark);
    QCOMPARE(QApplication::palette().color(QPalette::Highlight), QColor(0x1e, 0x66, 0xf5));
    // Edited in place the file counts too, twice running.
    write(state.path() + "/theme/colors.toml", QString::fromLatin1(macchiato));
    QTRY_COMPARE(swatch.pixel(), qRgb(0x24, 0x27, 0x3a));
    write(state.path() + "/theme/colors.toml", QString::fromLatin1(latte));
    QTRY_COMPARE(swatch.pixel(), qRgb(0xef, 0xf1, 0xf5));
    // Replaced under a held handle, then edited: still seen.
    QFile held(state.path() + "/theme/colors.toml");
    QVERIFY(held.open(QIODevice::ReadOnly));
    write(state.path() + "/theme/next.toml", QString::fromLatin1(macchiato));
    QCOMPARE(::rename(QFile::encodeName(state.path() + "/theme/next.toml").constData(), QFile::encodeName(state.path() + "/theme/colors.toml").constData()), 0);
    QTRY_COMPARE(swatch.pixel(), qRgb(0x24, 0x27, 0x3a));
    write(state.path() + "/theme/colors.toml", QString::fromLatin1(latte));
    QTRY_COMPARE(swatch.pixel(), qRgb(0xef, 0xf1, 0xf5));
}

void OmarchyThemeTests::anUnreadableSwitchKeepsThePalette()
{
    QTemporaryDir state;
    write(state.path() + "/theme/colors.toml", QString::fromLatin1(macchiato));
    OmarchyTheme theme(state.path());
    Swatch swatch;
    QCOMPARE(swatch.pixel(), qRgb(0x24, 0x27, 0x3a));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Omarchy theme unreadable, keeping the palette: colors.toml lacks accent"));
    write(state.path() + "/theme/colors.toml", QString::fromLatin1(macchiato).replace("accent = \"#8aadf4\"\n", ""));
    QTest::qWait(400);
    QCOMPARE(swatch.pixel(), qRgb(0x24, 0x27, 0x3a));
    QVERIFY(theme.colors().dark);
    // Mended, it applies again.
    write(state.path() + "/theme/colors.toml", QString::fromLatin1(latte));
    QTRY_COMPARE(swatch.pixel(), qRgb(0xef, 0xf1, 0xf5));
}

void OmarchyThemeTests::aMalformedFileAtStartupLeavesTheBuiltInDark()
{
    QTemporaryDir state;
    write(state.path() + "/theme/colors.toml", QString::fromLatin1(macchiato).replace("mode = \"dark\"\n", ""));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Omarchy theme unreadable, keeping the palette: colors.toml lacks mode"));
    OmarchyTheme theme(state.path());
    QCOMPARE(theme.colors(), OmarchyColors::builtInDark());
    Swatch swatch;
    QCOMPARE(swatch.pixel(), qRgb(0x24, 0x24, 0x24));
    write(state.path() + "/theme/colors.toml", QString::fromLatin1(latte));
    QTRY_COMPARE(swatch.pixel(), qRgb(0xef, 0xf1, 0xf5));
}

void OmarchyThemeTests::aFileThatCannotBeOpenedKeepsThePalette()
{
    QTemporaryDir state;
    const QString path = state.path() + "/theme/colors.toml";
    write(path, QString::fromLatin1(latte));
    OmarchyTheme theme(state.path());
    Swatch swatch;
    QCOMPARE(swatch.pixel(), qRgb(0xef, 0xf1, 0xf5));
    // A file that will not open is no absent file.
    QVERIFY(QFile::setPermissions(path, QFileDevice::Permissions()));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Omarchy theme unreadable, keeping the palette: .*colors.toml"));
    theme.apply();
    QCOMPARE(QApplication::palette().color(QPalette::Window), QColor(0xef, 0xf1, 0xf5));
    QVERIFY(!theme.colors().dark);
    QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    // An unsearchable folder hides the file: kept too.
    const QString folder = state.path() + "/theme";
    QVERIFY(QFile::setPermissions(folder, QFileDevice::Permissions()));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Omarchy theme unreadable, keeping the palette: .*colors.toml"));
    theme.apply();
    QCOMPARE(QApplication::palette().color(QPalette::Window), QColor(0xef, 0xf1, 0xf5));
    QVERIFY(QFile::setPermissions(folder, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
}

void OmarchyThemeTests::anAbsentStateDirectoryIsWatchedForItsBirth()
{
    QTemporaryDir root;
    const QString state = root.path() + "/omarchy/current";
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression("no Omarchy theme at .*colors.toml - built-in dark"));
    OmarchyTheme theme(state);
    Swatch swatch;
    QCOMPARE(swatch.pixel(), qRgb(0x24, 0x24, 0x24));
    // Omarchy installed later: the theme arrives with its folders.
    auto &watcher = *theme.findChild<QFileSystemWatcher *>();
    QCOMPARE(watcher.directories(), QStringList{root.path()});
    write(state + "/theme/colors.toml", QString::fromLatin1(latte));
    QTRY_COMPARE(swatch.pixel(), qRgb(0xef, 0xf1, 0xf5));
    // Found, the state directory is watched, its ancestor let go.
    QVERIFY(watcher.directories().contains(state) && !watcher.directories().contains(root.path()));
    write(state + "/theme/colors.toml", QString::fromLatin1(macchiato));
    QTRY_COMPARE(swatch.pixel(), qRgb(0x24, 0x27, 0x3a));
}

void OmarchyThemeTests::aBarredStateDirectoryIsWatchedFromAbove()
{
    QTemporaryDir root;
    const QString state = root.path() + "/current";
    write(state + "/theme/colors.toml", QString::fromLatin1(latte));
    QVERIFY(QFile::setPermissions(state, QFileDevice::Permissions()));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Omarchy theme unreadable, keeping the palette: .*colors.toml"));
    OmarchyTheme theme(state);
    Swatch swatch;
    QCOMPARE(swatch.pixel(), qRgb(0x24, 0x24, 0x24));
    auto &watcher = *theme.findChild<QFileSystemWatcher *>();
    QCOMPARE(watcher.directories(), QStringList{root.path()});
    // Opened up again, the theme is found and followed.
    QVERIFY(QFile::setPermissions(state, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    QTRY_COMPARE(swatch.pixel(), qRgb(0xef, 0xf1, 0xf5));
    QTRY_VERIFY(watcher.directories().contains(state) && !watcher.directories().contains(root.path()));
    write(state + "/theme/colors.toml", QString::fromLatin1(macchiato));
    QTRY_COMPARE(swatch.pixel(), qRgb(0x24, 0x27, 0x3a));
}

void OmarchyThemeTests::aReadCutShortKeepsThePalette()
{
    QTemporaryDir state;
    write(state.path() + "/theme/colors.toml", QString::fromLatin1(latte));
    OmarchyTheme theme(state.path());
    Swatch swatch;
    QCOMPARE(swatch.pixel(), qRgb(0xef, 0xf1, 0xf5));
    // Sixty-four bytes, then an error: a prefix, not the file.
    write(state.path() + "/theme/colors.toml", QString::fromLatin1(macchiato));
    shortReads = true;
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Omarchy theme unreadable, keeping the palette: read 64 of [0-9]+ bytes"));
    theme.apply();
    QCOMPARE(QApplication::palette().color(QPalette::Window), QColor(0xef, 0xf1, 0xf5));
    QCOMPARE(swatch.pixel(), qRgb(0xef, 0xf1, 0xf5));
    shortReads = false;
    theme.apply();
    QCOMPARE(QApplication::palette().color(QPalette::Window), QColor(0x24, 0x27, 0x3a));
    // Widgets take the new palette from the event loop.
    QTRY_COMPARE(swatch.pixel(), qRgb(0x24, 0x27, 0x3a));
}

void OmarchyThemeTests::aSwitchReachesTheWindowsTabsAndCanvas()
{
    QTemporaryDir state;
    write(state.path() + "/theme/colors.toml", QString::fromLatin1(macchiato));
    OmarchyTheme theme(state.path());
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    // The lightest or darkest pixel of a widget: its text.
    const auto extreme = [](QWidget &widget, bool lightest) {
        const QImage image = widget.grab().toImage();
        int best = lightest ? 0 : 255;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x)
                best = lightest ? std::max(best, qGray(image.pixel(x, y))) : std::min(best, qGray(image.pixel(x, y)));
        }
        return best;
    };
    auto &tab = *window.findChild<QToolButton *>("selectTab");
    auto &canvas = *window.findChild<QWidget *>("editorCanvas");
    QVERIFY(std::abs(extreme(tab, true) - qGray(qRgb(0xca, 0xd3, 0xf5))) <= 12);
    QCOMPARE(canvas.grab().toImage().pixelColor(5, 5), QColor(0x18, 0x19, 0x26));
    // Light: the tab's title turns dark, the canvas surround light.
    write(state.path() + "/theme/colors.toml", QString::fromLatin1(latte));
    QTRY_COMPARE(canvas.grab().toImage().pixelColor(5, 5), QColor(0xd7, 0xd8, 0xdc));
    QVERIFY(std::abs(extreme(tab, false) - qGray(qRgb(0x4c, 0x4f, 0x69))) <= 12);
    // The row's margin shows the toolbar, not a fill.
    auto &strip = *window.findChild<QWidget *>("projectTabs");
    const QImage bar = window.grab().toImage();
    const QPoint inRow = strip.mapTo(&window, QPoint(6, 1)), beside = strip.mapTo(&window, QPoint(strip.width() + 8, 1));
    QCOMPARE(bar.pixelColor(inRow), bar.pixelColor(beside));
}

QTEST_MAIN(OmarchyThemeTests)
#include "OmarchyThemeTests.moc"
