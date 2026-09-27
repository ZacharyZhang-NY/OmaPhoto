#include "Document/BrushStroke.h"
#include "Document/EditorSession+Model.h"
#include "Document/Filters.h"
#include "Document/PixelAdjust.h"
#include "IO/ProjectStore.h"
#include "Rendering/RasterSnapshot.h"
#include <QPainter>
#include <QtTest>
#include <cmath>
#include <limits>
#include <numbers>

extern "C" {
#include "LensPixels.h"
#include "NoisePixels.h"
}

// The filters' models: kinds, settings, the edit's grids, the blur.
namespace {
QImage filled(int width, int height, QColor colour = Qt::red)
{
    QImage image = BrushRaster::context(width, height, false);
    image.fill(colour);
    return image;
}

// A painted asset without full pixels: a two-pixel base.
ImportedImage painted(int width, int height)
{
    return ImportedImage(std::make_shared<const RasterSnapshot>(width, height, filled(2, 2), QRectF(0, 0, 2, 2), std::vector<BrushPatch>{}), QImage(),
                         QStringLiteral("Painted"));
}

ImageLayer layer(int width, int height, QPointF origin = QPointF())
{
    const QImage image = filled(width, height);
    return ImageLayer(ImportedImage(image, image, QStringLiteral("Red")), origin);
}

int alpha(const QImage &image, int x, int y)
{
    return image.constScanLine(y)[x * 4 + 3];
}
}

class FilterModelTests : public QObject {
    Q_OBJECT
private slots:
    void kindsKeepSwiftsNames();
    void settingsNormalizeIntoTheirRanges();
    void blursTakeRoomRoundTheLayer();
    void previewsScaleTheLongSideTo2048UnlessGrainy();
    void aBlurGrowsTheGridAndNeverShrinksIt();
    void contentAwareFillGrowsOverTheArea();
    void theMotionBlurTapersAsAGaussianAlongItsAngle();
    void theMotionBlurReadsNothingPastItsImage();
    void theGaussianBlurFadesTheImagesOwnEdge();
    void theImageAdjustmentsRunTheirOwnSettings();
    void aSelectionLimitsTheFilter();
    void previewsFilterByTheirScale();
    void theKernelsKeepSwiftsConstants();
    void paintedLayersKeepTheirTiles();
};

void FilterModelTests::kindsKeepSwiftsNames()
{
    const std::pair<FilterKind, const char *> kinds[] = {
        {FilterKind::gaussianBlur, "Gaussian Blur"}, {FilterKind::motionBlur, "Motion Blur"}, {FilterKind::addNoise, "Add Noise"},
        {FilterKind::lensCorrection, "Lens Correction"}, {FilterKind::cameraRaw, "Camera Raw Filter"},
        {FilterKind::removeBackground, "Remove Background"},
        {FilterKind::contentAwareFill, "Content-Aware Fill"}, {FilterKind::curves, "Curves"}, {FilterKind::exposure, "Exposure"},
        {FilterKind::gradientMap, "Gradient Map"}, {FilterKind::grain, "Grain"}, {FilterKind::blackWhite, "Black & White"},
        {FilterKind::colorBalance, "Color Balance"}};
    // Swift's allCases is this order, the menus'.
    QCOMPARE(allFilterKinds.size(), std::size(kinds));
    for (size_t index = 0; index < std::size(kinds); ++index) {
        const auto &[kind, name] = kinds[index];
        QVERIFY(allFilterKinds[index] == kind);
        QCOMPARE(rawValue(kind), QString(name));
        QCOMPARE(isAutomatic(kind), kind == FilterKind::contentAwareFill || kind == FilterKind::removeBackground);
        QCOMPARE(isImageAdjustment(kind), index >= 7);
    }
    // Remove Background's qualities, Swift's words and order.
    QVERIFY(allBackgroundQualities.size() == 2 && allBackgroundQualities[0] == BackgroundQuality::basic);
    QVERIFY(rawValue(BackgroundQuality::basic) == "Basic" && rawValue(BackgroundQuality::advanced) == "Advanced");
    QVERIFY(FilterSettings().backgroundQuality == BackgroundQuality::basic);
}

void FilterModelTests::settingsNormalizeIntoTheirRanges()
{
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    FilterSettings wild{.radius = 1000, .angle = -200, .distance = 0, .amount = 1000, .distortion = -500};
    wild.grain.amount = 500;
    wild.exposure.gamma = 50;
    wild.gradientMap.shadows = AdjustmentColor(2, -1, 0.5);
    wild.curves.channels[1] = {{0, 0}, {128, 255}, {255, 255}};
    wild.refineEdges = 90;
    wild.matteContrast = -5;
    wild.shiftEdge = 20;
    const FilterSettings tamed = wild.normalized();
    QVERIFY(tamed.refineEdges == 40 && tamed.matteContrast == 0 && tamed.shiftEdge == 10);
    QVERIFY(tamed.radius == 250 && tamed.angle == -90 && tamed.distance == 1 && tamed.amount == 400 && tamed.distortion == -100);
    QVERIFY(tamed.grain.amount == 100 && tamed.exposure.gamma == 9.99);
    QCOMPARE(tamed.gradientMap.shadows, AdjustmentColor(1, 0, 0.5));
    // Curves pass as they are, as Swift's.
    QVERIFY(tamed.curves == wild.curves);
    FilterSettings wide{.radius = 0, .angle = 200, .distance = 5000, .amount = 0, .distortion = 500};
    wide.refineEdges = -1;
    wide.matteContrast = 300;
    wide.shiftEdge = -20;
    const FilterSettings low = wide.normalized();
    QVERIFY(low.radius == 0.1 && low.angle == 90 && low.distance == 2000 && low.amount == 0.1 && low.distortion == 100);
    QVERIFY(low.refineEdges == 0 && low.matteContrast == 100 && low.shiftEdge == -10);
    // What is no number falls back to the default.
    FilterSettings unknown{.radius = nan, .angle = nan, .distance = nan, .amount = nan, .distortion = nan};
    unknown.refineEdges = nan;
    unknown.matteContrast = nan;
    unknown.shiftEdge = nan;
    QVERIFY(unknown.normalized() == FilterSettings());
    QVERIFY(FilterSettings().refineEdges == 12 && FilterSettings().matteContrast == 25 && FilterSettings().shiftEdge == 0);
}

void FilterModelTests::blursTakeRoomRoundTheLayer()
{
    QCOMPARE(FilterEdit::blurMargin(FilterKind::gaussianBlur, FilterSettings{.radius = 3}), 11.0);
    QCOMPARE(FilterEdit::blurMargin(FilterKind::motionBlur, FilterSettings{.distance = 10}), 7.0);
    for (const FilterKind kind : {FilterKind::addNoise, FilterKind::lensCorrection, FilterKind::removeBackground, FilterKind::contentAwareFill,
                                  FilterKind::curves, FilterKind::exposure, FilterKind::gradientMap, FilterKind::grain})
        QCOMPARE(FilterEdit::blurMargin(kind, FilterSettings{.radius = 3, .distance = 10}), 0.0);
}

void FilterModelTests::previewsScaleTheLongSideTo2048UnlessGrainy()
{
    // Swift's Int() truncates: 2215 tall previews 2047.
    for (const auto &[size, preview] : {std::pair(QSize(4096, 3), QSize(2048, 1)), std::pair(QSize(3, 2215), QSize(2, 2047))}) {
        const FilterEdit edit(FilterKind::exposure, layer(size.width(), size.height()), std::nullopt, FilterSettings(), std::nullopt);
        QCOMPARE(edit.previewSource.size(), preview);
        QCOMPARE(edit.previewScale, double(preview.width()) / size.width());
        // The preview's far corner is the layer's.
        QCOMPARE(edit.previewMapping.map(QPointF(preview.width(), preview.height())), QPointF(size.width(), size.height()));
        QCOMPARE(edit.mapping.map(QPointF(size.width(), size.height())), QPointF(size.width(), size.height()));
        const FilterJob job = edit.previewJob();
        QVERIFY(job.kind == FilterKind::exposure && job.scale == edit.previewScale && job.seed == edit.seed && job.image.size() == preview);
        QVERIFY(job.settings == edit.settings && job.mapping == edit.previewMapping);
    }
    // Noise and grain preview whole: enlarged grain looks coarse.
    for (const FilterKind kind : {FilterKind::addNoise, FilterKind::grain, FilterKind::contentAwareFill, FilterKind::removeBackground}) {
        const FilterEdit edit(kind, layer(4096, 3), std::nullopt, FilterSettings(), std::nullopt);
        QVERIFY(edit.previewSource.size() == QSize(4096, 3) && edit.previewScale == 1 && edit.previewMapping == edit.mapping);
    }
    // A small layer's own pixels are the preview.
    const ImageLayer small = layer(2048, 3);
    const FilterEdit whole(FilterKind::exposure, small, std::nullopt, FilterSettings(), std::nullopt);
    QCOMPARE(whole.previewSource.cacheKey(), small.asset.value().image().cacheKey());
    // Two edits roll their own grain.
    const FilterEdit first(FilterKind::addNoise, layer(2, 2), std::nullopt, FilterSettings(), std::nullopt);
    const FilterEdit second(FilterKind::addNoise, layer(2, 2), std::nullopt, FilterSettings(), std::nullopt);
    QVERIFY(first.seed != second.seed);
    // Settings arrive normalized.
    QCOMPARE(FilterEdit(FilterKind::gaussianBlur, layer(2, 2), std::nullopt, FilterSettings{.radius = 1000}, std::nullopt).settings.radius, 250.0);
}

void FilterModelTests::aBlurGrowsTheGridAndNeverShrinksIt()
{
    FilterEdit edit(FilterKind::gaussianBlur, layer(40, 20, QPointF(10, 10)), std::nullopt, FilterSettings{.radius = 3}, std::nullopt);
    QCOMPARE(edit.grownMargin, 11.0);
    QCOMPARE(edit.grownImage.value().size(), QSize(62, 42));
    QCOMPARE(edit.grownTransform.value().origin, QPointF(-1, -1));
    QCOMPARE(edit.grownTransform.value().size, QSizeF(62, 42));
    // The layer's pixels sit inside the padding.
    QVERIFY(alpha(edit.grownImage.value(), 10, 10) == 0 && alpha(edit.grownImage.value(), 11, 11) == 255 && alpha(edit.grownImage.value(), 51, 31) == 0);
    QVERIFY(edit.previewSource.size() == QSize(62, 42) && edit.mapping.map(QPointF(0, 0)) == QPointF(-1, -1));
    // A smaller blur keeps the grid; a bigger one grows.
    edit.settings = FilterSettings{.radius = 1};
    edit.growForBlur();
    QCOMPARE(edit.grownImage.value().size(), QSize(62, 42));
    edit.settings = FilterSettings{.radius = 10};
    edit.growForBlur();
    QCOMPARE(edit.grownMargin, 32.0);
    QCOMPARE(edit.grownImage.value().size(), QSize(104, 84));
    QCOMPARE(edit.grownTransform.value().origin, QPointF(-22, -22));
    // A motion blur's reach is half its streak.
    const FilterEdit motion(FilterKind::motionBlur, layer(4, 4), std::nullopt, FilterSettings{.distance = 9}, std::nullopt);
    QCOMPARE(motion.grownImage.value().size(), QSize(18, 18));
    // Past 30,000 a side or 100 million pixels: refused.
    QVERIFY_THROWS_EXCEPTION(ProjectError, FilterEdit(FilterKind::gaussianBlur, layer(29990, 1), std::nullopt, FilterSettings{.radius = 3}, std::nullopt));
    QVERIFY_THROWS_EXCEPTION(ProjectError, FilterEdit(FilterKind::gaussianBlur, layer(1, 29990), std::nullopt, FilterSettings{.radius = 3}, std::nullopt));
    QVERIFY_THROWS_EXCEPTION(ProjectError, FilterEdit(FilterKind::gaussianBlur, ImageLayer(painted(9990, 9990), QPointF()), std::nullopt,
                                                      FilterSettings{.radius = 3}, std::nullopt));
}

void FilterModelTests::contentAwareFillGrowsOverTheArea()
{
    // The area passes the layer's right and bottom only.
    const FilterEdit edit(FilterKind::contentAwareFill, layer(10, 10, QPointF(5, 5)), std::nullopt, FilterSettings(), QRectF(8, 8, 12.5, 9));
    QCOMPARE(edit.grownImage.value().size(), QSize(16, 12));
    QCOMPARE(edit.grownTransform.value().origin, QPointF(5, 5));
    QCOMPARE(edit.grownMargin, 0.0);
    QVERIFY(edit.previewSource.size() == QSize(16, 12) && edit.previewScale == 1);
    // Inside the layer nothing grows.
    const FilterEdit inside(FilterKind::contentAwareFill, layer(10, 10), std::nullopt, FilterSettings(), QRectF(2, 2, 3, 3));
    QVERIFY(!inside.grownImage && !inside.grownTransform && inside.grownMargin == 0);
}

void FilterModelTests::theMotionBlurTapersAsAGaussianAlongItsAngle()
{
    QImage dot = BrushRaster::context(21, 21, false);
    QPainter(&dot).fillRect(10, 10, 1, 1, Qt::white);
    // Level, the streak's taps are the Gaussian's weights.
    const QImage level = PixelAdjust::motionBlur(dot, 2, 0);
    double total = 0;
    for (int i = 0; i <= 6; ++i)
        total += (i == 0 ? 1 : 2) * std::exp(-i * i / 8.0);
    for (int i = 0; i <= 6; ++i) {
        const int expected = int(std::lround(float(std::exp(-i * i / 8.0) / total) * 255));
        QVERIFY2(alpha(level, 10 + i, 10) == expected && alpha(level, 10 - i, 10) == expected, qPrintable(QString::number(i)));
        QCOMPARE(alpha(level, 10, 10 + std::max(i, 1)), 0);
    }
    // A quarter turn streaks down the column.
    const QImage upright = PixelAdjust::motionBlur(dot, 2, std::numbers::pi / 2);
    QVERIFY(alpha(upright, 10, 13) == alpha(level, 13, 10) && alpha(upright, 13, 10) == 0);
    // Unclamped: what streaks past the edge is lost.
    QImage edge = BrushRaster::context(3, 1, false);
    edge.fill(Qt::white);
    const float kept = float(1 / total) + float(std::exp(-1 / 8.0) / total) + float(std::exp(-4 / 8.0) / total);
    QCOMPARE(alpha(PixelAdjust::motionBlur(edge, 2, 0), 0, 0), int(std::lround(kept * 255)));
    // Slanted taps share bilinearly: 0.48 here, then 0.32.
    const QImage slanted = PixelAdjust::motionBlur(dot, 1, std::atan2(0.6, 0.8));
    double sum = 0;
    for (int i = 0; i <= 3; ++i)
        sum += (i == 0 ? 1 : 2) * std::exp(-i * i / 2.0);
    QCOMPARE(alpha(slanted, 9, 11), int(std::lround((std::exp(-0.5) * 0.48 + std::exp(-2.0) * 0.32) / sum * 255)));
    const auto refusal = [&dot](double radius, double angle) {
        try {
            PixelAdjust::motionBlur(dot, radius, angle);
        } catch (const std::logic_error &error) {
            return QString::fromUtf8(error.what());
        }
        return QString();
    };
    const QString words = QStringLiteral("a motion blur needs a finite positive radius and angle");
    QCOMPARE(refusal(0, 0), words);
    QCOMPARE(refusal(std::numeric_limits<double>::infinity(), 0), words);
    QCOMPARE(refusal(2, std::numeric_limits<double>::quiet_NaN()), words);
    QVERIFY_THROWS_EXCEPTION(std::logic_error, PixelAdjust::motionBlur(dot.convertToFormat(QImage::Format_ARGB32), 2, 0));
}

void FilterModelTests::theMotionBlurReadsNothingPastItsImage()
{
    // A clear 5 by 5 in an opaque one-pixel frame.
    std::vector<uchar> framed(7 * 7 * 4, 255);
    for (int y = 1; y < 6; ++y)
        std::fill_n(framed.begin() + (y * 7 + 1) * 4, 5 * 4, uchar(0));
    const QImage inside(framed.data() + (7 + 1) * 4, 5, 5, 7 * 4, QImage::Format_RGBA8888_Premultiplied);
    for (const double angle : {0.0, std::numbers::pi / 2, std::numbers::pi / 4, -std::numbers::pi / 4})
        QCOMPARE(PixelAdjust::motionBlur(inside, 2, angle), BrushRaster::context(5, 5, false));
}

void FilterModelTests::theGaussianBlurFadesTheImagesOwnEdge()
{
    // Unclamped, as Core Image blurs a finite image: corners fade.
    const QImage blurred = PixelFilter::run(FilterJob{FilterKind::gaussianBlur, filled(8, 8, Qt::white), FilterSettings{.radius = 1}, 1, std::nullopt, QTransform()});
    double side = 0, sum = 0;
    for (int i = 0; i <= 3; ++i) {
        side += std::exp(-i * i / 2.0);
        sum += (i == 0 ? 1 : 2) * std::exp(-i * i / 2.0);
    }
    QCOMPARE(alpha(blurred, 0, 0), int(std::lround(side / sum * side / sum * 255)));
    QCOMPARE(alpha(blurred, 4, 4), 255);
}

void FilterModelTests::theImageAdjustmentsRunTheirOwnSettings()
{
    const QImage colour = filled(4, 4, QColor(100, 150, 200));
    FilterSettings settings;
    settings.curves.channels[0] = {{0, 40}, {255, 255}};
    settings.exposure.exposure = 1;
    settings.gradientMap = GradientMapSettings{{1, 0, 0}, {0, 0, 1}, false};
    settings.grain.amount = 50;
    const auto run = [&](FilterKind kind) { return PixelFilter::run(FilterJob{kind, colour, settings, 1, std::nullopt, QTransform(), 9}); };
    const QImage made[] = {settings.curves.apply(colour), settings.exposure.apply(colour), settings.gradientMap.apply(colour),
                           settings.grain.apply(colour, QPointF(), 1, 9)};
    const FilterKind kinds[] = {FilterKind::curves, FilterKind::exposure, FilterKind::gradientMap, FilterKind::grain};
    for (int index = 0; index < 4; ++index) {
        QVERIFY(made[index] != colour);
        QCOMPARE(run(kinds[index]), made[index]);
    }
}

void FilterModelTests::aSelectionLimitsTheFilter()
{
    const QImage gray = filled(4, 1, QColor(100, 100, 100));
    QPainterPath left;
    left.addRect(0, 0, 2, 1);
    const SelectionClip clip = DocumentSelection{left, false}.clip(QSizeF(4, 1));
    FilterSettings settings;
    settings.exposure.exposure = 1;
    const QImage made = PixelFilter::run(FilterJob{FilterKind::exposure, gray, settings, 1, clip, QTransform()});
    QVERIFY(made.pixelColor(1, 0).red() > 100);
    QCOMPARE(made.pixelColor(2, 0), QColor(100, 100, 100));
}

void FilterModelTests::previewsFilterByTheirScale()
{
    // Half white across: a half-size preview blurs half as far.
    QImage stripe = BrushRaster::context(40, 3, false);
    QPainter(&stripe).fillRect(20, 0, 20, 3, Qt::white);
    const auto run = [&stripe](FilterKind kind, const FilterSettings &settings, double scale) {
        return PixelFilter::run(FilterJob{kind, stripe, settings, scale, std::nullopt, QTransform(), 3});
    };
    QCOMPARE(run(FilterKind::gaussianBlur, FilterSettings{.radius = 4}, 0.5), run(FilterKind::gaussianBlur, FilterSettings{.radius = 2}, 1));
    QCOMPARE(run(FilterKind::motionBlur, FilterSettings{.distance = 16}, 0.5), run(FilterKind::motionBlur, FilterSettings{.distance = 8}, 1));
    // Grain sits in layer pixels: half scale, twice the units.
    FilterSettings grain;
    grain.grain.amount = 50;
    QCOMPARE(run(FilterKind::grain, grain, 0.5), grain.grain.apply(stripe, QPointF(), 2, 3));
}

void FilterModelTests::theKernelsKeepSwiftsConstants()
{
    // A 16-pixel streak spreads as an even one: sigma 4.62.
    QImage dot = BrushRaster::context(41, 41, false);
    QPainter(&dot).fillRect(20, 20, 1, 1, Qt::white);
    const QImage streak = PixelFilter::run(FilterJob{FilterKind::motionBlur, dot, FilterSettings{.distance = 16}, 1, std::nullopt, QTransform()});
    QCOMPARE(alpha(streak, 20, 20), 22);
    // Remove Distortion at 100 is the kernel's k of 0.35.
    QImage ramp = BrushRaster::context(40, 30, false);
    for (int x = 0; x < 40; ++x)
        QPainter(&ramp).fillRect(x, 0, 1, 30, QColor(x * 6, 0, 0));
    const QImage bent = PixelFilter::run(FilterJob{FilterKind::lensCorrection, ramp, FilterSettings{.distortion = 100}, 1, std::nullopt, QTransform()});
    QImage lens = BrushRaster::context(40, 30, false);
    lens_distort(ramp.constBits(), lens.bits(), 40, 30, size_t(ramp.bytesPerLine()), 0.35);
    QCOMPARE(bent, lens);
    // The job's settings are normalized first: 500 is 100.
    QCOMPARE(PixelFilter::run(FilterJob{FilterKind::lensCorrection, ramp, FilterSettings{.distortion = 500}, 1, std::nullopt, QTransform()}), bent);
    // Add Noise hands the kernel amount, kind, channels, seed.
    const QImage gray = filled(8, 8, QColor(128, 128, 128));
    for (const FilterSettings &settings : {FilterSettings{.amount = 30}, FilterSettings{.amount = 30, .gaussian = true},
                                           FilterSettings{.amount = 30, .monochromatic = true}}) {
        QImage noise = gray.copy();
        noise_add(noise.bits(), 8, 8, size_t(noise.bytesPerLine()), 30, settings.gaussian ? 1 : 0, settings.monochromatic ? 1 : 0, 5);
        QVERIFY(noise != gray);
        QCOMPARE(PixelFilter::run(FilterJob{FilterKind::addNoise, gray, settings, 1, std::nullopt, QTransform(), 5}), noise);
    }
}

void FilterModelTests::paintedLayersKeepTheirTiles()
{
    // A painted layer's preview and grid come from its tiles.
    const ImportedImage asset = painted(3000, 10);
    const FilterEdit adjusted(FilterKind::exposure, ImageLayer(asset, QPointF()), std::nullopt, FilterSettings(), std::nullopt);
    QVERIFY(adjusted.previewSource.size() == QSize(2048, 6) && !asset.raster->hasMaterializedPixels());
    const FilterEdit blurred(FilterKind::gaussianBlur, ImageLayer(asset, QPointF()), std::nullopt, FilterSettings(), std::nullopt);
    QVERIFY(blurred.grownImage.value().size() == QSize(3010, 20) && !asset.raster->hasMaterializedPixels());
}

QTEST_GUILESS_MAIN(FilterModelTests)
#include "FilterModelTests.moc"
