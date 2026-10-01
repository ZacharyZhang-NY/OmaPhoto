#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "Document/ToolDefaults.h"
#include <QSettings>
#include <QStandardPaths>
#include <QtTest>

// Swift 1.3.5's GuideTests for Grid Settings, and their keeping.
class GuideGridTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void layoutGridTakesItsSpacingAndSubdivisions();
    void layoutGridKeepsToItsLimits();
    void gridAppearanceColorsAndStyles();
    void gridSnapFollowsTheGridSettings();
    void theSettingsAreThePersonsAcrossLaunches();
};

void GuideGridTests::layoutGridTakesItsSpacingAndSubdivisions()
{
    const LayoutGrid grid(100, 4);
    QCOMPARE(grid.lines(200), (std::vector<double>{0, 25, 50, 75, 100, 125, 150, 175, 200}));
    QVERIFY(grid.isMajor(100) && !grid.isMajor(50));
    QCOMPARE(grid.step(), 25.0);
    // An uneven step still lands on every major line.
    const LayoutGrid thirds(100, 3);
    std::vector<double> majors;
    for (const double line : thirds.lines(300))
        if (thirds.isMajor(line))
            majors.push_back(line);
    QCOMPARE(majors, (std::vector<double>{0, 100, 200, 300}));
    QCOMPARE(thirds.lines(100), (std::vector<double>{0, 33, 67, 100}));
    QCOMPARE(LayoutGrid(50, 1).lines(120), (std::vector<double>{0, 50, 100}));
    // A hair short of a line counts it: Swift's 0.001.
    QCOMPARE(LayoutGrid(50, 1).lines(99.96), (std::vector<double>{0, 50, 100}));
    QCOMPARE(LayoutGrid(50, 1).lines(99.9), (std::vector<double>{0, 50}));
}

void GuideGridTests::layoutGridKeepsToItsLimits()
{
    QCOMPARE(LayoutGrid(0, 0), LayoutGrid(2, 1));
    QCOMPARE(LayoutGrid(10, 40).subdivisions, 10);
    QCOMPARE(LayoutGrid(1'000'000, 1'000).spacing, LayoutGrid::spacingHigh);
    QCOMPARE(LayoutGrid(1'000'000, 1'000).subdivisions, LayoutGrid::subdivisionHigh);
    QVERIFY(LayoutGrid::spacingLow == 2 && LayoutGrid::spacingHigh == 4096 && LayoutGrid::subdivisionLow == 1 && LayoutGrid::subdivisionHigh == 64);
    QVERIFY(LayoutGrid().spacing == 64 && LayoutGrid().subdivisions == 8);
}

void GuideGridTests::gridAppearanceColorsAndStyles()
{
    const GridAppearance standard;
    QVERIFY(standard.preset == GridAppearance::Preset::lightGray && standard.style == GridAppearance::Style::lines && standard.opacity == 45);
    QCOMPARE(standard.color(), (PaletteColor{0.7, 0.7, 0.7}));
    QVERIFY(dashes(GridAppearance::Style::lines).empty());
    QCOMPARE(dashes(GridAppearance::Style::dashedLines), (std::vector<double>{4, 3}));
    QCOMPARE(dashes(GridAppearance::Style::dots), (std::vector<double>{1, 2}));
    GridAppearance appearance{GridAppearance::Preset::cyan, PaletteColor::black(), GridAppearance::Style::dots};
    QCOMPARE(appearance.color(), (PaletteColor{0, 1, 1}));
    appearance.preset = GridAppearance::Preset::custom;
    QCOMPARE(appearance.color(), PaletteColor::black());
    for (const GridAppearance::Preset preset : allGridPresets)
        QCOMPARE(!presetColor(preset), preset == GridAppearance::Preset::custom);
    QCOMPARE(PaletteColor::fromHex(appearance.customColor.hex()), std::optional(appearance.customColor));
    QVERIFY(std::abs(standard.majorAlpha() - 0.45) < 0.001 && std::abs(standard.subdivisionAlpha() - 0.28) < 0.001);
    appearance.opacity = 100;
    QVERIFY(appearance.majorAlpha() == 1 && appearance.subdivisionAlpha() < 1);
    appearance.opacity = 150;
    QCOMPARE(appearance.majorAlpha(), 1.0);
    appearance.opacity = 0;
    QCOMPARE(appearance.majorAlpha(), 0.01);
    // Photoshop's names and colours, in Swift's order.
    QStringList names;
    for (const GridAppearance::Preset preset : allGridPresets)
        names << rawValue(preset);
    QCOMPARE(names, QStringList({"Light Gray", "Light Blue", "Light Red", "Green", "Medium Blue", "Yellow", "Magenta", "Cyan", "Black", "Custom"}));
    QVERIFY(presetColor(GridAppearance::Preset::lightBlue) == (PaletteColor{0.29, 0.78, 1}) && presetColor(GridAppearance::Preset::lightRed) == (PaletteColor{1, 0.4, 0.4}));
    QVERIFY(presetColor(GridAppearance::Preset::green) == (PaletteColor{0.25, 0.8, 0.25}) && presetColor(GridAppearance::Preset::mediumBlue) == (PaletteColor{0.2, 0.4, 1}));
    QVERIFY(presetColor(GridAppearance::Preset::yellow) == (PaletteColor{1, 1, 0}) && presetColor(GridAppearance::Preset::magenta) == (PaletteColor{1, 0, 1}));
    QCOMPARE(presetColor(GridAppearance::Preset::black), std::optional(PaletteColor::black()));
    QStringList styles;
    for (const GridAppearance::Style style : allGridStyles)
        styles << rawValue(style);
    QCOMPARE(styles, QStringList({"Lines", "Dashed Lines", "Dots"}));
}

void GuideGridTests::gridSnapFollowsTheGridSettings()
{
    EditorSession session;
    session.createDocument(400, 300);
    session.setSnapToLayers(false);
    session.setSnapToDocumentBounds(false);
    session.setShowsGrid(true);
    session.setSnapToGrid(true);
    QSignalSpy changed(&session, &EditorSession::changed);
    session.setLayoutGrid(LayoutGrid(100, 2));
    QCOMPARE(changed.count(), 1);
    std::vector<double> xs = session.cropSnapTargets().xs;
    std::sort(xs.begin(), xs.end());
    QCOMPARE(xs, (std::vector<double>{0, 50, 100, 150, 200, 250, 300, 350, 400}));
    QCOMPARE(session.snappedGuidePosition(52, CanvasGuide::Axis::vertical, std::nullopt), 50.0);
    // The same grid or look again is no news.
    session.setLayoutGrid(LayoutGrid(100, 2));
    session.setGridAppearance(GridAppearance());
    QCOMPARE(changed.count(), 1);
    GridAppearance dashed;
    dashed.style = GridAppearance::Style::dashedLines;
    session.setGridAppearance(dashed);
    QVERIFY(changed.count() == 2 && session.gridAppearance() == dashed);
}

void GuideGridTests::theSettingsAreThePersonsAcrossLaunches()
{
    // Tests keep compiled defaults until the app turns them on.
    QSettings().clear();
    ToolDefaults::enable();
    QCOMPARE(EditorSession().layoutGrid(), LayoutGrid());
    QCOMPARE(EditorSession().gridAppearance(), GridAppearance());
    GridAppearance look{GridAppearance::Preset::custom, PaletteColor{0.2, 0.4, 0.6}, GridAppearance::Style::dots, 70};
    {
        EditorSession session;
        session.setLayoutGrid(LayoutGrid(100, 5));
        session.setGridAppearance(look);
    }
    // Swift's keys, so a person's settings read as Swift's.
    QSettings settings;
    QVERIFY(settings.value("tool.gridSpacing").toInt() == 100 && settings.value("tool.gridSubdivisions").toInt() == 5);
    QVERIFY(settings.value("tool.gridColor").toString() == "Custom" && settings.value("tool.gridStyle").toString() == "Dots");
    QVERIFY(settings.value("tool.gridCustomColor").toString() == look.customColor.hex() && settings.value("tool.gridOpacity").toInt() == 70);
    EditorSession next;
    QVERIFY(next.layoutGrid() == LayoutGrid(100, 5) && next.gridAppearance() == look);
    // What cannot be read is warned of, then defaulted.
    settings.setValue("tool.gridSpacing", "wide");
    settings.setValue("tool.gridColor", "Plaid");
    settings.setValue("tool.gridStyle", "Wavy");
    settings.setValue("tool.gridCustomColor", "#12");
    for (int warning = 0; warning < 4; ++warning)
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("ignoring the tool setting grid"));
    EditorSession unread;
    QCOMPARE(unread.layoutGrid(), LayoutGrid(64, 5));
    QVERIFY(unread.gridAppearance().preset == GridAppearance::Preset::lightGray && unread.gridAppearance().style == GridAppearance::Style::lines);
    QCOMPARE(unread.gridAppearance().customColor, GridAppearance().customColor);
    QCOMPARE(unread.gridAppearance().opacity, 70);
    QSettings().clear();
}

QTEST_GUILESS_MAIN(GuideGridTests)
#include "GuideGridTests.moc"
