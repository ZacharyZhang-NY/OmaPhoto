#include "Document/Curves.h"
#include "Document/ImageAdjustments.h"
#include "IO/ProjectStore.h"
#include <QtTest>
#include <cmath>

// The adjustments' models at their edges: ranges, curves, tables.
namespace {
QImage filled(int width, int height, QColor colour)
{
    QImage result(width, height, QImage::Format_RGBA8888_Premultiplied);
    result.fill(colour);
    return result;
}

std::array<int, 4> first(const QImage &image)
{
    const uchar *pixel = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied).constBits();
    return {pixel[0], pixel[1], pixel[2], pixel[3]};
}

// Whether a call throws ProjectError(invalid).
template <typename Call> bool refused(Call call)
{
    try {
        call();
    } catch (const ProjectError &error) {
        return error.kind == ProjectError::Kind::invalid;
    }
    return false;
}

CurvesSettings withCurve(size_t channel, std::vector<CurvePoint> points)
{
    CurvesSettings settings;
    settings.channels[channel] = std::move(points);
    return settings;
}
}

class ImageAdjustmentModelTests : public QObject {
    Q_OBJECT
private slots:
    void curvesAreValidAsSwifts();
    void curvesInterpolateWithoutOvershoot();
    void curvesRunEachChannelThenTheComposite();
    void exposureKeepsItsRangesAndTable();
    void gradientMapsKeepTheirEndsAndColours();
    void grainKeepsItsRangesSeedAndPlace();
    void theKernelSeesPremultipliedRgba();
};

void ImageAdjustmentModelTests::curvesAreValidAsSwifts()
{
    QVERIFY(CurvesSettings().isValid());
    CurvesSettings three;
    three.channels.pop_back();
    QVERIFY(!three.isValid());
    QVERIFY(!withCurve(0, {{0, 0}}).isValid() && !withCurve(0, {}).isValid());
    std::vector<CurvePoint> many{{0, 0}};
    for (int index = 1; index < 31; ++index)
        many.push_back({double(index), 10});
    many.push_back({255, 255});
    QCOMPARE(many.size(), size_t(32));
    QVERIFY(withCurve(1, many).isValid());
    many.insert(many.end() - 1, CurvePoint{100, 10});
    std::sort(many.begin(), many.end(), [](const CurvePoint &a, const CurvePoint &b) { return a.x < b.x; });
    QVERIFY(!withCurve(1, many).isValid());
    // Ends at 0 and 255, x rising, values in range.
    QVERIFY(!withCurve(2, {{1, 0}, {255, 255}}).isValid());
    QVERIFY(!withCurve(2, {{0, 0}, {254, 255}}).isValid());
    QVERIFY(!withCurve(3, {{0, 0}, {100, 10}, {100, 20}, {255, 255}}).isValid());
    QVERIFY(!withCurve(3, {{0, 0}, {100, 10}, {50, 20}, {255, 255}}).isValid());
    QVERIFY(!withCurve(3, {{0, 0}, {100, 256}, {255, 255}}).isValid());
    QVERIFY(!withCurve(3, {{0, -1}, {255, 255}}).isValid());
    QVERIFY(!withCurve(0, {{0, 0}, {100, std::nan("")}, {255, 255}}).isValid());
    QVERIFY(withCurve(0, {{0, 255}, {255, 0}}).isValid());
}

void ImageAdjustmentModelTests::curvesInterpolateWithoutOvershoot()
{
    const CurvesSettings identity;
    for (const double x : {0.0, 1.0, 64.5, 200.0, 255.0})
        QCOMPARE(identity.value(x, 0), x);
    // Swift's Hermite: secant ends, harmonic means inside.
    const CurvesSettings lifted = withCurve(0, {{0, 0}, {128, 200}, {255, 255}});
    const double inner = 2 / (1 / (200.0 / 128) + 1 / (55.0 / 127));
    QVERIFY(std::abs(lifted.value(64, 0) - (0.125 * 128 * (200.0 / 128) + 100 - 0.125 * 128 * inner)) < 1e-9);
    const double t = 63.5 / 127;
    QVERIFY(std::abs(lifted.value(191.5, 0)
                     - ((2 * t * t * t - 3 * t * t + 1) * 200 + (t * t * t - 2 * t * t + t) * 127 * inner + (-2 * t * t * t + 3 * t * t) * 255
                        + (t * t * t - t * t) * 127 * (55.0 / 127)))
            < 1e-9);
    QCOMPARE(lifted.value(128, 0), 200.0);
    double previous = -1;
    for (int x = 0; x <= 255; ++x) {
        QVERIFY(lifted.value(x, 0) >= previous);
        previous = lifted.value(x, 0);
    }
    // A peak flattens: nothing passes its neighbours.
    const CurvesSettings peak = withCurve(0, {{0, 0}, {128, 250}, {255, 0}});
    for (int x = 0; x <= 255; ++x)
        QVERIFY(peak.value(x, 0) <= 250);
    QCOMPARE(peak.value(128, 0), 250.0);
    // Off the curve: the first point below, the last above.
    QCOMPARE(lifted.value(-40, 0), 0.0);
    QCOMPARE(lifted.value(400, 0), 255.0);
    QCOMPARE(lifted.value(std::nan(""), 0), 0.0);
    QCOMPARE(withCurve(2, {{0, 30}, {255, 90}}).value(std::nan(""), 2), 30.0);
    // A straight curve stops at its ends, never runs on.
    const CurvesSettings line = withCurve(1, {{0, 100}, {255, 150}});
    QCOMPARE(line.value(400, 1), 150.0);
    QCOMPARE(line.value(-40, 1), 100.0);
}

void ImageAdjustmentModelTests::curvesRunEachChannelThenTheComposite()
{
    CurvesSettings settings = withCurve(1, {{0, 255}, {255, 0}});
    QVERIFY((first(settings.apply(filled(2, 2, QColor(200, 100, 50)))) == std::array<int, 4>{55, 100, 50, 255}));
    settings.channels[0] = {{0, 255}, {255, 0}};
    QVERIFY((first(settings.apply(filled(2, 2, QColor(200, 100, 50)))) == std::array<int, 4>{200, 155, 205, 255}));
    settings.channels[3] = {{0, 0}, {255, 0}};
    QVERIFY((first(settings.apply(filled(2, 2, QColor(200, 100, 50)))) == std::array<int, 4>{200, 155, 255, 255}));
    QVERIFY(refused([] { withCurve(0, {{0, 0}, {256, 255}}).apply(filled(1, 1, Qt::red)); }));
}

void ImageAdjustmentModelTests::exposureKeepsItsRangesAndTable()
{
    QVERIFY((ExposureSettings{20, 0.5, 9.99}.isValid() && ExposureSettings{-20, -0.5, 0.01}.isValid()));
    for (const ExposureSettings &wild : {ExposureSettings{20.01, 0, 1}, ExposureSettings{-20.01, 0, 1}, ExposureSettings{0, -0.51, 1},
                                         ExposureSettings{0, 0.51, 1}, ExposureSettings{0, 0, 0.009}, ExposureSettings{0, 0, 10},
                                         ExposureSettings{std::nan(""), 0, 1}}) {
        QVERIFY(!wild.isValid());
        QVERIFY(refused([&] { wild.apply(filled(1, 1, Qt::red)); }));
    }
    QCOMPARE((ExposureSettings{30, -2, 0}.normalized()), (ExposureSettings{20, -0.5, 0.01}));
    QCOMPARE((ExposureSettings{-30, 2, 20}.normalized()), (ExposureSettings{-20, 0.5, 9.99}));
    QCOMPARE((ExposureSettings{std::nan(""), INFINITY, std::nan("")}.normalized()), ExposureSettings());
    const std::array<float, 256> identity = ExposureSettings().table();
    for (size_t index = 0; index < 256; ++index)
        QVERIFY(std::abs(identity[index] - float(index) / 255) < 1e-5f);
    // A stop doubles linear light; the offset lifts black.
    QCOMPARE((ExposureSettings{1, 0, 1}.table()[128]), 0.68845345f);
    QCOMPARE((ExposureSettings{0, 0.1, 1}.table()[0]), 0.34919021f);
    QCOMPARE((ExposureSettings{0, -0.5, 1}.table()[255]), 0.7353569f);
    QCOMPARE((ExposureSettings{-20, 0, 1}.table()[255]), 1.2321472e-5f);
    QCOMPARE((ExposureSettings{20, 0, 1}.table()[1]), 1.0f);
}

void ImageAdjustmentModelTests::gradientMapsKeepTheirEndsAndColours()
{
    const GradientMapSettings settings{{0, 0, 0}, {1, 0.7, 0}, false};
    QCOMPARE(settings.ends().dark, AdjustmentColor(0, 0, 0));
    GradientMapSettings reversed = settings;
    reversed.reversed = true;
    QCOMPARE(reversed.ends().dark, AdjustmentColor(1, 0.7, 0));
    QCOMPARE(reversed.ends().light, AdjustmentColor(0, 0, 0));
    // Gray 128 lies halfway, each byte rounded.
    QVERIFY((first(settings.apply(filled(1, 1, QColor(128, 128, 128)))) == std::array<int, 4>{128, 90, 0, 255}));
    QVERIFY((first(settings.apply(filled(1, 1, Qt::transparent))) == std::array<int, 4>{0, 0, 0, 0}));
    const GradientMapSettings wild{{1.5, 0, 0}, {0, std::nan(""), -0.5}, true};
    QVERIFY(!wild.isValid() && !(GradientMapSettings{{0, 0, 0}, {0, 0, 1.01}, false}.isValid()));
    QVERIFY(!(GradientMapSettings{{1.5, 0, 0}, {1, 1, 1}, false}.isValid()));
    QVERIFY(refused([&] { wild.apply(filled(1, 1, Qt::red)); }));
    QCOMPARE(wild.normalized(), (GradientMapSettings{{1, 0, 0}, {0, 0, 0}, true}));
    QCOMPARE(AdjustmentColor(PaletteColor{0.2, 0.4, 0.6}), AdjustmentColor(0.2, 0.4, 0.6));
    QVERIFY(AdjustmentColor(0, 1, 0.5).isValid() && !AdjustmentColor(0, 1, INFINITY).isValid() && !AdjustmentColor(-0.1, 0, 0).isValid());
    for (const AdjustmentColor &wild : {AdjustmentColor(1.1, 0, 0), AdjustmentColor(0, -0.1, 0), AdjustmentColor(0, 1.1, 0), AdjustmentColor(0, 0, -0.1)})
        QVERIFY(!wild.isValid());
}

void ImageAdjustmentModelTests::grainKeepsItsRangesSeedAndPlace()
{
    QVERIFY((GrainSettings{100, 20, 100, 0}.isValid() && GrainSettings{0, 0.5, 0, 0}.isValid()));
    for (const GrainSettings &wild : {GrainSettings{101, 1.5, 50, 0}, GrainSettings{-1, 1.5, 50, 0}, GrainSettings{25, 0.4, 50, 0},
                                      GrainSettings{25, 21, 50, 0}, GrainSettings{25, 1.5, -1, 0}, GrainSettings{25, 1.5, 101, 0},
                                      GrainSettings{std::nan(""), 1.5, 50, 0}})
        QVERIFY(!wild.isValid());
    QCOMPARE((GrainSettings{std::nan(""), std::nan(""), std::nan(""), 4}.normalized()), (GrainSettings{25, 1.5, 50, 4}));
    QCOMPARE((GrainSettings{-5, 50, 200, 4}.normalized()), (GrainSettings{0, 20, 100, 4}));
    QCOMPARE((GrainSettings{150, 0.1, -5, 4}.normalized()), (GrainSettings{100, 0.5, 0, 4}));
    const GrainSettings settings{60, 2, 40, 7};
    const QImage gray = filled(8, 8, QColor(128, 128, 128));
    for (const double scale : {0.0, -1.0, double(std::nan("")), double(INFINITY)})
        QVERIFY(refused([&] { settings.apply(gray, QPointF(), scale); }));
    QVERIFY(refused([] { GrainSettings{101, 1.5, 50, 0}.apply(filled(1, 1, Qt::red)); }));
    // No amount hands the same image back.
    QCOMPARE((GrainSettings{0, 1.5, 50, 0}.apply(gray).cacheKey()), gray.cacheKey());
    // A given seed stands in for the stored one.
    GrainSettings reseeded = settings;
    reseeded.seed = 8;
    QCOMPARE(settings.apply(gray, QPointF(), 1, 8u), reseeded.apply(gray));
    QVERIFY(settings.apply(gray) != reseeded.apply(gray));
    // Pixels twice as big sit at other grain.
    QVERIFY(settings.apply(gray, QPointF(), 2) != settings.apply(gray));
    const QImage whole = settings.apply(filled(4, 4, QColor(128, 128, 128)));
    QCOMPARE(settings.apply(filled(2, 2, QColor(128, 128, 128)), QPointF(1, 2)), whole.copy(1, 2, 2, 2));
}

void ImageAdjustmentModelTests::theKernelSeesPremultipliedRgba()
{
    QImage source(3, 2, QImage::Format_ARGB32);
    source.fill(QColor(200, 100, 50, 128));
    const QImage result = ImageAdjustmentPixels::run(source, [](uchar *pixels, int width, int height, qsizetype stride) {
        QCOMPARE(width, 3);
        QCOMPARE(height, 2);
        QCOMPARE(stride, qsizetype(12));
        // Premultiplied, red first, alpha last.
        QVERIFY(std::abs(pixels[0] - 100) <= 1 && std::abs(pixels[1] - 50) <= 1 && std::abs(pixels[2] - 25) <= 1 && pixels[3] == 128);
        pixels[12 + 3] = 255;
    });
    QCOMPARE(result.format(), QImage::Format_RGBA8888_Premultiplied);
    QCOMPARE(qAlpha(result.pixel(0, 1)), 255);
    QCOMPARE(ImageAdjustmentPixels::clamp(7, 0, 5, 1), 5.0);
    QCOMPARE(ImageAdjustmentPixels::clamp(-7, 0, 5, 1), 0.0);
    QCOMPARE(ImageAdjustmentPixels::clamp(std::nan(""), 0, 5, 1), 1.0);
    QCOMPARE(ImageAdjustmentPixels::clamp(-INFINITY, 0, 5, 1), 1.0);
}

QTEST_GUILESS_MAIN(ImageAdjustmentModelTests)
#include "ImageAdjustmentModelTests.moc"
