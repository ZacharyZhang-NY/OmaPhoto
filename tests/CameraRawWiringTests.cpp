#include "CameraRawFixtures.h"
#include "Document/Filters.h"
#include <QPainter>
#include <QtTest>
#include <cmath>
extern "C" {
#include "AdjustPixels.h"
}

// Each setting reaches its kernel argument, each eye its fields.
using namespace CameraRawFixtures;

namespace {
// Colours that differ everywhere, so every kernel argument shows.
QImage noise(int side = 32)
{
    QImage picture = BrushRaster::context(side, side, false);
    quint32 state = 7;
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            std::array<int, 3> channel{};
            for (int &value : channel) {
                state = state * 1664525 + 1013904223;
                value = int(state >> 24);
            }
            picture.setPixelColor(x, y, QColor(channel[0], channel[1], channel[2]));
        }
    return picture;
}

QImage kernel(const QImage &picture, const std::function<void(uchar *, size_t, size_t, size_t)> &run)
{
    QImage copy = picture.copy();
    run(copy.bits(), size_t(copy.width()), size_t(copy.height()), size_t(copy.bytesPerLine()));
    return copy;
}

// The picture drawn under a transform, as the warp does.
QImage placed(const QImage &picture, const QTransform &transform)
{
    QImage result = BrushRaster::context(picture.width(), picture.height(), false);
    QPainter painter(&result);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    painter.setTransform(transform);
    painter.drawImage(QRectF(0, 0, picture.width(), picture.height()), picture);
    return result;
}

int farthest(const std::vector<std::array<int, 4>> &left, const std::vector<std::array<int, 4>> &right)
{
    int most = 0;
    for (size_t index = 0; index < left.size(); ++index)
        for (size_t channel = 0; channel < 4; ++channel)
            most = std::max(most, std::abs(left[index][channel] - right[index][channel]));
    return most;
}

double decode(double encoded)
{
    return encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
}
}

class CameraRawWiringTests : public QObject {
    Q_OBJECT
private slots:
    void aClipViewShowsOnAnUntouchedGrade();
    void autoAveragesLinearLight();
    void eachEyeClearsEveryFieldOfItsGroup();
    void effectsReachTheKernelInOrder();
    void opticsAndDetailReachTheKernelsInOrder();
    void offsetAndAspectPlaceThePicture();
    void constrainCropFitsTheWholeFrame();
    void theScopeCountsEachChannelAndHue();
    void curveMixerAndGradingReachTheKernelInOrder();
    void theSharpenMaskAndEachLensFlagReachTheirKernels();
    void grainScalesByThePreview();
    void turnVerticalAndScalePlaceThePicture();
};

void CameraRawWiringTests::aClipViewShowsOnAnUntouchedGrade()
{
    const QImage picture = noise();
    const QImage clipped = CameraRawSettings().apply(picture, CameraRawClipping::highlights);
    const QImage direct = kernel(picture, [](uchar *bytes, size_t w, size_t h, size_t s) {
        adjust_camera_raw(bytes, w, h, s, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, int(CameraRawClipping::highlights));
    });
    QVERIFY(pixels(clipped) == pixels(direct) && pixels(clipped) != pixels(picture));
}

void CameraRawWiringTests::autoAveragesLinearLight()
{
    QImage picture = BrushRaster::context(2, 1, false);
    picture.setPixelColor(0, 0, QColor(200, 100, 50));
    picture.setPixelColor(1, 0, QColor(20, 60, 120));
    const auto mean = [](const std::function<double(double)> &read, int left, int right) { return (read(left / 255.0) + read(right / 255.0)) / 2; };
    const auto same = [](double value) { return value; };
    const CameraRawSettings::Balance linear =
        CameraRawSettings::neutralizeLinear(mean(decode, 200, 20), mean(decode, 100, 60), mean(decode, 50, 120)).value();
    const CameraRawSettings::Balance encoded =
        CameraRawSettings::neutralizeLinear(mean(same, 200, 20), mean(same, 100, 60), mean(same, 50, 120)).value();
    const CameraRawSettings::Balance automatic = CameraRawSettings::autoBalance(picture).value();
    QVERIFY(std::abs(automatic.temperature - linear.temperature) < 1e-9 && std::abs(automatic.tint - linear.tint) < 1e-9);
    QVERIFY(std::abs(automatic.temperature - encoded.temperature) > 1);
}

void CameraRawWiringTests::eachEyeClearsEveryFieldOfItsGroup()
{
    CameraRawSettings all;
    double next = 1;
    for (double CameraRawSettings::*field :
         {&CameraRawSettings::exposure, &CameraRawSettings::contrast, &CameraRawSettings::highlights, &CameraRawSettings::shadows,
          &CameraRawSettings::whites, &CameraRawSettings::blacks, &CameraRawSettings::temperature, &CameraRawSettings::tint,
          &CameraRawSettings::vibrance, &CameraRawSettings::saturation, &CameraRawSettings::texture, &CameraRawSettings::clarity,
          &CameraRawSettings::dehaze, &CameraRawSettings::glow, &CameraRawSettings::vignetteAmount, &CameraRawSettings::grainAmount})
        all.*field = next++;
    const auto cleared = [&](std::initializer_list<double CameraRawSettings::*> fields) {
        CameraRawSettings expected = all;
        for (double CameraRawSettings::*field : fields)
            expected.*field = 0;
        return expected;
    };
    QVERIFY(all.applying({.light = false})
            == cleared({&CameraRawSettings::exposure, &CameraRawSettings::contrast, &CameraRawSettings::highlights, &CameraRawSettings::shadows,
                        &CameraRawSettings::whites, &CameraRawSettings::blacks}));
    QVERIFY(all.applying({.color = false})
            == cleared({&CameraRawSettings::temperature, &CameraRawSettings::tint, &CameraRawSettings::vibrance, &CameraRawSettings::saturation}));
    QVERIFY(all.applying({.effects = false})
            == cleared({&CameraRawSettings::texture, &CameraRawSettings::clarity, &CameraRawSettings::dehaze, &CameraRawSettings::glow,
                        &CameraRawSettings::vignetteAmount, &CameraRawSettings::grainAmount}));
}

void CameraRawWiringTests::effectsReachTheKernelInOrder()
{
    const QImage picture = noise();
    CameraRawSettings settings;
    settings.texture = 30, settings.clarity = -20, settings.dehaze = 15, settings.glow = 40, settings.glowStyle = CameraRawGlowStyle::bloom;
    settings.glowRange = 10, settings.glowSpread = -30, settings.glowWarmth = 25, settings.vignetteAmount = -60;
    settings.vignetteStyle = CameraRawVignetteStyle::colorPriority, settings.vignetteMidpoint = 30, settings.vignetteRoundness = 20;
    settings.vignetteFeather = 70, settings.vignetteHighlights = 40;
    const QImage direct = kernel(picture, [](uchar *bytes, size_t w, size_t h, size_t s) {
        adjust_camera_raw_effects(bytes, w, h, s, 30, -20, 15, 40, int(CameraRawGlowStyle::bloom), 10, -30, 25, -60, 30, 20, 70, 40,
                                  int(CameraRawVignetteStyle::colorPriority), 1);
    });
    QVERIFY(pixels(settings.apply(picture)) == pixels(direct));
}

void CameraRawWiringTests::opticsAndDetailReachTheKernelsInOrder()
{
    const QImage picture = noise();
    CameraRawSettings settings;
    settings.optics = {.removeChromaticAberration = true, .enableLensProfile = true, .profileDistortion = 60, .profileVignetting = 40,
                       .distortion = 20, .purpleAmount = 30, .purpleHueLow = 260, .purpleHueHigh = 320, .greenAmount = 40, .greenHueLow = 50,
                       .greenHueHigh = 130, .vignetteAmount = -45, .vignetteMidpoint = 30};
    settings.detail = {.sharpenAmount = 80, .sharpenRadius = 30, .sharpenDetail = 60, .sharpenMasking = 20, .noiseLuminance = 40,
                       .noiseLuminanceDetail = 70, .noiseLuminanceContrast = 30, .noiseColor = 50, .noiseColorDetail = 20,
                       .noiseColorSmoothness = 80};
    const double k = settings.optics.distortionK(PixelFilter::lensStrength);
    const QImage direct = kernel(picture, [k](uchar *bytes, size_t w, size_t h, size_t s) {
        adjust_camera_raw_optics(bytes, w, h, s, 1, 1, 60, 40, k, 30, 260, 320, 40, 50, 130, -45, 30, 1);
        adjust_camera_raw_detail(bytes, w, h, s, 80, 30, 60, 20, 40, 70, 30, 50, 20, 80, 1);
    });
    QVERIFY(pixels(settings.apply(picture)) == pixels(direct));
}

void CameraRawWiringTests::offsetAndAspectPlaceThePicture()
{
    const QImage picture = checker(24, 24);
    const auto warped = [&](const std::function<void(CameraRawGeometrySettings &)> &change) {
        CameraRawGeometrySettings geometry;
        change(geometry);
        return pixels(geometry.apply(picture));
    };
    // Offset X slides; Offset Y stretches from the middle.
    QVERIFY(farthest(warped([](CameraRawGeometrySettings &g) { g.offsetX = 40; }), pixels(placed(picture, QTransform::fromTranslate(24 * 0.06, 0))))
            <= 1);
    const double stretch = (24 + 2 * 24 * 0.06) / 24;
    QVERIFY(farthest(warped([](CameraRawGeometrySettings &g) { g.offsetY = 40; }),
                     pixels(placed(picture, QTransform::fromTranslate(12, 12).scale(1, stretch).translate(-12, -12))))
            <= 1);
    // Aspect widens by its factor and flattens by the same.
    QVERIFY(farthest(warped([](CameraRawGeometrySettings &g) { g.aspect = 50; }),
                     pixels(placed(picture, QTransform::fromTranslate(12, 12).scale(1.25, 1 / 1.25).translate(-12, -12))))
            <= 1);
}

void CameraRawWiringTests::constrainCropFitsTheWholeFrame()
{
    // A wide box fills the width, the top clear.
    CameraRawGeometrySettings wide{.aspect = 50, .scale = -50, .constrainCrop = true};
    const QImage filled = wide.apply(checker(24, 24));
    QVERIFY(qAlpha(filled.pixel(0, 12)) > 0 && qAlpha(filled.pixel(23, 12)) > 0);
    QCOMPARE(qAlpha(filled.pixel(12, 0)), 0);
    // A box filling one side alone is still centred.
    CameraRawGeometrySettings tall{.aspect = -50, .offsetX = 50};
    const QImage loose = tall.apply(checker(24, 24));
    tall.constrainCrop = true;
    const QImage centred = tall.apply(checker(24, 24));
    QCOMPARE(qAlpha(loose.pixel(3, 12)), 0);
    QVERIFY(qAlpha(centred.pixel(3, 12)) > 0);
}

void CameraRawWiringTests::theScopeCountsEachChannelAndHue()
{
    QImage picture = BrushRaster::context(3, 1, false);
    picture.setPixelColor(0, 0, QColor(0, 255, 0, 128));
    picture.setPixelColor(1, 0, QColor(0, 0, 255));
    picture.setPixelColor(2, 0, QColor(255, 0, 255));
    const CameraRawScope scope = CameraRawScope::make(picture).value();
    // Counts weigh by alpha: the green pixel is half.
    const double half = 128 / 255.0;
    const auto near = [](double left, double right) { return std::abs(left - right) < 1e-9; };
    QVERIFY(near(scope.red[0], 1 + half) && near(scope.red[255], 1));
    QVERIFY(near(scope.green[255], half) && near(scope.green[0], 2));
    QVERIFY(near(scope.blue[255], 2) && near(scope.blue[0], half));
    const auto cell = [&](int column, int line) { return scope.vectorscope[size_t(line * CameraRawScope::scopeSide + column)]; };
    // Green at 120°, blue at 240°, magenta at 300°.
    QVERIFY(std::abs(cell(16, 58) - 128 / 255.0) < 1e-9);
    QCOMPARE(cell(16, 5), 1.0);
    QCOMPARE(cell(47, 5), 1.0);
}

void CameraRawWiringTests::curveMixerAndGradingReachTheKernelInOrder()
{
    const QImage picture = noise();
    CameraRawSettings settings;
    CameraRawCurveSettings &curve = settings.curve;
    curve.lights = 30, curve.shadows = -20, curve.refineSaturation = 40;
    curve.rgb = {{0, 0.05}, {0.5, 0.6}, {1, 0.95}};
    curve.red = {{0, 0}, {0.3, 0.45}, {1, 1}};
    curve.green = {{0, 0.1}, {1, 0.8}};
    curve.blue = {{0, 0}, {0.7, 0.5}, {1, 1}};
    for (size_t index = 0; index < 8; ++index) {
        settings.mixer.hue[index] = 5 + double(index);
        settings.mixer.saturation[index] = -30 + 7 * double(index);
        settings.mixer.luminance[index] = 20 - 4 * double(index);
    }
    settings.mixer.points = {{200, 0.6, 0.4, 25, -35, 15, 40, 0.5, 0.3, false}, {40, 0.8, 0.6, -10, 20, -45, 60, 0.3, 0.6, false}};
    settings.grading = {{210, 40, -20}, {100, 25, 10}, {30, 55, 15}, {300, 10, -5}, 70, -30};
    const CameraRawCurveSettings shape = curve.normalized();
    const std::vector<float> luma = shape.lumaTable(), red = shape.channelTable(shape.red), green = shape.channelTable(shape.green),
                             blue = shape.channelTable(shape.blue);
    std::vector<float> mixer;
    for (size_t index = 0; index < 8; ++index)
        mixer.push_back(float((5 + double(index)) / 100));
    for (size_t index = 0; index < 8; ++index)
        mixer.push_back(float((-30 + 7 * double(index)) / 100));
    for (size_t index = 0; index < 8; ++index)
        mixer.push_back(float((20 - 4 * double(index)) / 100));
    const std::vector<float> points{float(200 / 360.0), 0.6f, 0.4f, 0.25f, -0.35f, 0.15f, float(40 / 360.0), 0.5f, 0.3f,
                                    float(40 / 360.0),  0.8f, 0.6f, -0.1f, 0.2f,  -0.45f, float(60 / 360.0), 0.3f, 0.6f};
    const std::vector<float> grade{float(210 / 360.0), 0.4f,  -0.2f, float(100 / 360.0), 0.25f, 0.1f,
                                   float(30 / 360.0),  0.55f, 0.15f, float(300 / 360.0), 0.1f,  -0.05f};
    for (const int visualize : {-1, 1}) {
        const QImage direct = kernel(picture, [&](uchar *bytes, size_t w, size_t h, size_t stride) {
            adjust_camera_raw_curve_color(bytes, w, h, stride, luma.data(), red.data(), green.data(), blue.data(), 0.4, mixer.data(), 2, points.data(),
                                          grade.data(), 0.7, -0.3, visualize);
        });
        QVERIFY(pixels(settings.apply(picture, std::nullopt, 1, 0, visualize)) == pixels(direct));
    }
}

void CameraRawWiringTests::theSharpenMaskAndEachLensFlagReachTheirKernels()
{
    const QImage picture = noise();
    CameraRawSettings masked;
    masked.detail = {.sharpenAmount = 50, .sharpenRadius = 30, .sharpenDetail = 60, .sharpenMasking = 20};
    const QImage mask = kernel(picture, [](uchar *bytes, size_t w, size_t h, size_t s) { adjust_camera_raw_sharpen_mask_overlay(bytes, w, h, s, 30, 60, 20, 1); });
    QVERIFY(pixels(masked.apply(picture, std::nullopt, 1, 0, -1, true)) == pixels(mask));
    const QImage half = kernel(picture, [](uchar *bytes, size_t w, size_t h, size_t s) { adjust_camera_raw_sharpen_mask_overlay(bytes, w, h, s, 80, 60, 20, 0.25); });
    masked.detail.sharpenRadius = 80;
    QVERIFY(pixels(masked.apply(picture, std::nullopt, 0.25, 0, -1, true)) == pixels(half));
    for (const bool chromatic : {true, false}) {
        CameraRawSettings lens;
        lens.optics.removeChromaticAberration = chromatic;
        lens.optics.enableLensProfile = !chromatic;
        const double k = lens.optics.distortionK(PixelFilter::lensStrength);
        const QImage direct = kernel(picture, [&](uchar *bytes, size_t w, size_t h, size_t s) {
            adjust_camera_raw_optics(bytes, w, h, s, chromatic ? 1 : 0, chromatic ? 0 : 1, 100, 100, k, 0, 270, 310, 0, 60, 120, 0, 50, 1);
        });
        QVERIFY(pixels(lens.apply(picture)) == pixels(direct));
    }
}

void CameraRawWiringTests::grainScalesByThePreview()
{
    const QImage picture = noise();
    CameraRawSettings settings;
    settings.grainAmount = 40;
    const QImage direct = kernel(picture, [](uchar *bytes, size_t w, size_t h, size_t s) { adjust_grain(bytes, w, h, s, 40, 5.375, 50, 3, 0, 0, 2); });
    QVERIFY(pixels(settings.apply(picture, std::nullopt, 0.5, 3)) == pixels(direct));
    // A dull colour plots nearer the middle: chroma over value.
    QImage dull = BrushRaster::context(1, 1, false);
    dull.setPixelColor(0, 0, QColor(128, 64, 64));
    QCOMPARE(CameraRawScope::make(dull).value().vectorscope[size_t(32 * CameraRawScope::scopeSide + 47)], 1.0);
}

void CameraRawWiringTests::turnVerticalAndScalePlaceThePicture()
{
    const QImage picture = checker(24, 24);
    const auto warped = [&](const std::function<void(CameraRawGeometrySettings &)> &change) {
        CameraRawGeometrySettings geometry;
        change(geometry);
        return pixels(geometry.apply(picture));
    };
    // A positive turn goes counterclockwise on screen, as Core Image's.
    QVERIFY(farthest(warped([](CameraRawGeometrySettings &g) { g.rotate = 10; }),
                     pixels(placed(picture, QTransform::fromTranslate(12, 12).rotate(-10).translate(-12, -12))))
            <= 1);
    QVERIFY(farthest(warped([](CameraRawGeometrySettings &g) { g.scale = 50; }),
                     pixels(placed(picture, QTransform::fromTranslate(12, 12).scale(1.5, 1.5).translate(-12, -12))))
            <= 1);
    // Vertical 40 widens the top by 0.072 of the width.
    const double v = 0.4 * 24 * 0.18;
    QTransform trapezoid;
    QVERIFY(QTransform::quadToQuad(QPolygonF(QRectF(0, 0, 24, 24)).mid(0, 4),
                                   QPolygonF({QPointF(-v, 0), QPointF(24 + v, 0), QPointF(24, 24), QPointF(0, 24)}), trapezoid));
    QVERIFY(farthest(warped([](CameraRawGeometrySettings &g) { g.vertical = 40; }), pixels(placed(picture, trapezoid))) <= 1);
    QTransform wideBottom;
    QVERIFY(QTransform::quadToQuad(QPolygonF(QRectF(0, 0, 24, 24)).mid(0, 4),
                                   QPolygonF({QPointF(0, 0), QPointF(24, 0), QPointF(24 + v, 24), QPointF(-v, 24)}), wideBottom));
    QVERIFY(farthest(warped([](CameraRawGeometrySettings &g) { g.horizontal = 40; }), pixels(placed(picture, wideBottom))) <= 1);
}

QTEST_GUILESS_MAIN(CameraRawWiringTests)
#include "CameraRawWiringTests.moc"
