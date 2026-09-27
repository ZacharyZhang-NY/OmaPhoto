#include "CameraRawFixtures.h"
#include <QtTest>
#include <cmath>
extern "C" {
#include "AdjustPixels.h"
}

// Detail, Optics, Geometry and Calibration, rule by rule.
using namespace CameraRawFixtures;

namespace {
// Stripes two pixels wide, mirrored about the middle column.
QImage stripes(int side = 24)
{
    QImage picture = BrushRaster::context(side, side, false);
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            const int from = std::min(x, side - 1 - x);
            picture.setPixelColor(x, y, (from / 2) % 2 ? QColor(230, 230, 230) : QColor(20, 20, 20));
        }
    return picture;
}

int transitions(const QImage &picture, int y)
{
    int count = 0;
    for (int x = 1; x < picture.width(); ++x)
        count += (qRed(picture.pixel(x, y)) > 128) != (qRed(picture.pixel(x - 1, y)) > 128);
    return count;
}

bool mirrored(const QImage &picture)
{
    for (int y = 0; y < picture.height(); ++y)
        for (int x = 0; x < picture.width() / 2; ++x)
            if (std::abs(qRed(picture.pixel(x, y)) - qRed(picture.pixel(picture.width() - 1 - x, y))) > 2)
                return false;
    return true;
}

QImage warped(const std::function<void(CameraRawGeometrySettings &)> &change, const QImage &picture = stripes())
{
    CameraRawGeometrySettings settings;
    change(settings);
    return settings.apply(picture);
}
}

class CameraRawLensTests : public QObject {
    Q_OBJECT
private slots:
    void detailAndOpticsSayWhenTheyAdjust();
    void opticsStayInTheirRanges();
    void theProfileAddsDistortionOnlyWhenOn();
    void verticalWidensTheTopAndHorizontalTheBottom();
    void aGuidedLineTurnsAsTheSameRotationWould();
    void aSecondLineTiltsAQuarter();
    void projectionAspectAndScaleShapeTheWarp();
    void constrainCropFillsTheFrame();
    void calibrationRunsAtItsProcessVersion();
};

void CameraRawLensTests::detailAndOpticsSayWhenTheyAdjust()
{
    QVERIFY(CameraRawDetailSettings{.noiseColor = 10}.adjusts() && CameraRawDetailSettings{.noiseLuminance = 10}.adjusts());
    QVERIFY(CameraRawOpticsSettings{.enableLensProfile = true}.adjusts() && CameraRawOpticsSettings{.vignetteAmount = -20}.adjusts());
    QVERIFY(!CameraRawDetailSettings().adjusts() && !CameraRawOpticsSettings().adjusts());
    QVERIFY(CameraRawGeometrySettings{.aspect = 20}.adjusts() && !CameraRawGeometrySettings().adjusts());
    // A guide a two-hundredth long reads as no line.
    CameraRawGeometrySettings shortLine{.upright = CameraRawUprightMode::guided};
    shortLine.guides = {{0.5, 0.5, 0.505, 0.5}};
    QVERIFY(!shortLine.adjusts() && shortLine.normalized().guides.empty());
    shortLine.guides = {{0.5, 0.5, 0.52, 0.5}};
    QVERIFY(shortLine.adjusts());
}

void CameraRawLensTests::opticsStayInTheirRanges()
{
    QCOMPARE(CameraRawDetailSettings{.sharpenAmount = 140}.normalized().sharpenAmount, 140.0);
    QCOMPARE(CameraRawDetailSettings{.sharpenAmount = 160}.normalized().sharpenAmount, 150.0);
    const CameraRawOpticsSettings swapped = CameraRawOpticsSettings{.purpleHueLow = 320, .purpleHueHigh = 250, .greenHueLow = 130, .greenHueHigh = 40,
                                                                    .vignetteAmount = -50}
                                                .normalized();
    QVERIFY(swapped.purpleHueLow == 250 && swapped.purpleHueHigh == 320 && swapped.greenHueLow == 40 && swapped.greenHueHigh == 130);
    QCOMPARE(swapped.vignetteAmount, -50.0);
    QCOMPARE(CameraRawGeometrySettings{.rotate = 60}.normalized().rotate, 45.0);
    QCOMPARE(CameraRawCalibrationSettings{.process = CameraRawProcessVersion::version2}.normalized().process, CameraRawProcessVersion::version2);
}

void CameraRawLensTests::theProfileAddsDistortionOnlyWhenOn()
{
    CameraRawOpticsSettings optics{.profileDistortion = 100, .distortion = 50};
    QCOMPARE(optics.distortionK(0.35), 0.175);
    optics.enableLensProfile = true;
    QCOMPARE(optics.distortionK(0.35), 0.175 + 0.35);
}

void CameraRawLensTests::verticalWidensTheTopAndHorizontalTheBottom()
{
    const QImage picture = stripes();
    const QImage vertical = warped([](CameraRawGeometrySettings &g) { g.vertical = 60; });
    QVERIFY(transitions(vertical, 1) < transitions(vertical, 22));
    QVERIFY(mirrored(vertical));
    const QImage horizontal = warped([](CameraRawGeometrySettings &g) { g.horizontal = 60; });
    QVERIFY(transitions(horizontal, 22) < transitions(horizontal, 1));
    QVERIFY(mirrored(horizontal));
    QVERIFY(transitions(picture, 1) == transitions(picture, 22));
}

void CameraRawLensTests::aGuidedLineTurnsAsTheSameRotationWould()
{
    const QImage picture = checker(24, 24);
    for (const double direction : {1.0, -1.0}) {
        // Steep: 82.9° either way, levelled by a quarter turn back.
        const QImage guided = warped(
            [direction](CameraRawGeometrySettings &g) {
                g.upright = CameraRawUprightMode::guided;
                g.guides = {{0.5, 0.5, 0.6, 0.5 + 0.8 * direction}};
            },
            picture);
        const double angle = std::atan2(0.8 * direction, 0.1) * 180 / std::numbers::pi;
        const double turn = -angle + (direction > 0 ? 90 : -90);
        const QImage turned = warped([turn](CameraRawGeometrySettings &g) { g.rotate = turn; }, picture);
        QVERIFY(pixels(guided) == pixels(turned));
    }
}

void CameraRawLensTests::aSecondLineTiltsAQuarter()
{
    const QImage picture = checker(24, 24);
    const auto guided = [&](CameraRawGeometryGuide second) {
        return warped(
            [second](CameraRawGeometrySettings &g) {
                g.upright = CameraRawUprightMode::guided;
                g.guides = {{0.2, 0.5, 0.8, 0.5}, second};
            },
            picture);
    };
    const auto manual = [&](double vertical, double horizontal) {
        return warped([=](CameraRawGeometrySettings &g) { g.vertical = vertical, g.horizontal = horizontal; }, picture);
    };
    QVERIFY(pixels(guided({0.5, 0.1, 0.55, 0.9})) == pixels(manual(25, 0)));
    QVERIFY(pixels(guided({0.5, 0.9, 0.55, 0.1})) == pixels(manual(-25, 0)));
    QVERIFY(pixels(guided({0.1, 0.5, 0.9, 0.6})) == pixels(manual(0, 25)));
    QVERIFY(pixels(guided({0.1, 0.5, 0.9, 0.4})) == pixels(manual(0, -25)));
}

void CameraRawLensTests::projectionAspectAndScaleShapeTheWarp()
{
    const QImage picture = checker(24, 24);
    const QImage rectilinear = warped(
        [](CameraRawGeometrySettings &g) {
            g.projection = CameraRawProjection::rectilinear;
            g.vertical = 40;
        },
        picture);
    QVERIFY(pixels(rectilinear) == pixels(warped([](CameraRawGeometrySettings &g) { g.vertical = 40 * 0.55; }, picture)));
    QVERIFY(pixels(warped([](CameraRawGeometrySettings &g) { g.aspect = 50; }, picture)) != pixels(picture));
    QVERIFY(pixels(warped([](CameraRawGeometrySettings &g) { g.scale = 50; }, picture)) != pixels(picture));
}

void CameraRawLensTests::constrainCropFillsTheFrame()
{
    // Shrunk to its middle, the picture leaves every edge empty.
    const QImage open = warped([](CameraRawGeometrySettings &g) { g.scale = -50; });
    QCOMPARE(qAlpha(open.pixel(0, 0)), 0);
    const QImage cropped = warped([](CameraRawGeometrySettings &g) { g.scale = -50, g.constrainCrop = true; });
    QVERIFY(qAlpha(cropped.pixel(0, 0)) > 0 && qAlpha(cropped.pixel(23, 0)) > 0);
}

void CameraRawLensTests::calibrationRunsAtItsProcessVersion()
{
    // Each primary on top, so every field shows.
    QImage red = BrushRaster::context(3, 1, false);
    red.setPixelColor(0, 0, QColor(230, 120, 40));
    red.setPixelColor(1, 0, QColor(60, 210, 110));
    red.setPixelColor(2, 0, QColor(90, 50, 220));
    for (const CameraRawProcessVersion version : {CameraRawProcessVersion::version1, CameraRawProcessVersion::version4, CameraRawProcessVersion::version6}) {
        CameraRawSettings settings;
        settings.calibration = {version, 20, 80, 30, -40, 60, -70, 50};
        QImage direct = red.copy();
        adjust_camera_raw_calibration(direct.bits(), 3, 1, size_t(direct.bytesPerLine()), 20, 80, 30, -40, 60, -70, 50, int(version) + 1);
        QVERIFY(pixels(settings.apply(red)) == pixels(direct));
    }
    QCOMPARE(rawValue(CameraRawProcessVersion::version3), QString("Version 3"));
}

QTEST_GUILESS_MAIN(CameraRawLensTests)
#include "CameraRawLensTests.moc"
