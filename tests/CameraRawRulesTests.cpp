#include "CameraRawFixtures.h"
#include "Document/Filters.h"
#include <QtTest>
#include <cmath>

// The grade's rules that Swift's tests leave to its code.
using namespace CameraRawFixtures;

namespace {
// Every group but Light and Color moved, visibly.
CameraRawSettings everything()
{
    CameraRawSettings settings;
    settings.whites = 100;
    settings.curve.rgb = CameraRawCurveSettings::strongContrast();
    settings.grading.shadows.saturation = 100;
    settings.vignetteAmount = -100;
    settings.detail.sharpenAmount = 150;
    settings.optics.distortion = 100;
    settings.geometry.vertical = 60;
    settings.calibration.redHue = 80;
    return settings;
}

QImage colourful()
{
    QImage picture = checker(16, 16);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 8; ++x)
            picture.setPixelColor(x, y, QColor(200, 40 + 10 * y, 30));
    return picture;
}
}

class CameraRawRulesTests : public QObject {
    Q_OBJECT
private slots:
    void boundsAndNormalizingCoverEveryField();
    void anUntouchedGradeHandsTheImageBack();
    void theClippingAndMaskViewsShowNothingElse();
    void theScaleDefaultsToOneAndReachesTheRadii();
    void eachEyeDropsItsOwnGroupAlone();
    void grainSizeMapsIntoTheKernelsScale();
    void autoBalanceSkipsClearPixels();
    void thePreviewShowsItsViewAndCountsTheGrade();
    void aPointColorShowsAloneOnlyWhenAsked();
};

void CameraRawRulesTests::boundsAndNormalizingCoverEveryField()
{
    // Every tone clamps to ±100, every unit to 0–100.
    CameraRawSettings past;
    const std::array tones{&CameraRawSettings::contrast,   &CameraRawSettings::highlights, &CameraRawSettings::shadows,   &CameraRawSettings::whites,
                           &CameraRawSettings::blacks,     &CameraRawSettings::temperature, &CameraRawSettings::tint,     &CameraRawSettings::vibrance,
                           &CameraRawSettings::saturation, &CameraRawSettings::texture,    &CameraRawSettings::clarity,   &CameraRawSettings::dehaze,
                           &CameraRawSettings::glowRange,  &CameraRawSettings::glowSpread, &CameraRawSettings::glowWarmth, &CameraRawSettings::vignetteAmount,
                           &CameraRawSettings::vignetteRoundness};
    const std::array units{&CameraRawSettings::glow,         &CameraRawSettings::vignetteMidpoint, &CameraRawSettings::vignetteFeather,
                           &CameraRawSettings::vignetteHighlights, &CameraRawSettings::grainAmount, &CameraRawSettings::grainSize,
                           &CameraRawSettings::grainRoughness};
    for (double CameraRawSettings::*field : tones)
        past.*field = 150;
    for (double CameraRawSettings::*field : units)
        past.*field = 150;
    past.exposure = 9;
    CameraRawSettings clamped = past.normalized();
    for (double CameraRawSettings::*field : tones)
        QCOMPARE(clamped.*field, 100.0);
    for (double CameraRawSettings::*field : units)
        QCOMPARE(clamped.*field, 100.0);
    QCOMPARE(clamped.exposure, 5.0);
    for (double CameraRawSettings::*field : tones)
        past.*field = -150;
    for (double CameraRawSettings::*field : units)
        past.*field = -150;
    past.exposure = -9;
    clamped = past.normalized();
    for (double CameraRawSettings::*field : tones)
        QCOMPARE(clamped.*field, -100.0);
    for (double CameraRawSettings::*field : units)
        QCOMPARE(clamped.*field, 0.0);
    QCOMPARE(clamped.exposure, -5.0);
    CameraRawSettings wild;
    wild.vignetteMidpoint = std::nan("");
    wild.grainSize = 400;
    wild.grainRoughness = -3;
    wild.vignetteFeather = 250;
    wild.glow = 101;
    const CameraRawSettings normal = wild.normalized();
    QVERIFY(normal.vignetteMidpoint == 50 && normal.grainSize == 100 && normal.grainRoughness == 0 && normal.vignetteFeather == 100 && normal.glow == 100);
}

void CameraRawRulesTests::anUntouchedGradeHandsTheImageBack()
{
    const QImage input = gray();
    QCOMPARE(CameraRawSettings().apply(input).cacheKey(), input.cacheKey());
    CameraRawSettings moved;
    moved.exposure = 1;
    QVERIFY(moved.apply(input).cacheKey() != input.cacheKey());
}

void CameraRawRulesTests::theClippingAndMaskViewsShowNothingElse()
{
    const QImage picture = colourful();
    CameraRawSettings light;
    light.whites = 100;
    const CameraRawSettings all = everything();
    QVERIFY(pixels(all.apply(picture, CameraRawClipping::highlights)) == pixels(light.apply(picture, CameraRawClipping::highlights)));
    QVERIFY(pixels(all.apply(picture, CameraRawClipping::shadows)) == pixels(light.apply(picture, CameraRawClipping::shadows)));
    // An edge off the centre, where the lens moves it.
    QImage edge = image(24, 24, 0.9, 0.9, 0.9);
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 5; ++x)
            edge.setPixelColor(x, y, QColor(40, 40, 40));
    QVERIFY(pixels(all.apply(edge, CameraRawClipping::highlights)) == pixels(light.apply(edge, CameraRawClipping::highlights)));
    // The mask reads the Light grade, as Swift's does.
    CameraRawSettings sharpening = light;
    sharpening.detail = all.detail;
    sharpening.optics = all.optics;
    QVERIFY(pixels(all.apply(picture, std::nullopt, 1, 0, -1, true)) == pixels(sharpening.apply(picture, std::nullopt, 1, 0, -1, true)));
    // Showing a point colour leaves the geometry out.
    CameraRawSettings warped;
    warped.geometry.vertical = 60;
    warped.mixer.points = {CameraRawPointColor{.hue = 10, .saturation = 0.8, .luminance = 0.5, .visualize = true}};
    CameraRawSettings flat = warped;
    flat.geometry = CameraRawGeometrySettings();
    QVERIFY(pixels(warped.apply(picture, std::nullopt, 1, 0, 0)) == pixels(flat.apply(picture, std::nullopt, 1, 0, 0)));
    QVERIFY(pixels(warped.apply(picture)) != pixels(flat.apply(picture)));
}

void CameraRawRulesTests::theScaleDefaultsToOneAndReachesTheRadii()
{
    CameraRawSettings settings;
    settings.clarity = 100;
    const QImage edge = step();
    QVERIFY(pixels(settings.apply(edge, std::nullopt, 0)) == pixels(settings.apply(edge, std::nullopt, 1)));
    QVERIFY(pixels(settings.apply(edge, std::nullopt, -2)) == pixels(settings.apply(edge, std::nullopt, 1)));
    QVERIFY(pixels(settings.apply(edge, std::nullopt, 0.25)) != pixels(settings.apply(edge, std::nullopt, 1)));
    // Grain's units read the scale: a negative one is one.
    CameraRawSettings grainy;
    grainy.grainAmount = 60;
    QVERIFY(pixels(grainy.apply(gray(16, 16), std::nullopt, -2, 3)) == pixels(grainy.apply(gray(16, 16), std::nullopt, 1, 3)));
}

void CameraRawRulesTests::eachEyeDropsItsOwnGroupAlone()
{
    const CameraRawSettings all = [] {
        CameraRawSettings settings = everything();
        settings.exposure = 1;
        settings.temperature = 20;
        settings.texture = 30;
        settings.mixer.hue[0] = 50;
        return settings;
    }();
    const CameraRawSettings color = all.applying({.color = false});
    QVERIFY(color.temperature == 0 && color.saturation == 0 && color.exposure == 1);
    CameraRawSettings saturated = all;
    saturated.saturation = 40;
    QCOMPARE(saturated.applying({.color = false}).saturation, 0.0);
    const std::vector<std::pair<CameraRawGroups, std::function<bool(const CameraRawSettings &)>>> groups{
        {{.mixer = false}, [](const CameraRawSettings &s) { return s.mixer == CameraRawMixerSettings(); }},
        {{.grading = false}, [](const CameraRawSettings &s) { return s.grading == CameraRawGradingSettings(); }},
        {{.optics = false}, [](const CameraRawSettings &s) { return s.optics == CameraRawOpticsSettings(); }},
        {{.geometry = false}, [](const CameraRawSettings &s) { return s.geometry == CameraRawGeometrySettings(); }}};
    for (const auto &[shown, dropped] : groups) {
        const CameraRawSettings hidden = all.applying(shown);
        QVERIFY(dropped(hidden));
        QVERIFY(hidden.exposure == 1 && hidden.curve == all.curve && hidden.texture == 30);
    }
}

void CameraRawRulesTests::grainSizeMapsIntoTheKernelsScale()
{
    CameraRawSettings settings;
    QCOMPARE(settings.grainKernelSize(), 0.5 + 0.25 * 19.5);
    settings.grainSize = 100;
    QCOMPARE(settings.grainKernelSize(), 20.0);
    settings.grainSize = 0;
    QCOMPARE(settings.grainKernelSize(), 0.5);
}

void CameraRawRulesTests::autoBalanceSkipsClearPixels()
{
    const double red = 160 / 255.0, green = 140 / 255.0, blue = 120 / 255.0;
    QImage half = image(8, 8, red, green, blue);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 4; ++x)
            half.setPixelColor(x, y, Qt::transparent);
    const CameraRawSettings::Balance solid = CameraRawSettings::autoBalance(image(8, 8, red, green, blue)).value();
    const CameraRawSettings::Balance cut = CameraRawSettings::autoBalance(half).value();
    QVERIFY(std::abs(cut.temperature - solid.temperature) < 1e-9 && std::abs(cut.tint - solid.tint) < 1e-9);
    QVERIFY(!CameraRawSettings::autoBalance(image(8, 8, red, green, blue, 0)));
}

void CameraRawRulesTests::thePreviewShowsItsViewAndCountsTheGrade()
{
    FilterSettings settings;
    settings.cameraRaw.whites = 100;
    const QImage picture = pair(230, 20);
    FilterJob job{FilterKind::cameraRaw, picture, settings, 1, std::nullopt, QTransform()};
    const auto [graded, gradedScope] = CameraRawScope::preview(job);
    QVERIFY(pixels(graded) == pixels(settings.cameraRaw.apply(picture)));
    QVERIFY(gradedScope == CameraRawScope::make(graded).value());
    job.cameraRawClipping = CameraRawClipping::highlights;
    const auto [view, viewScope] = CameraRawScope::preview(job);
    QVERIFY(pixels(view) == pixels(settings.cameraRaw.apply(picture, CameraRawClipping::highlights)));
    QVERIFY(viewScope == gradedScope);
    job.cameraRawClipping = std::nullopt;
    job.showsHighlightClipping = true;
    const auto [marked, markedScope] = CameraRawScope::preview(job);
    QVERIFY(pixels(marked) == pixels(CameraRawScope::overlay(graded, false, true)) && pixels(marked) != pixels(graded));
    QVERIFY(markedScope == gradedScope);
}

void CameraRawRulesTests::aPointColorShowsAloneOnlyWhenAsked()
{
    CameraRawSettings settings;
    settings.mixer.points = {CameraRawPointColor{.hue = 10}, CameraRawPointColor{.hue = 200, .visualize = true}};
    CameraRawPanel panel;
    QCOMPARE(panel.pointColorVisualizeIndex(settings), -1);
    panel.pointIndex = 1;
    QCOMPARE(panel.pointColorVisualizeIndex(settings), 1);
    panel.pointIndex = 2;
    QCOMPARE(panel.pointColorVisualizeIndex(settings), -1);
}

QTEST_GUILESS_MAIN(CameraRawRulesTests)
#include "CameraRawRulesTests.moc"
