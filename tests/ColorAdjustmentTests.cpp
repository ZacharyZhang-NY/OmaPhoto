#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "IO/ProjectStore+Json.h"
#include "UI/FilterSheet.h"
#include <QCheckBox>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QtTest>
#include <cmath>

// Black & White, Color Balance: kernels, bounds, keys, sheet.
namespace {
QImage solid(QColor colour)
{
    QImage image(2, 2, QImage::Format_RGBA8888_Premultiplied);
    image.fill(colour);
    return image;
}

QColor first(const QImage &image)
{
    return image.pixelColor(0, 0);
}

bool near(const QColor &actual, const QColor &expected)
{
    return std::abs(actual.red() - expected.red()) <= 1 && std::abs(actual.green() - expected.green()) <= 1
        && std::abs(actual.blue() - expected.blue()) <= 1 && actual.alpha() == expected.alpha();
}

// Photoshop's tint: the gray as lightness at a hue, HSL.
QColor tinted(double gray, double hue, double saturation)
{
    const double c = (1 - std::abs(2 * gray - 1)) * saturation, h = hue / 60, x = c * (1 - std::abs(std::fmod(h, 2) - 1)), m = gray - c / 2;
    const std::array<double, 3> rgb = h < 1 ? std::array{c, x, 0.0} : h < 2 ? std::array{x, c, 0.0} : h < 3 ? std::array{0.0, c, x}
                                    : h < 4 ? std::array{0.0, x, c} : h < 5 ? std::array{x, 0.0, c} : std::array{c, 0.0, x};
    return QColor(int(std::lround((rgb[0] + m) * 255)), int(std::lround((rgb[1] + m) * 255)), int(std::lround((rgb[2] + m) * 255)));
}

struct Sheet {
    EditorSession session;
    std::unique_ptr<FilterSheet> sheet;
    explicit Sheet(FilterKind kind)
    {
        session.createDocument(4, 4);
        const QImage image = solid(QColor(120, 160, 200));
        session.insert(ImportedImage(image, image, QStringLiteral("Colour")));
        session.beginFilter(kind);
        sheet = std::make_unique<FilterSheet>(session);
    }
    const FilterSettings &settings() const { return session.filterEdit().value().settings; }
    QCheckBox &box(const QString &title) const
    {
        for (QCheckBox *each : sheet->findChildren<QCheckBox *>())
            if (each->text() == title)
                return *each;
        throw std::runtime_error("no such box");
    }
    QStringList labels() const
    {
        QStringList words;
        for (const QLabel *each : sheet->findChildren<QLabel *>())
            words << each->text();
        return words;
    }
};
}

class ColorAdjustmentTests : public QObject {
    Q_OBJECT
private slots:
    void blackWhiteWeighsEachFamilyOfColours();
    void blackWhiteTintsTheGrayItMakes();
    void colorBalanceShiftsEachToneAndCanKeepBrightness();
    void boundsFollowSwifts();
    void theManifestKeepsEveryKey();
    void theSheetShowsBlackAndWhitesFamiliesAndTint();
    void theSheetShowsColorBalancesThreeRanges();
    void theImageMenusFiltersChangeTheLayer();
    void aValuePastItsRangeIsExplained();
};

void ColorAdjustmentTests::blackWhiteWeighsEachFamilyOfColours()
{
    const BlackWhiteSettings defaults;
    // Photoshop's defaults: red at 40%, yellow 60%, blue 20%.
    QVERIFY(near(first(defaults.apply(solid(Qt::red))), QColor(102, 102, 102)));
    QVERIFY(near(first(defaults.apply(solid(Qt::yellow))), QColor(153, 153, 153)));
    QVERIFY(near(first(defaults.apply(solid(Qt::blue))), QColor(51, 51, 51)));
    QVERIFY(near(first(defaults.apply(solid(Qt::magenta))), QColor(204, 204, 204)));
    QVERIFY(near(first(defaults.apply(solid(Qt::white))), QColor(255, 255, 255)));
    // Gray keeps its level; a colour splits primary, secondary, gray.
    QVERIFY(near(first(defaults.apply(solid(QColor(90, 90, 90)))), QColor(90, 90, 90)));
    const double mixed = 0.2 + (0.6 - 0.2) * 0.6 + (1 - 0.6) * 0.4;
    const int level = int(std::lround(mixed * 255));
    QVERIFY(near(first(defaults.apply(solid(QColor::fromRgbF(1, 0.6f, 0.2f)))), QColor(level, level, level)));
    // Premultiplied pixels keep their alpha.
    QImage half(1, 1, QImage::Format_RGBA8888_Premultiplied);
    half.setPixel(0, 0, qRgba(128, 0, 0, 128));
    QCOMPARE(defaults.apply(half).pixel(0, 0), qRgba(51, 51, 51, 128));
    BlackWhiteSettings bright;
    bright.reds = 100;
    QVERIFY(near(first(bright.apply(solid(Qt::red))), QColor(255, 255, 255)));
    // Yellows and cyans reach the kernel each in its place.
    const BlackWhiteSettings apart{.yellows = 100, .cyans = 0};
    QVERIFY(near(first(apart.apply(solid(Qt::yellow))), QColor(255, 255, 255)));
    QVERIFY(near(first(apart.apply(solid(Qt::cyan))), QColor(0, 0, 0)));
}

void ColorAdjustmentTests::blackWhiteTintsTheGrayItMakes()
{
    BlackWhiteSettings settings;
    settings.tint = true;
    settings.tintHue = 30;
    settings.tintSaturation = 50;
    const double gray = 0.4;
    QVERIFY(near(first(settings.apply(solid(Qt::red))), tinted(gray, 30, 0.5)));
    settings.tintHue = 250;
    QVERIFY(near(first(settings.apply(solid(Qt::red))), tinted(gray, 250, 0.5)));
    // No saturation tints nothing.
    settings.tintSaturation = 0;
    QVERIFY(near(first(settings.apply(solid(Qt::red))), QColor(102, 102, 102)));
}

void ColorAdjustmentTests::colorBalanceShiftsEachToneAndCanKeepBrightness()
{
    // Untouched, the image is handed back as it is.
    const QImage gray = solid(QColor(128, 128, 128));
    QCOMPARE(ColorBalanceSettings().apply(gray).cacheKey(), gray.cacheKey());
    ColorBalanceSettings settings{.midCyanRed = 60, .preserveLuminosity = false};
    // A midtone weighs 0.7 in the midtones: red gains 0.42.
    QVERIFY(near(first(settings.apply(gray)), QColor(235, 128, 128)));
    // Shadows and highlights leave a midtone alone.
    QVERIFY(near(first(ColorBalanceSettings{.shadowCyanRed = 60, .preserveLuminosity = false}.apply(gray)), QColor(128, 128, 128)));
    QVERIFY(near(first(ColorBalanceSettings{.highlightCyanRed = 60, .preserveLuminosity = false}.apply(gray)), QColor(128, 128, 128)));
    // A shadow takes the shadows' shift.
    QVERIFY(near(first(ColorBalanceSettings{.shadowYellowBlue = 50, .preserveLuminosity = false}.apply(solid(QColor(20, 20, 20)))),
                 QColor(20, 20, 109)));
    // Preserve Luminosity scales the result back to its brightness.
    settings.preserveLuminosity = true;
    const QColor kept = first(settings.apply(gray));
    const double before = 128 / 255.0, after = 0.299 * (235 / 255.0) + 0.587 * before + 0.114 * before;
    QVERIFY(near(kept, QColor(int(std::lround(235 * before / after)), int(std::lround(128 * before / after)), int(std::lround(128 * before / after)))));
}

void ColorAdjustmentTests::boundsFollowSwifts()
{
    QVERIFY((BlackWhiteSettings{.reds = 300, .blues = -200}.isValid()));
    for (const BlackWhiteSettings &broken : {BlackWhiteSettings{.reds = 300.5}, BlackWhiteSettings{.magentas = -200.5},
                                            BlackWhiteSettings{.greens = std::nan("")}, BlackWhiteSettings{.tintHue = 360.5}, BlackWhiteSettings{.tintHue = -0.5},
                                            BlackWhiteSettings{.tintSaturation = -1}, BlackWhiteSettings{.tintSaturation = 100.5}}) {
        QVERIFY(!broken.isValid());
        QVERIFY_THROWS_EXCEPTION(ProjectError, broken.apply(solid(Qt::red)));
    }
    QVERIFY((ColorBalanceSettings{.shadowCyanRed = -100, .highlightYellowBlue = 100}.isValid()));
    for (const ColorBalanceSettings &broken : {ColorBalanceSettings{.midMagentaGreen = 100.5}, ColorBalanceSettings{.shadowYellowBlue = -100.5},
                                              ColorBalanceSettings{.highlightCyanRed = std::nan("")}}) {
        QVERIFY(!broken.isValid());
        QVERIFY_THROWS_EXCEPTION(ProjectError, broken.apply(solid(Qt::red)));
    }
    // An adjustment layer refuses settings of either past them.
    LayerAdjustment adjustment{AdjustmentKind::blackWhite};
    adjustment.setBlackWhite(BlackWhiteSettings{.cyans = 301});
    QVERIFY(!adjustment.isValid());
    adjustment = LayerAdjustment{AdjustmentKind::colorBalance};
    adjustment.setColorBalance(ColorBalanceSettings{.midYellowBlue = -101});
    QVERIFY(!adjustment.isValid());
}

void ColorAdjustmentTests::theManifestKeepsEveryKey()
{
    LayerAdjustment adjustment{AdjustmentKind::blackWhite};
    adjustment.setBlackWhite(BlackWhiteSettings{.yellows = 10, .tint = true});
    // Nine distinct amounts: no two keys can trade places unseen.
    adjustment.setColorBalance(ColorBalanceSettings{-40, -30, -20, -10, 10, 20, 30, 40, 50, false});
    const QJsonObject object = ManifestJson::encoded(adjustment);
    const QJsonObject gray = object.value("blackWhiteSettings").toObject(), balance = object.value("colorBalanceSettings").toObject();
    QCOMPARE(gray.keys(), (QStringList{"blues", "cyans", "greens", "magentas", "reds", "tint", "tintHue", "tintSaturation", "yellows"}));
    QCOMPARE(balance.keys(), (QStringList{"highlightCyanRed", "highlightMagentaGreen", "highlightYellowBlue", "midCyanRed", "midMagentaGreen",
                                          "midYellowBlue", "preserveLuminosity", "shadowCyanRed", "shadowMagentaGreen", "shadowYellowBlue"}));
    QVERIFY(ManifestJson::adjustment(object) == adjustment);
    const std::vector<std::pair<const char *, double>> amounts{
        {"shadowCyanRed", -40}, {"shadowMagentaGreen", -30}, {"shadowYellowBlue", -20}, {"midCyanRed", -10}, {"midMagentaGreen", 10},
        {"midYellowBlue", 20}, {"highlightCyanRed", 30}, {"highlightMagentaGreen", 40}, {"highlightYellowBlue", 50}};
    for (const auto &[key, amount] : amounts)
        QCOMPARE(balance.value(key).toDouble(), amount);
    QCOMPARE(balance.value("preserveLuminosity").toBool(), false);
    // Synthesized decoding needs every key, defaults or not.
    for (const QString &key : gray.keys()) {
        QJsonObject missing = object, inner = gray;
        inner.remove(key);
        missing.insert("blackWhiteSettings", inner);
        QVERIFY_THROWS_EXCEPTION(ProjectError, ManifestJson::adjustment(missing));
    }
    for (const QString &key : balance.keys()) {
        QJsonObject missing = object, inner = balance;
        inner.remove(key);
        missing.insert("colorBalanceSettings", inner);
        QVERIFY_THROWS_EXCEPTION(ProjectError, ManifestJson::adjustment(missing));
    }
}

void ColorAdjustmentTests::theSheetShowsBlackAndWhitesFamiliesAndTint()
{
    Sheet shown(FilterKind::blackWhite);
    shown.sheet->show();
    QVERIFY(QTest::qWaitForWindowExposed(shown.sheet.get()));
    for (const char *family : {"Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas"})
        QVERIFY2(shown.labels().contains(family), family);
    QSlider &reds = *shown.sheet->findChild<QSlider *>("redsSlider");
    QCOMPARE(reds.value(), int(std::lround((40.0 + 200) / 500 * 1000)));
    reds.setValue(1000);
    QCOMPARE(shown.settings().blackWhite.reds, 300.0);
    // Tint's rows show while it is on, as Swift's `if`.
    QSlider &hue = *shown.sheet->findChild<QSlider *>("hueSlider");
    QVERIFY(hue.parentWidget()->isHidden());
    QCheckBox &tint = shown.box("Tint");
    QCOMPARE(tint.toolTip(), QString("Color the result while keeping its tones, for a sepia or a cyanotype"));
    tint.click();
    QVERIFY(shown.settings().blackWhite.tint && !hue.parentWidget()->isHidden());
    QVERIFY(!shown.sheet->findChild<QSlider *>("saturationSlider")->parentWidget()->isHidden());
    // Every row writes its own field, each set apart.
    const std::vector<std::pair<const char *, double BlackWhiteSettings::*>> rows{
        {"redsSlider", &BlackWhiteSettings::reds},   {"yellowsSlider", &BlackWhiteSettings::yellows},   {"greensSlider", &BlackWhiteSettings::greens},
        {"cyansSlider", &BlackWhiteSettings::cyans}, {"bluesSlider", &BlackWhiteSettings::blues},       {"magentasSlider", &BlackWhiteSettings::magentas},
        {"hueSlider", &BlackWhiteSettings::tintHue}, {"saturationSlider", &BlackWhiteSettings::tintSaturation}};
    int position = 100;
    for (const auto &[name, field] : rows) {
        shown.sheet->findChild<QSlider *>(name)->setValue(position);
        position += 100;
    }
    const auto at = [](int travel, double low, double high) { return low + (high - low) * travel / 1000; };
    position = 100;
    for (size_t row = 0; row < rows.size(); ++row, position += 100) {
        const bool tone = row >= 6;
        const double expected = tone ? std::round(at(position, 0, row == 6 ? 360 : 100)) : std::round(at(position, -200, 300));
        QCOMPARE(shown.settings().blackWhite.*rows[row].second, expected);
    }
    // Hue runs to 360 degrees at the slider's end.
    hue.setValue(1000);
    QCOMPARE(shown.settings().blackWhite.tintHue, 360.0);
    tint.click();
    QVERIFY(!shown.settings().blackWhite.tint && hue.parentWidget()->isHidden());
}

void ColorAdjustmentTests::theSheetShowsColorBalancesThreeRanges()
{
    Sheet shown(FilterKind::colorBalance);
    const QStringList words = shown.labels();
    for (const char *range : {"Shadows", "Midtones", "Highlights"})
        QCOMPARE(words.count(range), 1);
    // Swift's headline: bold, 13 pixels.
    for (const QLabel *label : shown.sheet->findChildren<QLabel *>())
        if (label->text() == "Midtones")
            QVERIFY(label->font().bold() && label->font().pixelSize() == 13);
    QCOMPARE(words.count("Cyan / Red"), 3);
    QCOMPARE(words.count("Magenta / Green"), 3);
    QCOMPARE(words.count("Yellow / Blue"), 3);
    QCheckBox &keep = shown.box("Preserve Luminosity");
    QCOMPARE(keep.toolTip(), QString("Put each pixel's brightness back afterwards, so only the color moves"));
    QVERIFY(keep.isChecked());
    keep.click();
    QVERIFY(!shown.settings().colorBalance.preserveLuminosity);
    // Each range's three rows, in Shadows, Midtones, Highlights order.
    const std::array<const char *, 3> pairs{"cyan/RedSlider", "magenta/GreenSlider", "yellow/BlueSlider"};
    const std::array<std::array<double ColorBalanceSettings::*, 3>, 3> fields{{
        {&ColorBalanceSettings::shadowCyanRed, &ColorBalanceSettings::midCyanRed, &ColorBalanceSettings::highlightCyanRed},
        {&ColorBalanceSettings::shadowMagentaGreen, &ColorBalanceSettings::midMagentaGreen, &ColorBalanceSettings::highlightMagentaGreen},
        {&ColorBalanceSettings::shadowYellowBlue, &ColorBalanceSettings::midYellowBlue, &ColorBalanceSettings::highlightYellowBlue}}};
    for (size_t pair = 0; pair < 3; ++pair) {
        const QList<QSlider *> rows = shown.sheet->findChildren<QSlider *>(pairs[pair]);
        QCOMPARE(rows.size(), 3);
        for (size_t range = 0; range < 3; ++range)
            rows[qsizetype(range)]->setValue(int(100 + 100 * (pair * 3 + range)));
    }
    for (size_t pair = 0; pair < 3; ++pair)
        for (size_t range = 0; range < 3; ++range)
            QCOMPARE(shown.settings().colorBalance.*fields[pair][range], -100 + 200 * (100 + 100 * double(pair * 3 + range)) / 1000);
}

// From the Image menu: one step that changes the pixels.
void ColorAdjustmentTests::theImageMenusFiltersChangeTheLayer()
{
    for (const FilterKind kind : {FilterKind::blackWhite, FilterKind::colorBalance}) {
        Sheet shown(kind);
        FilterSettings settings = shown.settings();
        settings.colorBalance.midCyanRed = 100;
        shown.session.updateFilter(settings, true);
        bool done = false;
        shown.session.commitFilter([&done] { done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(shown.session.history.undoName(), rawValue(kind));
        const QColor made = shown.session.activeLayer().value().asset.value().image().pixelColor(1, 1);
        const QColor expected = first(kind == FilterKind::blackWhite ? BlackWhiteSettings().apply(solid(QColor(120, 160, 200)))
                                                                      : settings.colorBalance.apply(solid(QColor(120, 160, 200))));
        QCOMPARE(made, expected);
        QVERIFY(made != QColor(120, 160, 200));
    }
}

// Swift reports the refusal; its preview and commit never crash.
void ColorAdjustmentTests::aValuePastItsRangeIsExplained()
{
    const QString invalid = ProjectError(ProjectError::Kind::invalid).what();
    for (const FilterKind kind : {FilterKind::blackWhite, FilterKind::colorBalance}) {
        Sheet shown(kind);
        const ImageLayer before = shown.session.activeLayer().value();
        const int steps = shown.session.history.undoCount();
        FilterSettings settings = shown.settings();
        settings.blackWhite.reds = 301;
        settings.colorBalance.midCyanRed = 101;
        shown.session.updateFilter(settings, true);
        QTRY_VERIFY(!shown.session.filterEdit().value().preparing);
        QCOMPARE(shown.session.filterEdit().value().previewError, std::optional(invalid));
        shown.session.updateFilter(settings, false);
        bool done = false;
        shown.session.commitFilter([&done] { done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(shown.session.brushError(), std::optional(invalid));
        QVERIFY(!shown.session.filterEdit());
        QCOMPARE(shown.session.history.undoCount(), steps);
        QVERIFY(shown.session.activeLayer().value().asset.value().identity() == before.asset.value().identity());
    }
    // Typed into the sheet's field, as a person would.
    Sheet typed(FilterKind::blackWhite);
    QLineEdit &field = *typed.sheet->findChild<QLineEdit *>("redsField");
    field.setFocus();
    field.selectAll();
    QTest::keyClicks(&field, "301");
    QTest::keyClick(&field, Qt::Key_Return);
    QTRY_COMPARE(typed.session.filterEdit().value().previewError, std::optional(invalid));
}

QTEST_MAIN(ColorAdjustmentTests)
#include "ColorAdjustmentTests.moc"
