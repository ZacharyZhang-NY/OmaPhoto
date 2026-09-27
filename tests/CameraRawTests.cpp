#include "CameraRawFixtures.h"
#include "Document/Filters.h"
#include <QtTest>
#include <cmath>

// Swift's CameraRawTests: the grade on pixels, group by group.
using namespace CameraRawFixtures;

class CameraRawTests : public QObject {
    Q_OBJECT
private slots:
    void defaultsLeavePixelsAndAlphaAlone();
    void exposureAddsOneStopAndContrastPivotsAroundMidGray();
    void tonalSlidersMoveTheEndTheyName();
    void temperatureWarmsAndTintMovesTowardMagenta();
    void vibranceFavorsDullColorsAndProtectsSkinWhileSaturationDoesNot();
    void textureAndClaritySharpenAnEdgeAndLeaveAFlatField();
    void dehazeDeepensOrLiftsAndKeepsAlpha();
    void glowIsIdleAtZeroAndHalationFringeIsRedderThanDiffusion();
    void vignetteDarkensCornersAndHighlightsOnlyWhileDarkening();
    void grainIsStableAndTheEffectsEyeDropsTheWholeGroup();
    void histogramFollowsTheGradeAndClippingPaintStaysOffTheResult();
    void curveMixerAndGradingChangeOnlyTheirOwnTones();
    void detailSharpeningNoiseAndMaskingPreview();
    void opticsDistortionDefringeAndDetailEye();
};

void CameraRawTests::defaultsLeavePixelsAndAlphaAlone()
{
    const QImage input = gray(4, 4, 0.5);
    QVERIFY(pixels(CameraRawSettings().apply(input)) == pixels(input));
    CameraRawSettings broken;
    broken.exposure = std::nan("");
    broken.temperature = 400;
    QVERIFY(broken.normalized().exposure == 0 && broken.normalized().temperature == 100);
    QVERIFY(!isImageAdjustment(FilterKind::cameraRaw));
}

void CameraRawTests::exposureAddsOneStopAndContrastPivotsAroundMidGray()
{
    const QImage input = gray();
    CameraRawSettings settings;
    settings.exposure = 1;
    const std::array<int, 4> brighter = pixels(settings.apply(input))[0];
    QVERIFY2(std::abs(brighter[0] - 176) <= 2, qPrintable(QString::number(brighter[0])));
    QVERIFY(brighter[0] == brighter[1] && brighter[1] == brighter[2]);
    const QImage translucent = gray(4, 4, 0.5);
    QCOMPARE(pixels(settings.apply(translucent))[0][3], pixels(translucent)[0][3]);
    settings = CameraRawSettings();
    settings.contrast = 100;
    const auto pushed = pixels(settings.apply(pair(64, 192)));
    QVERIFY2(pushed[0][0] < 10 && pushed[1][0] > 250, "contrast +100 drives the pair apart");
    settings.contrast = -100;
    const auto flat = pixels(settings.apply(pair(64, 192)));
    QVERIFY2(std::abs(flat[0][0] - 128) <= 2 && std::abs(flat[1][0] - 128) <= 2, "contrast −100 meets at mid gray");
}

void CameraRawTests::tonalSlidersMoveTheEndTheyName()
{
    const QImage brightAndMid = pair(230, 128);
    CameraRawSettings settings;
    settings.highlights = -100;
    const auto recovered = pixels(settings.apply(brightAndMid));
    QVERIFY(recovered[0][0] < 200 && std::abs(recovered[1][0] - 128) <= 2);
    settings = CameraRawSettings();
    settings.whites = 100;
    const auto clipped = pixels(settings.apply(brightAndMid));
    QVERIFY(clipped[0][0] == 255 && std::abs(clipped[1][0] - 128) <= 2);
    const auto view = pixels(settings.apply(brightAndMid, CameraRawClipping::highlights));
    QVERIFY2((view[0] == std::array{255, 255, 255, 255}) && (view[1] == std::array{0, 0, 0, 255}), "highlight clipping is not the grade");
    const QImage darkAndMid = pair(20, 128);
    settings = CameraRawSettings();
    settings.shadows = 100;
    const auto opened = pixels(settings.apply(darkAndMid));
    QVERIFY(opened[0][0] > 50 && std::abs(opened[1][0] - 128) <= 2);
    settings = CameraRawSettings();
    settings.blacks = -100;
    const auto crushed = pixels(settings.apply(darkAndMid));
    QVERIFY(crushed[0][0] < 20 && std::abs(crushed[1][0] - 128) <= 2);
    const auto shadowView = pixels(settings.apply(darkAndMid, CameraRawClipping::shadows));
    QVERIFY2((shadowView[0] == std::array{0, 0, 0, 255}) && (shadowView[1] == std::array{255, 255, 255, 255}), "shadow clipping is not the grade");
}

void CameraRawTests::temperatureWarmsAndTintMovesTowardMagenta()
{
    const QImage input = gray();
    CameraRawSettings settings;
    settings.temperature = 100;
    const std::array<int, 4> warm = pixels(settings.apply(input))[0];
    QVERIFY(warm[0] > 128 && warm[2] < 128 && warm[0] > warm[2]);
    settings = CameraRawSettings();
    settings.tint = 100;
    const std::array<int, 4> magenta = pixels(settings.apply(input))[0];
    QVERIFY(magenta[1] < 128 && magenta[1] < magenta[0] && magenta[1] < magenta[2]);
}

void CameraRawTests::vibranceFavorsDullColorsAndProtectsSkinWhileSaturationDoesNot()
{
    const QImage dullGreen = image(4, 4, 77 / 255.0, 153 / 255.0, 77 / 255.0), saturatedGreen = image(4, 4, 20 / 255.0, 200 / 255.0, 20 / 255.0);
    const QImage skin = image(4, 4, 153 / 255.0, 115 / 255.0, 77 / 255.0);
    CameraRawSettings settings;
    settings.vibrance = 100;
    const int dullBefore = chroma(pixels(dullGreen)[0]), saturatedBefore = chroma(pixels(saturatedGreen)[0]), skinBefore = chroma(pixels(skin)[0]);
    const int dullDelta = chroma(pixels(settings.apply(dullGreen))[0]) - dullBefore;
    const int saturatedDelta = chroma(pixels(settings.apply(saturatedGreen))[0]) - saturatedBefore;
    const int skinDelta = chroma(pixels(settings.apply(skin))[0]) - skinBefore;
    QVERIFY(dullDelta > saturatedDelta + 10);
    QCOMPARE(dullBefore, skinBefore);
    QVERIFY(dullDelta > skinDelta + 10);
    const QImage red = image(4, 4, 160 / 255.0, 120 / 255.0, 120 / 255.0), blue = image(4, 4, 100 / 255.0, 100 / 255.0, 140 / 255.0);
    settings = CameraRawSettings();
    settings.saturation = 100;
    const double redRatio = double(chroma(pixels(settings.apply(red))[0])) / chroma(pixels(red)[0]);
    const double blueRatio = double(chroma(pixels(settings.apply(blue))[0])) / chroma(pixels(blue)[0]);
    QVERIFY2(std::abs(redRatio - 2) < 0.15 && std::abs(blueRatio - 2) < 0.15, "saturation doubles both");
}

void CameraRawTests::textureAndClaritySharpenAnEdgeAndLeaveAFlatField()
{
    const QImage flat = gray();
    CameraRawSettings settings;
    settings.texture = 100;
    settings.clarity = 100;
    QVERIFY2(pixels(settings.apply(flat)) == pixels(flat), "a flat field has no local contrast");
    const QImage edge = step();
    const auto original = pixels(edge);
    settings = CameraRawSettings();
    settings.texture = 100;
    const auto textured = pixels(settings.apply(edge));
    QCOMPARE(textured[0][0], original[0][0]);
    QVERIFY2(textured[8][0] == original[8][0], "texture's fine radius does not reach this far");
    QVERIFY(textured[11][0] != original[11][0]);
    settings = CameraRawSettings();
    settings.clarity = 100;
    const auto clarified = pixels(settings.apply(edge));
    QCOMPARE(clarified[0][0], original[0][0]);
    QVERIFY2(clarified[8][0] != original[8][0], "clarity's wider radius reaches further in");
    settings.clarity = -100;
    const auto softened = pixels(settings.apply(edge));
    QVERIFY(std::abs(softened[12][0] - softened[11][0]) < std::abs(original[12][0] - original[11][0]));
}

void CameraRawTests::dehazeDeepensOrLiftsAndKeepsAlpha()
{
    const QImage dark = image(4, 4, 30 / 255.0, 30 / 255.0, 30 / 255.0), pale = image(4, 4, 180 / 255.0, 150 / 255.0, 150 / 255.0);
    CameraRawSettings settings;
    settings.dehaze = 100;
    QVERIFY(pixels(settings.apply(dark))[0][0] < 30);
    const std::array<int, 4> paleBefore = pixels(pale)[0];
    QVERIFY(chroma(pixels(settings.apply(pale))[0]) > chroma(paleBefore));
    settings.dehaze = -100;
    QVERIFY(pixels(settings.apply(dark))[0][0] > 30);
    QVERIFY(chroma(pixels(settings.apply(pale))[0]) < chroma(paleBefore));
    const QImage translucent = image(4, 4, 30 / 255.0, 30 / 255.0, 30 / 255.0, 0.5);
    settings.dehaze = 100;
    QCOMPARE(pixels(settings.apply(translucent))[0][3], pixels(translucent)[0][3]);
}

void CameraRawTests::glowIsIdleAtZeroAndHalationFringeIsRedderThanDiffusion()
{
    QImage spot = image(21, 21, 0, 0, 0);
    for (int y = 8; y < 13; ++y)
        for (int x = 8; x < 13; ++x)
            spot.setPixelColor(x, y, Qt::white);
    CameraRawSettings settings;
    settings.glowWarmth = 100;
    settings.glowRange = 100;
    QVERIFY2(pixels(settings.apply(spot)) == pixels(spot), "range and warmth do nothing until Glow is raised");
    settings.glow = 100;
    settings.glowStyle = CameraRawGlowStyle::diffusion;
    const auto diffusion = pixels(settings.apply(spot));
    settings.glowStyle = CameraRawGlowStyle::halation;
    const auto halation = pixels(settings.apply(spot));
    const size_t fringe = 10 * 21 + 15;
    QVERIFY(diffusion[fringe][0] > diffusion[0][0]);
    QVERIFY((halation[0] == std::array{0, 0, 0, 255}));
    QVERIFY(halation[fringe][0] - halation[fringe][1] > diffusion[fringe][0] - diffusion[fringe][1]);
}

void CameraRawTests::vignetteDarkensCornersAndHighlightsOnlyWhileDarkening()
{
    const QImage field = gray(9, 9);
    CameraRawSettings settings;
    settings.vignetteAmount = -100;
    const auto darkened = pixels(settings.apply(field));
    QVERIFY(std::abs(darkened[4 * 9 + 4][0] - 128) <= 2 && darkened[0][0] < darkened[4 * 9 + 4][0] - 40);
    const QImage white = image(9, 9, 1, 1, 1);
    settings.vignetteHighlights = 100;
    settings.vignetteStyle = CameraRawVignetteStyle::highlightPriority;
    const int kept = pixels(settings.apply(white))[0][0];
    settings.vignetteHighlights = 0;
    const int exposed = pixels(settings.apply(white))[0][0];
    QVERIFY(kept > exposed + 40);
    settings.vignetteHighlights = 100;
    settings.vignetteStyle = CameraRawVignetteStyle::paintOverlay;
    QVERIFY2(pixels(settings.apply(white))[0][0] < kept, "paint overlay does not use Highlights");
    settings = CameraRawSettings();
    settings.vignetteAmount = 100;
    const auto plain = pixels(settings.apply(field));
    settings.vignetteHighlights = 100;
    QVERIFY2(plain == pixels(settings.apply(field)), "highlights is idle while the vignette lightens");
}

void CameraRawTests::grainIsStableAndTheEffectsEyeDropsTheWholeGroup()
{
    const QImage field = gray(16, 16);
    CameraRawSettings settings;
    settings.grainSize = 40;
    settings.grainRoughness = 80;
    QVERIFY(pixels(settings.apply(field, std::nullopt, 1, 4)) == pixels(field));
    settings.grainAmount = 70;
    const auto first = pixels(settings.apply(field, std::nullopt, 1, 4));
    QVERIFY(first == pixels(settings.apply(field, std::nullopt, 1, 4)));
    QVERIFY(first != pixels(field));
    QVERIFY(first[0][0] == first[0][1] && first[0][1] == first[0][2]);
    QCOMPARE(pixels(settings.apply(image(4, 4, 0.5, 0.5, 0.5, 0), std::nullopt, 1, 4))[0][3], 0);
    const QImage edge = step();
    settings = CameraRawSettings();
    settings.texture = 100;
    settings.grainAmount = 50;
    const CameraRawSettings hidden = settings.applying({.effects = false});
    QVERIFY(hidden.isIdentity());
    QVERIFY(pixels(hidden.apply(edge, std::nullopt, 1, 2)) == pixels(edge));
    QVERIFY(pixels(settings.apply(edge, std::nullopt, 1, 2)) != pixels(edge));
}

void CameraRawTests::histogramFollowsTheGradeAndClippingPaintStaysOffTheResult()
{
    const QImage black = image(4, 4, 0, 0, 0), white = image(4, 4, 1, 1, 1);
    QCOMPARE(peakIndex(CameraRawScope::make(black).value().red), 0);
    QCOMPARE(peakIndex(CameraRawScope::make(white).value().red), 255);
    CameraRawSettings settings;
    settings.exposure = 1;
    QVERIFY(peakIndex(CameraRawScope::make(settings.apply(gray())).value().red) > 128);
    const std::array<int, 4> shadowed = pixels(CameraRawScope::overlay(black, true, false))[0];
    QVERIFY(shadowed[2] > shadowed[0]);
    const std::array<int, 4> highlighted = pixels(CameraRawScope::overlay(white, false, true))[0];
    QVERIFY(highlighted[0] > highlighted[2]);
    QVERIFY(pixels(CameraRawScope::overlay(black, false, false)) == pixels(black));
    const CameraRawScope scope = CameraRawScope::make(image(4, 4, 1, 0, 0)).value();
    const auto hottest = std::max_element(scope.vectorscope.begin(), scope.vectorscope.end()) - scope.vectorscope.begin();
    QVERIFY2(hottest % CameraRawScope::scopeSide > CameraRawScope::scopeSide / 2, "red sits on the right of the vectorscope");
}

void CameraRawTests::curveMixerAndGradingChangeOnlyTheirOwnTones()
{
    const QImage dark = image(4, 4, 0.12, 0.12, 0.12), light = image(4, 4, 0.62, 0.62, 0.62);
    CameraRawSettings settings;
    settings.curve.shadows = 100;
    const int darkGain = pixels(settings.apply(dark))[0][0] - int(std::lround(0.12 * 255));
    const int lightGain = pixels(settings.apply(light))[0][0] - int(std::lround(0.62 * 255));
    QVERIFY(darkGain > lightGain + 8);
    settings = CameraRawSettings();
    settings.curve.rgb = CameraRawCurveSettings::strongContrast();
    QVERIFY(pixels(settings.apply(image(4, 4, 0.25, 0.25, 0.25)))[0][0] < 55);
    settings = CameraRawSettings();
    settings.mixer.hue[0] = 100;
    const std::array<int, 4> shifted = pixels(settings.apply(image(4, 4, 1, 0, 0)))[0];
    QVERIFY(shifted[1] > shifted[2]);
    settings = CameraRawSettings();
    settings.grading.shadows.saturation = 100;
    const std::array<int, 4> gradedDark = pixels(settings.apply(dark))[0];
    const std::array<int, 4> gradedLight = pixels(settings.apply(image(4, 4, 1, 1, 1)))[0];
    QVERIFY(gradedDark[0] > gradedDark[1] + 5 && std::abs(gradedLight[0] - gradedLight[1]) <= 2);
    settings.grading.balance = 100;
    const std::array<int, 4> balanced = pixels(settings.apply(dark))[0];
    QVERIFY(balanced[0] - balanced[1] < gradedDark[0] - gradedDark[1]);
    settings = CameraRawSettings();
    settings.curve.shadows = 100;
    QVERIFY(pixels(settings.applying({.curve = false}).apply(dark)) == pixels(dark));
}

void CameraRawTests::detailSharpeningNoiseAndMaskingPreview()
{
    const QImage edge = step();
    CameraRawSettings settings;
    settings.detail.sharpenAmount = 150;
    settings.detail.sharpenRadius = 50;
    QVERIFY(pixels(settings.apply(edge)) != pixels(edge));
    settings = CameraRawSettings();
    settings.detail.noiseLuminance = 80;
    QVERIFY2(pixels(settings.apply(gray(8, 8))) == pixels(gray(8, 8)), "luminance NR leaves a flat field alone");
    settings.detail.sharpenMasking = 50;
    const auto mask = pixels(settings.apply(edge, std::nullopt, 1, 0, -1, true));
    QVERIFY(std::all_of(mask.begin(), mask.end(), [](const std::array<int, 4> &p) { return p[0] == p[1] && p[1] == p[2]; }));
    QVERIFY(settings.detail.adjusts());
}

void CameraRawTests::opticsDistortionDefringeAndDetailEye()
{
    const QImage stepped = checker();
    CameraRawSettings settings;
    settings.optics.distortion = 100;
    QVERIFY2(pixels(settings.apply(stepped)) != pixels(stepped), "distortion resamples pixels");
    const QImage purple = image(4, 4, 0.8, 0.2, 0.9);
    settings = CameraRawSettings();
    settings.optics.purpleAmount = 100;
    settings.optics.purpleHueLow = 250;
    settings.optics.purpleHueHigh = 320;
    QVERIFY(chroma(pixels(settings.apply(purple))[0]) < chroma(pixels(purple)[0]));
    settings = CameraRawSettings();
    settings.optics.removeChromaticAberration = true;
    QVERIFY(settings.optics.adjusts());
    settings = CameraRawSettings();
    settings.detail.sharpenAmount = 40;
    QVERIFY(pixels(settings.applying({.detail = false}).apply(step())) == pixels(step()));
}

QTEST_GUILESS_MAIN(CameraRawTests)
#include "CameraRawTests.moc"
