#include "CameraRawFixtures.h"
#include <QtTest>
#include <cmath>

// Swift 1.4.5: Camera Raw's curves as Photoshop's, the model.
using namespace CameraRawFixtures;

namespace {
// Swift's bend, written out apart from the code.
double bent(double tone, double lower, double low, double upper, double high)
{
    if (tone < lower && lower > 0)
        return lower * std::pow(tone / lower, std::pow(2.0, -low / 100 * 1.66));
    if (tone > upper && upper < 1)
        return 1 - (1 - upper) * std::pow((1 - tone) / (1 - upper), std::pow(2.0, high / 100 * 1.66));
    return tone;
}

// The kernel's table lookup: linear between entries.
double looked(const std::vector<float> &table, double value)
{
    const double at = value * 255;
    const int below = int(std::floor(at));
    if (below >= 255)
        return table[255];
    return table[size_t(below)] + (table[size_t(below + 1)] - table[size_t(below)]) * (at - below);
}

double luminance(const std::array<int, 4> &pixel)
{
    return 0.2126 * pixel[0] + 0.7152 * pixel[1] + 0.0722 * pixel[2];
}
}

class CameraRawCurveTests : public QObject {
    Q_OBJECT
private slots:
    void parametricCurveIsSmooth();
    void parametricCurveMatchesPhotoshop();
    void curveDeepensColorLikePhotoshop();
    void theCurvePassesThroughEachBentAnchor();
    void eachRegionBendsOnlyItsRange();
    void theToneTableRunsThroughThePointCurve();
    void refineSaturationEasesOrStrengthens();
};

void CameraRawCurveTests::parametricCurveIsSmooth()
{
    CameraRawCurveSettings curve;
    QCOMPARE(curve.parametric(0.3), 0.3);
    for (const std::array<double, 4> &amounts : {std::array{100.0, 0.0, 0.0, 0.0}, {0, 100, -100, 0}, {-100, 50, 100, -60}}) {
        curve.shadows = amounts[0];
        curve.darks = amounts[1];
        curve.lights = amounts[2];
        curve.highlights = amounts[3];
        std::vector<double> samples;
        for (int index = 0; index <= 200; ++index)
            samples.push_back(curve.parametric(index / 200.0));
        QCOMPARE(samples.front(), 0.0);
        QCOMPARE(samples.back(), 1.0);
        for (size_t index = 1; index < samples.size(); ++index)
            QVERIFY(samples[index] >= samples[index - 1] - 1e-9);
        // Finer sampling halves the slope's jumps; a corner keeps them.
        const auto jump = [&curve](int steps) {
            std::vector<double> slopes;
            double last = curve.parametric(0);
            for (int index = 1; index <= steps; ++index) {
                const double value = curve.parametric(double(index) / steps);
                slopes.push_back((value - last) * steps);
                last = value;
            }
            double worst = 0;
            for (size_t index = 1; index < slopes.size(); ++index)
                worst = std::max(worst, std::abs(slopes[index] - slopes[index - 1]));
            return worst;
        };
        QVERIFY(jump(400) < jump(200) * 0.7);
    }
    curve = CameraRawCurveSettings();
    curve.darks = 100;
    const double before = curve.parametric(0.4);
    curve.darkSplit = 70;
    QVERIFY(curve.parametric(0.4) > before);
}

void CameraRawCurveTests::parametricCurveMatchesPhotoshop()
{
    CameraRawCurveSettings curve;
    curve.darks = -51;
    curve.lights = 59;
    const std::pair<double, double> photoshop[] = {{0.093, 0.011}, {0.192, 0.089}, {0.267, 0.174}, {0.367, 0.310}, {0.491, 0.498},
                                                   {0.616, 0.698}, {0.690, 0.804}, {0.765, 0.886}, {0.840, 0.947}, {0.915, 0.982}};
    for (const auto &[tone, expected] : photoshop)
        QVERIFY2(std::abs(curve.parametric(tone) - expected) < 0.035, qPrintable(QString::number(curve.parametric(tone))));
}

void CameraRawCurveTests::curveDeepensColorLikePhotoshop()
{
    const QImage orange = image(2, 2, 0.85, 0.35, 0.1);
    CameraRawSettings settings;
    settings.curve.darks = -51;
    settings.curve.lights = 59;
    const std::array<int, 4> curved = pixels(settings.apply(orange))[0];
    QVERIFY2(curved[0] > 230 && curved[1] < 80, qPrintable(QString("%1 %2").arg(curved[0]).arg(curved[1])));
    settings.curve.refineSaturation = -100;
    const std::array<int, 4> brightness = pixels(settings.apply(orange))[0];
    QVERIFY(double(brightness[1]) / double(brightness[0]) > 0.35);
}

void CameraRawCurveTests::theCurvePassesThroughEachBentAnchor()
{
    CameraRawCurveSettings curve;
    curve.shadows = 40;
    curve.darks = -51;
    curve.lights = 59;
    curve.highlights = -30;
    curve.shadowSplit = 20;
    curve.darkSplit = 45;
    curve.lightSplit = 80;
    // Shadows and highlights first, then darks and lights.
    for (int index = 0; index <= 32; ++index) {
        const double x = index / 32.0;
        const double expected = bent(bent(x, 0.2, 40, 0.8, -30), 0.45, -51, 0.45, 59);
        QVERIFY2(std::abs(curve.parametric(x) - expected) < 1e-9, qPrintable(QString::number(x)));
    }
    // Between anchors the smooth curve, not a straight line.
    const double middle = (bent(bent(1 / 32.0, 0.2, 40, 0.8, -30), 0.45, -51, 0.45, 59) + bent(bent(2 / 32.0, 0.2, 40, 0.8, -30), 0.45, -51, 0.45, 59)) / 2;
    QVERIFY(std::abs(curve.parametric(1.5 / 32) - middle) > 1e-6);
}

void CameraRawCurveTests::eachRegionBendsOnlyItsRange()
{
    // Shadows and highlights bend past their dividers alone.
    CameraRawCurveSettings shadows;
    shadows.shadows = 100;
    QVERIFY(shadows.parametric(4 / 32.0) > 4 / 32.0 + 0.05);
    QVERIFY(std::abs(shadows.parametric(0.25) - 0.25) < 1e-9);
    QVERIFY(std::abs(shadows.parametric(0.5) - 0.5) < 1e-9);
    CameraRawCurveSettings highlights;
    highlights.highlights = -100;
    QVERIFY(highlights.parametric(28 / 32.0) < 28 / 32.0 - 0.05);
    QVERIFY(std::abs(highlights.parametric(0.75) - 0.75) < 1e-9);
    // Darks cover all below the middle; lights all above it.
    CameraRawCurveSettings darks;
    darks.darks = 100;
    QVERIFY(darks.parametric(4 / 32.0) > 4 / 32.0 + 0.02 && darks.parametric(12 / 32.0) > 12 / 32.0 + 0.05);
    QVERIFY(std::abs(darks.parametric(0.5) - 0.5) < 1e-9 && std::abs(darks.parametric(0.75) - 0.75) < 1e-9);
    CameraRawCurveSettings lights;
    lights.lights = -100;
    QVERIFY(lights.parametric(20 / 32.0) < 20 / 32.0 - 0.05 && lights.parametric(28 / 32.0) < 28 / 32.0 - 0.02);
    QVERIFY(std::abs(lights.parametric(0.25) - 0.25) < 1e-9);
    // A divider at an end bends nothing past it.
    CameraRawCurveSettings ends;
    ends.shadows = 100;
    ends.highlights = 100;
    ends.shadowSplit = 0;
    ends.lightSplit = 100;
    for (int index = 0; index <= 32; ++index)
        QVERIFY(std::abs(ends.parametric(index / 32.0) - index / 32.0) < 1e-9);
}

void CameraRawCurveTests::theToneTableRunsThroughThePointCurve()
{
    CameraRawCurveSettings curve;
    curve.lights = 59;
    curve.rgb = {{0, 0}, {0.5, 0.6}, {1, 1}};
    const std::vector<float> table = curve.toneTable();
    QCOMPARE(table.size(), size_t(256));
    CameraRawCurveSettings point;
    point.rgb = curve.rgb;
    // Without amounts, the point curve alone: 0.5 to 0.6.
    QVERIFY(std::abs(point.toneTable()[128] - 0.6) < 0.005);
    for (const int index : {0, 64, 128, 200, 255}) {
        const double expected = point.toneTable()[size_t(std::lround(curve.parametric(index / 255.0) * 255))];
        QVERIFY2(std::abs(table[size_t(index)] - expected) < 0.01, qPrintable(QString::number(index)));
    }
    QCOMPARE(table[255], 1.0f);
}

void CameraRawCurveTests::refineSaturationEasesOrStrengthens()
{
    const QImage orange = image(2, 2, 0.85, 0.35, 0.1);
    CameraRawSettings settings;
    settings.curve.darks = -51;
    settings.curve.lights = 59;
    // At zero each channel runs through the tone curve alone.
    const std::vector<float> table = settings.curve.toneTable();
    const std::array<int, 4> source = pixels(orange)[0], curved = pixels(settings.apply(orange))[0];
    for (int channel = 0; channel < 3; ++channel)
        QVERIFY2(std::abs(curved[size_t(channel)] - looked(table, source[size_t(channel)] / 255.0) * 255) <= 1, qPrintable(QString::number(channel)));
    // −100: the colour scaled to the curved brightness, hue kept.
    settings.curve.refineSaturation = -100;
    const std::array<int, 4> brightness = pixels(settings.apply(orange))[0];
    const double mapped = looked(table, luminance(source) / 255) * 255;
    QVERIFY(std::abs(luminance(brightness) - mapped) <= 1.5);
    QVERIFY(std::abs(double(brightness[1]) / brightness[0] - double(source[1]) / source[0]) < 0.02);
    // −50: halfway between the two.
    settings.curve.refineSaturation = -50;
    const std::array<int, 4> half = pixels(settings.apply(orange))[0];
    for (int channel = 0; channel < 3; ++channel)
        QVERIFY(std::abs(half[size_t(channel)] - (curved[size_t(channel)] + brightness[size_t(channel)]) / 2.0) <= 1.5);
    // +50: half again as far from luminance.
    settings.curve.refineSaturation = 50;
    const std::array<int, 4> stronger = pixels(settings.apply(orange))[0];
    const double lum = luminance(curved);
    for (int channel = 0; channel < 3; ++channel) {
        const double expected = std::clamp(lum + (curved[size_t(channel)] - lum) * 1.5, 0.0, 255.0);
        QVERIFY2(std::abs(stronger[size_t(channel)] - expected) <= 1.5, qPrintable(QString("%1 %2").arg(stronger[size_t(channel)]).arg(expected)));
    }
}

QTEST_MAIN(CameraRawCurveTests)
#include "CameraRawCurveTests.moc"
