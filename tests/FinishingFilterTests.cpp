#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "Document/PixelAdjust.h"
#include "UI/ColorPaletteControls.h"
#include "UI/FilterSheet.h"
#include <QLabel>
#include <QLineEdit>
#include <QSignalSpy>
#include <QSlider>
#include <QTimer>
#include <QtTest>
#include <limits>

extern "C" {
#include "AdjustPixels.h"
}

// Swift's FinishingFilterTests: Vignette, Bloom / Glow, Tonal Contrast.
namespace {
QImage filled(int width, int height, const QColor &colour)
{
    QImage image = BrushRaster::context(width, height, false);
    image.fill(colour);
    return image;
}

QImage run(FilterKind kind, const QImage &source, const FilterSettings &settings, double scale = 1)
{
    return PixelFilter::run(FilterJob{kind, source, settings, scale, std::nullopt, QTransform()});
}

bool opaque(const QImage &image)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) != 255)
                return false;
        }
    }
    return true;
}

// A 24-point layer, a grey square, a 48 canvas.
EditorSession &sample(EditorSession &session)
{
    session.createDocument(48, 48);
    QImage image = BrushRaster::context(24, 24, false);
    image.fill(0);
    QPainter(&image).fillRect(QRect(3, 3, 18, 18), QColor::fromRgbF(0.7, 0.7, 0.7));
    session.insert(ImportedImage(image, image, QStringLiteral("Sample")));
    return session;
}

bool committed(EditorSession &session)
{
    bool done = false;
    session.commitFilter([&done] { done = true; });
    return QTest::qWaitFor([&done] { return done; }, 10'000);
}
}

class FinishingFilterTests : public QObject {
    Q_OBJECT
private slots:
    void vignetteDarkensCornersWhileKeepingCenterAndAlpha();
    void vignetteBlendsSelectedColorOnlyAtEdges();
    void bloomSpreadsLightFromBrightPixels();
    void tonalContrastIncreasesMidtoneDetailWithoutChangingAlpha();
    void eachFilterCommitsAsOneUndoStepAndKeepsLayerEffects();
    void theKernelsTakeTheSettingsAtTheScale();
    void settingsKeepSwiftsDefaultsAndBounds();
    void nothingToChangeClosesWithoutAStep();
    void bloomGrowsItsGridAndIsTrimmed();
    void aPreviewForAnOlderGridShowsOnThatGrid();
    void aJobWaitingBehindAnotherEditShowsOnItsGrid();
    void theVignettePickerPreviewsAndPutsBack();
    void theSheetShowsSwiftsControls();
};

void FinishingFilterTests::vignetteDarkensCornersWhileKeepingCenterAndAlpha()
{
    FilterSettings settings;
    settings.vignetteAmount = 80;
    const QImage result = run(FilterKind::vignette, filled(41, 41, QColor::fromRgbF(0.8, 0.8, 0.8)), settings);
    QVERIFY(qRed(result.pixel(20, 20)) > qRed(result.pixel(0, 0)) + 50);
    QVERIFY(std::abs(qRed(result.pixel(20, 20)) - 204) <= 2);
    QVERIFY(opaque(result));
}

void FinishingFilterTests::vignetteBlendsSelectedColorOnlyAtEdges()
{
    FilterSettings settings;
    settings.vignetteAmount = 100;
    settings.vignetteHighlights = 0;
    settings.vignetteColor = AdjustmentColor(1, 0, 0);
    const QImage result = run(FilterKind::vignette, filled(41, 41, QColor::fromRgbF(0.5, 0.5, 0.5)), settings);
    const QRgb corner = result.pixel(0, 0), centre = result.pixel(20, 20);
    QVERIFY(qRed(corner) > qGreen(corner) + 80);
    QVERIFY(std::abs(qRed(centre) - qGreen(centre)) <= 2 && std::abs(qRed(centre) - 128) <= 2);
    QVERIFY(opaque(result));
}

void FinishingFilterTests::bloomSpreadsLightFromBrightPixels()
{
    QImage source = filled(65, 65, Qt::black);
    QPainter(&source).fillRect(QRect(30, 30, 5, 5), Qt::white);
    FilterSettings settings;
    settings.bloomAmount = 100;
    settings.bloomRadius = 12;
    const QImage result = run(FilterKind::bloomGlow, source, settings);
    QVERIFY(qRed(result.pixel(40, 32)) > qRed(result.pixel(2, 2)) && qRed(result.pixel(40, 32)) > 0);
    QVERIFY(opaque(result));
    // Past a clear layer's light, the glow still shows.
    QImage isolated = filled(65, 65, Qt::transparent);
    QPainter(&isolated).fillRect(QRect(30, 30, 5, 5), Qt::white);
    QVERIFY(qAlpha(run(FilterKind::bloomGlow, isolated, settings).pixel(40, 32)) > 0);
}

void FinishingFilterTests::tonalContrastIncreasesMidtoneDetailWithoutChangingAlpha()
{
    QImage source = filled(64, 16, Qt::black);
    {
        QPainter painter(&source);
        for (int stripe = 0; stripe < 8; ++stripe)
            painter.fillRect(QRect(stripe * 8, 0, 8, 16), QColor::fromRgbF(stripe % 2 ? 0.6 : 0.4, stripe % 2 ? 0.6 : 0.4, stripe % 2 ? 0.6 : 0.4));
    }
    const FilterSettings settings{.tonalAmount = 100, .tonalRadius = 6, .tonalShadows = 0, .tonalMidtones = 100, .tonalHighlights = 0};
    const QImage result = run(FilterKind::tonalContrast, source, settings);
    QVERIFY(qRed(result.pixel(5, 8)) < qRed(source.pixel(5, 8)));
    QVERIFY(qRed(result.pixel(13, 8)) > qRed(source.pixel(13, 8)));
    QVERIFY(opaque(result));
}

void FinishingFilterTests::eachFilterCommitsAsOneUndoStepAndKeepsLayerEffects()
{
    // Swift's three, and a blur: every filter keeps effects now.
    for (const FilterKind kind : {FilterKind::vignette, FilterKind::bloomGlow, FilterKind::tonalContrast, FilterKind::gaussianBlur}) {
        EditorSession session;
        sample(session);
        const LayerEffects effects{.stroke = StrokeEffect{.size = 3}};
        session.setEffects(effects);
        session.beginFilter(kind);
        QVERIFY(session.filterEdit().value().kind == kind);
        const int steps = session.history.undoCount();
        QVERIFY(committed(session));
        QVERIFY(!session.filterEdit());
        QCOMPARE(session.history.undoCount(), steps + 1);
        QCOMPARE(session.history.undoName(), rawValue(kind));
        QVERIFY(session.activeLayer().value().effects == effects);
    }
}

void FinishingFilterTests::theKernelsTakeTheSettingsAtTheScale()
{
    QImage source = filled(40, 30, QColor(40, 120, 200));
    QPainter(&source).fillRect(QRect(10, 8, 12, 10), QColor(250, 240, 90));
    // Vignette: Swift's kernel with every setting handed on.
    FilterSettings vignette{.vignetteAmount = 70, .vignetteColor = AdjustmentColor(0.2, 0.9, 0.4), .vignetteMidpoint = 30,
                            .vignetteRoundness = -40, .vignetteFeather = 20, .vignetteHighlights = 60};
    QImage expected = source.copy();
    // The layer's own pixels frame it; clear ones stay clear.
    adjust_colored_vignette(expected.bits(), 40, 30, size_t(expected.bytesPerLine()), 0, 0, 40, 30, 0, 70, 30, -40, 20, 60, 0.2, 0.9, 0.4);
    QCOMPARE(run(FilterKind::vignette, source, vignette, 3), expected);
    // Tonal Contrast: its base blurred at radius times scale.
    const FilterSettings tonal{.tonalAmount = 80, .tonalRadius = 3, .tonalShadows = -30, .tonalMidtones = 70, .tonalHighlights = 45};
    const QImage base = PixelAdjust::gaussianBlur(source, 6, false);
    expected = source.copy();
    adjust_tonal_contrast(expected.bits(), base.constBits(), 40, 30, size_t(expected.bytesPerLine()), size_t(base.bytesPerLine()), 80, -30, 70, 45);
    QCOMPARE(run(FilterKind::tonalContrast, source, tonal, 2), expected);
    // Bloom: the pixels plus their glow at amount / 50.
    const FilterSettings bloom{.bloomAmount = 30, .bloomRadius = 2};
    const QImage glow = PixelAdjust::gaussianBlur(source, 4, false);
    expected = source.copy();
    for (int y = 0; y < 30; ++y) {
        for (int index = 0; index < 160; ++index)
            expected.scanLine(y)[index] = uchar(std::min(255.0, std::round(source.constScanLine(y)[index] + 0.6 * glow.constScanLine(y)[index])));
    }
    QCOMPARE(run(FilterKind::bloomGlow, source, bloom, 2), expected);
}

void FinishingFilterTests::settingsKeepSwiftsDefaultsAndBounds()
{
    const FilterSettings defaults;
    QVERIFY(defaults.vignetteAmount == 35 && defaults.vignetteColor == AdjustmentColor(0, 0, 0) && defaults.vignetteMidpoint == 50);
    QVERIFY(defaults.vignetteRoundness == 100 && defaults.vignetteFeather == 60 && defaults.vignetteHighlights == 25);
    QVERIFY(defaults.bloomAmount == 40 && defaults.bloomRadius == 24);
    QVERIFY(defaults.tonalAmount == 50 && defaults.tonalRadius == 16 && defaults.tonalShadows == 40 && defaults.tonalMidtones == 60);
    QCOMPARE(defaults.tonalHighlights, 30.0);
    // Past the ends, clamped; no number, the default.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const FilterSettings high = FilterSettings{.vignetteAmount = 101, .vignetteColor = AdjustmentColor(2, -1, 0.5), .vignetteMidpoint = 101,
                                               .vignetteRoundness = 101, .vignetteFeather = 101, .vignetteHighlights = 101, .bloomAmount = 101,
                                               .bloomRadius = 151, .tonalAmount = 101, .tonalRadius = 101, .tonalShadows = 101,
                                               .tonalMidtones = 101, .tonalHighlights = 101}
                                    .normalized();
    QVERIFY(high.vignetteAmount == 100 && high.vignetteColor == AdjustmentColor(1, 0, 0.5) && high.vignetteMidpoint == 100);
    QVERIFY(high.vignetteRoundness == 100 && high.vignetteFeather == 100 && high.vignetteHighlights == 100 && high.bloomAmount == 100);
    QVERIFY(high.bloomRadius == 150 && high.tonalAmount == 100 && high.tonalRadius == 100 && high.tonalShadows == 100);
    QVERIFY(high.tonalMidtones == 100 && high.tonalHighlights == 100);
    const FilterSettings low = FilterSettings{.vignetteAmount = -1, .vignetteMidpoint = -1, .vignetteRoundness = -101, .vignetteFeather = -1,
                                              .vignetteHighlights = -1, .bloomAmount = -1, .bloomRadius = 0.5, .tonalAmount = -1, .tonalRadius = 0.5,
                                              .tonalShadows = -101, .tonalMidtones = -101, .tonalHighlights = -101}
                                   .normalized();
    QVERIFY(low.vignetteAmount == 0 && low.vignetteMidpoint == 0 && low.vignetteRoundness == -100 && low.vignetteFeather == 0);
    QVERIFY(low.vignetteHighlights == 0 && low.bloomAmount == 0 && low.bloomRadius == 1 && low.tonalAmount == 0 && low.tonalRadius == 1);
    QVERIFY(low.tonalShadows == -100 && low.tonalMidtones == -100 && low.tonalHighlights == -100);
    const FilterSettings none = FilterSettings{.vignetteAmount = nan, .vignetteMidpoint = nan, .vignetteRoundness = nan, .vignetteFeather = nan,
                                               .vignetteHighlights = nan, .bloomAmount = nan, .bloomRadius = nan, .tonalAmount = nan,
                                               .tonalRadius = nan, .tonalShadows = nan, .tonalMidtones = nan, .tonalHighlights = nan}
                                    .normalized();
    QVERIFY(none == FilterSettings());
    // The Filter menu's order: after Add Noise, before Lens Correction.
    QCOMPARE(rawValue(FilterKind::vignette), QString("Vignette"));
    QCOMPARE(rawValue(FilterKind::bloomGlow), QString("Bloom / Glow"));
    QCOMPARE(rawValue(FilterKind::tonalContrast), QString("Tonal Contrast"));
    QVERIFY(allFilterKinds[3] == FilterKind::vignette && allFilterKinds[4] == FilterKind::bloomGlow && allFilterKinds[5] == FilterKind::dither
            && allFilterKinds[6] == FilterKind::tonalContrast);
    QVERIFY(!isImageAdjustment(FilterKind::bloomGlow) && !isAutomatic(FilterKind::tonalContrast) && !isAutomatic(FilterKind::vignette));
}

void FinishingFilterTests::nothingToChangeClosesWithoutAStep()
{
    const std::pair<FilterKind, FilterSettings> idle[] = {
        {FilterKind::vignette, FilterSettings{.vignetteAmount = 0}},
        {FilterKind::bloomGlow, FilterSettings{.bloomAmount = 0}},
        {FilterKind::tonalContrast, FilterSettings{.tonalAmount = 0}},
        {FilterKind::tonalContrast, FilterSettings{.tonalShadows = 0, .tonalMidtones = 0, .tonalHighlights = 0}}};
    for (const auto &[kind, settings] : idle) {
        EditorSession session;
        sample(session);
        const ImportedImage before = session.activeLayer().value().asset.value();
        session.beginFilter(kind);
        session.updateFilter(settings, true);
        const int steps = session.history.undoCount();
        QVERIFY(committed(session));
        QVERIFY(!session.filterEdit() && session.history.undoCount() == steps);
        QVERIFY(session.activeLayer().value().asset.value().identity() == before.identity());
    }
    // One tone alone still changes the pixels.
    EditorSession session;
    sample(session);
    session.beginFilter(FilterKind::tonalContrast);
    session.updateFilter(FilterSettings{.tonalShadows = 0, .tonalMidtones = 0, .tonalHighlights = 50}, true);
    const int steps = session.history.undoCount();
    QVERIFY(committed(session));
    QCOMPARE(session.history.undoCount(), steps + 1);
}

void FinishingFilterTests::bloomGrowsItsGridAndIsTrimmed()
{
    QCOMPARE(FilterEdit::blurMargin(FilterKind::bloomGlow, FilterSettings{.bloomRadius = 10}), 32.0);
    QCOMPARE(FilterEdit::blurMargin(FilterKind::tonalContrast, FilterSettings{.tonalRadius = 10}), 0.0);
    QCOMPARE(FilterEdit::blurMargin(FilterKind::vignette, FilterSettings()), 0.0);
    EditorSession session;
    sample(session);
    const LayerTransform before = session.activeLayer().value().transform;
    session.beginFilter(FilterKind::bloomGlow);
    session.updateFilter(FilterSettings{.bloomAmount = 100, .bloomRadius = 4}, true);
    // Room for the default radius, 24: three sigmas and two.
    QCOMPARE(session.filterEdit().value().grownMargin, 74.0);
    QVERIFY(committed(session));
    // The glow reaches past the layer; clear edges go.
    const LayerTransform after = session.activeLayer().value().transform;
    QVERIFY(after.size.width() > before.size.width() && after.size.width() < before.size.width() + 2 * 74);
}

void FinishingFilterTests::aPreviewForAnOlderGridShowsOnThatGrid()
{
    EditorSession session;
    session.createDocument(1200, 1200);
    session.insert(ImportedImage(filled(1200, 1200, QColor(90, 90, 90)), QImage(), QStringLiteral("Big")));
    session.beginFilter(FilterKind::bloomGlow);
    QTRY_VERIFY(!session.filterEdit().value().preparing);
    // Every landed preview sits on the grid it came from.
    bool mismatched = false;
    int older = 0;
    QObject::connect(&session, &EditorSession::changed, &session, [&] {
        const std::optional<FilterEdit> &edit = session.filterEdit();
        if (!edit || !edit.value().preparedPreview)
            return;
        if (QSizeF(edit.value().preparedPreview.value().size()) != edit.value().preparedTransform.value().size)
            mismatched = true;
        older += edit.value().preparedPreview.value().size() != edit.value().previewSource.size();
    });
    session.updateFilter(FilterSettings{.bloomRadius = 5}, true);
    const LayerTransform grid = session.filterEdit().value().grownTransform.value();
    // Wider than the grid: it grows mid-render.
    session.updateFilter(FilterSettings{.bloomRadius = 40}, true);
    QVERIFY(session.filterEdit().value().grownTransform.value().size.width() > grid.size.width());
    QTRY_VERIFY(session.filterEdit().value().preparedPreview && !session.filterEdit().value().preparing);
    QVERIFY(!mismatched && older > 0);
    QCOMPARE(session.filterEdit().value().preparedPreview.value().size(), session.filterEdit().value().previewSource.size());
    QCOMPARE(session.filterEdit().value().preparedTransform, session.filterEdit().value().grownTransform);
    // Preview off: nothing waits; the stale render ends it.
    session.updateFilter(FilterSettings{.bloomRadius = 5}, true);
    session.updateFilter(FilterSettings{.bloomRadius = 60}, false);
    QTRY_VERIFY(!session.filterEdit().value().preparing);
    QVERIFY(!session.filterEdit().value().preparedPreview);
}

void FinishingFilterTests::aJobWaitingBehindAnotherEditShowsOnItsGrid()
{
    // The reviewer's sequence: a cancelled edit's worker still runs.
    EditorSession session;
    session.createDocument(1200, 1200);
    session.insert(ImportedImage(filled(1200, 1200, QColor(90, 90, 90)), QImage(), QStringLiteral("Big")));
    session.beginFilter(FilterKind::gaussianBlur);
    QVERIFY(session.filterEdit().value().preparing);
    session.cancelFilter();
    // Bloom's first job waits; Preview off grows the grid meanwhile.
    session.beginFilter(FilterKind::bloomGlow);
    session.updateFilter(FilterSettings{.bloomRadius = 50}, false);
    bool mismatched = false, armed = true;
    QObject::connect(&session, &EditorSession::changed, &session, [&] {
        const std::optional<FilterEdit> &edit = session.filterEdit();
        if (edit && edit.value().preparedPreview && QSizeF(edit.value().preparedPreview.value().size()) != edit.value().preparedTransform.value().size)
            mismatched = true;
        // The waiting job starts: Preview comes back next turn.
        if (std::exchange(armed, false))
            QTimer::singleShot(0, &session, [&session] { session.updateFilter(FilterSettings{.bloomRadius = 50}, true); });
    });
    QTRY_VERIFY(!armed);
    QTRY_VERIFY(session.filterEdit().value().preparedPreview && !session.filterEdit().value().preparing);
    QVERIFY(!mismatched);
    QCOMPARE(session.filterEdit().value().preparedPreview.value().size(), session.filterEdit().value().previewSource.size());
}

void FinishingFilterTests::theVignettePickerPreviewsAndPutsBack()
{
    EditorSession session;
    sample(session);
    // Only the open Vignette takes it.
    session.beginFilter(FilterKind::bloomGlow);
    session.openVignetteColorPicker();
    QVERIFY(!session.colorPicker());
    session.cancelFilter();
    session.beginFilter(FilterKind::vignette);
    session.updateFilter(FilterSettings{.vignetteColor = AdjustmentColor(0, 0, 1)}, true);
    session.openVignetteColorPicker();
    const ColorPickerState opened = session.colorPicker().value();
    QCOMPARE(opened.target.title(), QString("Color Picker (Vignette Color)"));
    QVERIFY(opened.original == (PaletteColor{0, 0, 1}));
    // The working colour previews; Cancel puts the original back.
    PickerHSB hsb = opened.hsb;
    hsb.setRGB(PaletteColor{1, 0, 0});
    session.setColorPickerHSB(hsb);
    session.previewVignetteColor();
    QCOMPARE(session.filterEdit().value().settings.vignetteColor, AdjustmentColor(1, 0, 0));
    session.closeColorPicker(false);
    QCOMPARE(session.filterEdit().value().settings.vignetteColor, AdjustmentColor(0, 0, 1));
    // OK keeps the colour; a picker already open refuses another.
    session.openVignetteColorPicker();
    hsb.setRGB(PaletteColor{0, 1, 0});
    session.setColorPickerHSB(hsb);
    const QUuid id = session.colorPicker().value().id;
    session.openVignetteColorPicker();
    QCOMPARE(session.colorPicker().value().id, id);
    session.closeColorPicker(true);
    QCOMPARE(session.filterEdit().value().settings.vignetteColor, AdjustmentColor(0, 1, 0));
    // The panel's Cancel closes the picker unkept, its OK kept.
    session.openVignetteColorPicker();
    hsb.setRGB(PaletteColor{1, 1, 0});
    session.setColorPickerHSB(hsb);
    session.cancelFilter();
    QVERIFY(!session.colorPicker() && !session.filterEdit());
    session.beginFilter(FilterKind::vignette);
    session.openVignetteColorPicker();
    hsb.setRGB(PaletteColor{1, 1, 0});
    session.setColorPickerHSB(hsb);
    QVERIFY(committed(session));
    QVERIFY(!session.colorPicker());
    QCOMPARE(session.filterSettings().vignetteColor, AdjustmentColor(1, 1, 0));
}

void FinishingFilterTests::theSheetShowsSwiftsControls()
{
    // Each row: title, range, unit and scale, as Swift.
    struct Row {
        const char *name;
        double FilterSettings::*member;
        double low, high;
        QString unit;
        bool logarithmic;
    };
    const std::pair<FilterKind, std::vector<Row>> kinds[] = {
        {FilterKind::vignette,
         {{"amount", &FilterSettings::vignetteAmount, 0, 100, "%", false},
          {"midpoint", &FilterSettings::vignetteMidpoint, 0, 100, "%", false},
          {"roundness", &FilterSettings::vignetteRoundness, -100, 100, "", false},
          {"feather", &FilterSettings::vignetteFeather, 0, 100, "%", false},
          {"highlights", &FilterSettings::vignetteHighlights, 0, 100, "%", false}}},
        {FilterKind::bloomGlow,
         {{"amount", &FilterSettings::bloomAmount, 0, 100, "%", false}, {"radius", &FilterSettings::bloomRadius, 1, 150, "px", true}}},
        {FilterKind::tonalContrast,
         {{"amount", &FilterSettings::tonalAmount, 0, 100, "%", false}, {"shadows", &FilterSettings::tonalShadows, -100, 100, "%", false},
          {"midtones", &FilterSettings::tonalMidtones, -100, 100, "%", false},
          {"highlights", &FilterSettings::tonalHighlights, -100, 100, "%", false},
          {"radius", &FilterSettings::tonalRadius, 1, 100, "px", true}}}};
    for (const auto &[kind, rows] : kinds) {
        EditorSession session;
        sample(session);
        session.beginFilter(kind);
        FilterSheet sheet(session);
        QCOMPARE(sheet.findChildren<QSlider *>().size(), qsizetype(rows.size()));
        for (const Row &row : rows) {
            auto *slider = sheet.findChild<QSlider *>(QString::fromLatin1(row.name) + "Slider");
            auto *field = sheet.findChild<QLineEdit *>(QString::fromLatin1(row.name) + "Field");
            QVERIFY2(slider && field, row.name);
            const QList<QLabel *> labels = field->parentWidget()->findChildren<QLabel *>(QString(), Qt::FindDirectChildrenOnly);
            QCOMPARE(labels.last()->text(), row.unit);
            // The ends, then the middle: linear, or the geometric mean.
            const double middle = row.logarithmic ? std::round(std::sqrt(row.low * row.high)) : (row.low + row.high) / 2;
            for (const auto &[position, value] : {std::pair(0, row.low), std::pair(1000, row.high), std::pair(500, middle)}) {
                // Its own setting alone moves, and the field shows it.
                FilterSettings expected = session.filterEdit().value().settings;
                expected.*row.member = value;
                slider->setValue(position);
                QVERIFY2(session.filterEdit().value().settings == expected, row.name);
                QCOMPARE(field->text().toDouble(), value);
            }
        }
        QCOMPARE(sheet.findChild<SwatchButton *>("vignetteSwatch") != nullptr, kind == FilterKind::vignette);
    }
    EditorSession session;
    sample(session);
    session.beginFilter(FilterKind::vignette);
    FilterSheet sheet(session);
    QCOMPARE(sheet.findChild<QSlider *>("amountSlider")->parentWidget()->toolTip(),
             QString("Blend the chosen color into the edges while keeping the center unchanged"));
    QCOMPARE(sheet.findChild<QSlider *>("highlightsSlider")->parentWidget()->toolTip(), QString("Protect bright areas near the edge"));
    QVERIFY(sheet.findChild<QSlider *>("midpointSlider")->parentWidget()->toolTip().isEmpty());
    // The swatch: 24 points, its words, picker and colour.
    SwatchButton &swatch = *sheet.findChild<SwatchButton *>("vignetteSwatch");
    QCOMPARE(swatch.size(), QSize(24, 24));
    QCOMPARE(swatch.toolTip(), QString("Choose the vignette color"));
    QCOMPARE(swatch.accessibleName(), QString("Vignette color"));
    const QList<QLabel *> labels = swatch.parentWidget()->findChildren<QLabel *>();
    QVERIFY(labels.size() == 1 && labels[0]->text() == QString("Color") && labels[0]->width() == 95);
    QCOMPARE(swatch.grab().toImage().pixelColor(12, 12).rgb(), QColor(Qt::black).rgb());
    swatch.click();
    QVERIFY(session.colorPicker().value().target.kind == ColorPickerTarget::Kind::vignette);
    // The sheet hands the picker's colour on and repaints.
    PickerHSB hsb = session.colorPicker().value().hsb;
    hsb.setRGB(PaletteColor{1, 0, 0});
    session.setColorPickerHSB(hsb);
    QCOMPARE(session.filterEdit().value().settings.vignetteColor, AdjustmentColor(1, 0, 0));
    QCOMPARE(swatch.grab().toImage().pixelColor(12, 12).rgb(), QColor(Qt::red).rgb());
    // Shown, the swatch repaints on a change unasked.
    sheet.show();
    QVERIFY(QTest::qWaitForWindowExposed(&sheet));
    struct Paints : QObject {
        int count = 0;
        bool eventFilter(QObject *, QEvent *event) override
        {
            count += event->type() == QEvent::Paint;
            return false;
        }
    } paints;
    swatch.installEventFilter(&paints);
    QTest::qWait(100);
    paints.count = 0;
    hsb.setRGB(PaletteColor{0, 0, 1});
    session.setColorPickerHSB(hsb);
    QTRY_VERIFY(paints.count > 0);
}

QTEST_MAIN(FinishingFilterTests)
#include "FinishingFilterTests.moc"
