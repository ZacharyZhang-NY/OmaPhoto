#include "CameraRawFixtures.h"
#include <QtTest>
#include <cmath>

// Curve, Color Mixer and Color Grading, rule by rule.
using namespace CameraRawFixtures;

namespace {
using Points = std::vector<CurvePoint>;

Points repaired(const Points &points)
{
    CameraRawCurveSettings curve;
    curve.rgb = points;
    return curve.normalized().rgb;
}

int shadowTint(double balance)
{
    CameraRawSettings settings;
    settings.grading.shadows.saturation = 100;
    settings.grading.balance = balance;
    const std::array<int, 4> pixel = pixels(settings.apply(image(4, 4, 0.12, 0.12, 0.12)))[0];
    return pixel[0] - pixel[1];
}
}

class CameraRawColorTests : public QObject {
    Q_OBJECT
private slots:
    void curvesAreRepairedIntoOrder();
    void aToneFindsItsRegion();
    void aNudgeMovesTheFirstNearestPointWithinRange();
    void splitsAndAmountsStayInTheirRanges();
    void mixerFamiliesOverlapRoundTheWheel();
    void pointColorsStayInTheirRanges();
    void gradingWheelsAndBalanceReachTheKernel();
    void aPointColorReachesOnlyItsRange();
    void everyGroupSaysWhenItAdjusts();
};

void CameraRawColorTests::curvesAreRepairedIntoOrder()
{
    QCOMPARE(repaired({{1, 1}, {0.5, 0.2}, {0, 0}}), (Points{{0, 0}, {0.5, 0.2}, {1, 1}}));
    QCOMPARE(repaired({{0.3, 0.4}}), CameraRawCurveSettings::linear());
    QCOMPARE(repaired({{0, 0}, {std::nan(""), 0.5}, {1, 1}}), CameraRawCurveSettings::linear());
    // Ends pinned to 0 and 1, heights clamped.
    QCOMPARE(repaired({{0.1, -0.5}, {0.9, 1.5}}), CameraRawCurveSettings::linear());
    // Inner points keep a hundredth from ends and neighbours.
    QCOMPARE(repaired({{0, 0}, {0.995, 0.5}, {1, 1}}), (Points{{0, 0}, {0.99, 0.5}, {1, 1}}));
    QCOMPARE(repaired({{0, 0}, {0.5, 0.3}, {0.505, 0.6}, {1, 1}}), (Points{{0, 0}, {0.5, 0.3}, {1, 1}}));
    QCOMPARE(repaired({{0, 0}, {0.004, 0.3}, {1, 1}}), CameraRawCurveSettings::linear());
    // A curve of one point passes its input through.
    CameraRawCurveSettings curve;
    const std::vector<float> table = curve.channelTable({{0.5, 0.7}});
    QCOMPARE(table[100], float(100 / 255.0));
}

void CameraRawColorTests::aToneFindsItsRegion()
{
    // Region picks the amount a tone moves.
    CameraRawCurveSettings regions;
    regions.region(0.1) = 1;
    regions.region(0.3) = 2;
    regions.region(0.6) = 3;
    regions.region(0.9) = 4;
    QVERIFY(regions.shadows == 1 && regions.darks == 2 && regions.lights == 3 && regions.highlights == 4);
}

void CameraRawColorTests::aNudgeMovesTheFirstNearestPointWithinRange()
{
    const CameraRawCurveSettings curve;
    // Halfway between two points, the first is nearest.
    QCOMPARE(curve.nudged(CameraRawPointChannel::green, 0.5, 0.2).green, (Points{{0, 0.2}, {1, 1}}));
    QCOMPARE(curve.nudged(CameraRawPointChannel::blue, 0.9, 5).blue, (Points{{0, 0}, {1, 1}}));
    QCOMPARE(curve.nudged(CameraRawPointChannel::rgb, 0.1, -5).rgb, (Points{{0, 0}, {1, 1}}));
    QCOMPARE(curve.nudged(CameraRawPointChannel::rgb, 0.9, -0.25).rgb, (Points{{0, 0}, {1, 0.75}}));
}

void CameraRawColorTests::splitsAndAmountsStayInTheirRanges()
{
    CameraRawCurveSettings curve;
    curve.shadowSplit = 25;
    curve.darkSplit = 20;
    curve.lightSplit = 26;
    curve.refineSaturation = 150;
    const CameraRawCurveSettings normal = curve.normalized();
    QVERIFY(normal.darkSplit == 27 && normal.lightSplit == 29 && normal.refineSaturation == 100);
    curve.shadowSplit = 1;
    QCOMPARE(curve.normalized().shadowSplit, 5.0);
}

void CameraRawColorTests::mixerFamiliesOverlapRoundTheWheel()
{
    const std::array<double, 8> near = CameraRawMixerSettings::weights(350);
    QCOMPARE(near[0], 0.75);
    QCOMPARE(near[7], 0.0);
    const std::array<double, 8> orange = CameraRawMixerSettings::weights(30);
    QVERIFY(orange[0] == 0.25 && orange[1] == 1 && orange[2] == 0.25 && orange[3] == 0);
    CameraRawMixerSettings mixer;
    mixer.points = std::vector<CameraRawPointColor>(9);
    QCOMPARE(mixer.normalized().points.size(), size_t(8));
}

void CameraRawColorTests::pointColorsStayInTheirRanges()
{
    const CameraRawPointColor wild{.hue = 400, .saturation = 2, .luminance = -1, .hueShift = 300, .saturationShift = -300, .luminanceShift = 101,
                                   .hueRange = 200, .saturationRange = 0.01, .luminanceRange = 2};
    const CameraRawPointColor normal = wild.normalized();
    QVERIFY(normal.hue == 360 && normal.saturation == 1 && normal.luminance == 0 && normal.hueShift == 100 && normal.saturationShift == -100);
    QVERIFY(normal.luminanceShift == 100 && normal.hueRange == 180 && normal.saturationRange == 0.05 && normal.luminanceRange == 1);
    QCOMPARE(CameraRawPointColor{.hueRange = 2}.normalized().hueRange, 5.0);
    const CameraRawGradeWheel wheel = CameraRawGradeWheel{400, -20, -150}.normalized();
    QVERIFY(wheel.hue == 360 && wheel.saturation == 0 && wheel.luminance == -100);
    CameraRawGradingSettings grading;
    grading.blending = 150;
    grading.balance = -150;
    QVERIFY(grading.normalized().blending == 100 && grading.normalized().balance == -100);
}

void CameraRawColorTests::gradingWheelsAndBalanceReachTheKernel()
{
    // Balance runs in hundredths: half is between none and all.
    const int none = shadowTint(0), half = shadowTint(50), all = shadowTint(100);
    QVERIFY2(none > half && half > all, qPrintable(QStringLiteral("%1 %2 %3").arg(none).arg(half).arg(all)));
    // Luminance runs in hundredths too: measured once, then held.
    const auto lifted = [](double luminance) {
        CameraRawSettings settings;
        settings.grading.shadows.luminance = luminance;
        return pixels(settings.apply(image(4, 4, 0.12, 0.12, 0.12)))[0][0];
    };
    QVERIFY(lifted(0) == 31 && lifted(50) == 53 && lifted(100) == 76);
}

void CameraRawColorTests::aPointColorReachesOnlyItsRange()
{
    CameraRawSettings settings;
    settings.mixer.points = {CameraRawPointColor{.hue = 0, .saturation = 1, .luminance = 0.5, .luminanceShift = -100, .hueRange = 30}};
    const std::array<int, 4> red = pixels(settings.apply(image(4, 4, 1, 0, 0)))[0];
    QVERIFY(red[0] < 240);
    // Fifty degrees off lies outside thirty either way.
    const QImage orange = image(4, 4, 1, 50 / 60.0, 0);
    QVERIFY(pixels(settings.apply(orange)) == pixels(orange));
}

void CameraRawColorTests::everyGroupSaysWhenItAdjusts()
{
    CameraRawCurveSettings refined;
    refined.refineSaturation = 10;
    CameraRawCurveSettings blue;
    blue.blue = {{0, 0.1}, {1, 1}};
    QVERIFY(refined.adjusts() && blue.adjusts() && !CameraRawCurveSettings().adjusts());
    CameraRawMixerSettings lighter;
    lighter.points = {CameraRawPointColor{.luminanceShift = 10}};
    QVERIFY(lighter.adjusts() && !CameraRawMixerSettings{.points = {CameraRawPointColor{.hue = 40}}}.adjusts());
    CameraRawGradingSettings brighter;
    brighter.global.luminance = 10;
    CameraRawGradingSettings balanced;
    balanced.balance = 50;
    QVERIFY(brighter.adjusts() && !balanced.adjusts());
}

QTEST_GUILESS_MAIN(CameraRawColorTests)
#include "CameraRawColorTests.moc"
