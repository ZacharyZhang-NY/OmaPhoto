#include "AddressSpaceLimit.h"
#include "BrushFixtures.h"
#include "Document/LayerMask.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore.h"
#include "Rendering/RasterSnapshot.h"
#include <malloc.h>

// The stroke engine alone; the session's brush arrives with 8.2.
class BrushTests : public QObject {
    Q_OBJECT
private slots:
    // Large asks map memory, so the address limit counts them.
    void initTestCase() { mallopt(M_MMAP_THRESHOLD, 64 * 1024); }
    void continuousStrokeCrossesTiles();
    void softBrushProducesPartialAlphaAndPaintsNothingOffTheCanvas();
    void brushStaysCircularOnNonuniformRotatedFlippedLayer();
    void maskedImagePaintingUsesCoverageAndOpacityOnlyOnce();
    void paintedBoundsTrimTilePaddingAndKeepSoftEdges();
    void opacityCapsTheWholeStrokeEvenWhereItOverlapsItself();
    void softStrokeBuildsCoverageWhileKeepingItsFeatheredRim();
    void spacedDabsLeaveNoVisibleRippleAlongTheStroke();
    void sparseMouseSamplesFollowACurveInsteadOfStraightChords();
    void liveStrokeReachesNewestSampleAndTailIsReplacedExactly();
    void largeBlankCanvasOnlyAllocatesTouchedTiles();
    void aSelectionClipLimitsTheStroke();
    void aSoftClipScalesEachCoverageByteOnce();
    void masksPaintInGrayAndCommitWhole();
    void wildSettingsAndPointsAreRefused();
    void hardTipsAntialiasTheirRimAndStopAtTheCanvas();
    void paintingPastTheLayerGrowsItsGrid();
    void aPaintedLayerPaintsFromItsTilesWithoutFlattening();
    void anOrdinarySourceKeepsItsPixelsBesideTheDab();
    void antialiasFollowsTheLayersScale();
    void tileKeysFitBigGridsAndACommitThatCannotCropThrows();
    void aSelectionClipsOnATransformedGrid();
    void aPlacedMaskPaintsInItsOwnGrid();
};

void BrushTests::continuousStrokeCrossesTiles()
{
    const auto paint = stroke(600, 80, red());
    paint->append(QPointF(20, 40));
    paint->append(QPointF(580, 40));
    QCOMPARE(paint->patches().size(), size_t(3));
    QCOMPARE(paint->dirtyDocumentRect(), std::optional(QRectF(0, 0, 600, 80)));
    const QImage live = preview(*paint, QSizeF(600, 80));
    for (int x : {20, 255, 256, 511, 512, 579})
        QCOMPARE(pixel(live, x, 40), (std::vector<int>{255, 0, 0, 255}));
    QCOMPARE(alpha(live, 300, 0), 0);
    // The commit keeps every pixel, the tiles' padding cut away.
    const BrushCommit::Output made = BrushCommit::render(paint->commitInput());
    QCOMPARE(made.pixelBounds, QRectF(10, 30, 580, 20));
    const QImage result = render(made.asset, paint->transform(made.pixelBounds.translated(paint->committedBounds().topLeft())), QSizeF(600, 80));
    for (int x : {20, 255, 256, 511, 512, 579})
        QCOMPARE(pixel(result, x, 40), (std::vector<int>{255, 0, 0, 255}));
}

void BrushTests::softBrushProducesPartialAlphaAndPaintsNothingOffTheCanvas()
{
    const auto paint = stroke(80, 80, red(40, 0));
    paint->append(QPointF(40, 40));
    const QImage live = preview(*paint, QSizeF(80, 80));
    QVERIFY(alpha(live, 40, 40) > 230);
    // Half at half radius, faint near the rim, none beyond.
    const int half = alpha(live, 50, 40);
    QVERIFY2(half > 95 && half < 140, qPrintable(QString::number(half)));
    QVERIFY(alpha(live, 57, 40) < 40);
    QCOMPARE(alpha(live, 64, 40), 0);
    const auto outside = stroke(80, 80, red(40, 0));
    outside->append(QPointF(-100, -100));
    QVERIFY(outside->patches().empty() && !outside->dirtyDocumentRect().has_value());
}

void BrushTests::brushStaysCircularOnNonuniformRotatedFlippedLayer()
{
    QImage source = BrushRaster::context(100, 100, false);
    QPainter painter(&source);
    painter.fillRect(QRect(2, 2, 6, 6), Qt::red);
    painter.end();
    ImageLayer layer(ImportedImage(source, source, "Fixture"), QPointF(0, 0));
    layer.transform = LayerTransform{.origin = {25, -50}, .size = {50, 200}, .rotation = 90, .flipX = true, .flipY = true, .sampling = LayerSampling::nearest};
    BrushStroke paint(layer, false, brush(16, 1, 0, 1, 0), QSizeF(100, 100));
    paint.append(QPointF(50, 50));
    const QImage live = preview(paint, QSizeF(100, 100));
    for (const auto &[x, y] : std::vector<std::pair<int, int>>{{50, 50}, {54, 50}, {50, 54}})
        QVERIFY2(pixel(live, x, y)[1] > 240, qPrintable(QStringLiteral("%1,%2").arg(x).arg(y)));
    const BrushCommit::Output made = BrushCommit::render(paint.commitInput());
    const QImage result = render(made.asset, paint.transform(made.pixelBounds.translated(paint.committedBounds().topLeft())), QSizeF(100, 100));
    for (const auto &[x, y] : std::vector<std::pair<int, int>>{{50, 50}, {54, 50}, {50, 54}})
        QVERIFY2(pixel(result, x, y)[1] > 240, qPrintable(QStringLiteral("%1,%2").arg(x).arg(y)));
    for (const auto &[x, y] : std::vector<std::pair<int, int>>{{61, 50}, {50, 61}})
        QCOMPARE(alpha(result, x, y), 0);
}

void BrushTests::maskedImagePaintingUsesCoverageAndOpacityOnlyOnce()
{
    ImageLayer layer = blankLayer(520, 40);
    layer.mask = LayerMask(gray(128));
    layer.opacity = 0.5;
    BrushStroke paint(layer, false, red(), QSizeF(520, 40));
    paint.append(QPointF(240, 20));
    paint.append(QPointF(280, 20));
    const QImage live = preview(paint, QSizeF(520, 40));
    for (int x : {250, 255, 256, 260})
        QVERIFY2(std::abs(alpha(live, x, 20) - 64) <= 1, qPrintable(QString::number(alpha(live, x, 20))));
}

void BrushTests::paintedBoundsTrimTilePaddingAndKeepSoftEdges()
{
    for (double hardness : {0.0, 1.0}) {
        const auto paint = stroke(600, 200, red(20, hardness));
        paint->append(QPointF(300, 100));
        const QImage live = preview(*paint, QSizeF(600, 200));
        const PaintSnapshot snapshot = paint->paintSnapshot();
        // The snapshot is tiles; reading its pixels flattens it.
        QVERIFY(!snapshot.asset.raster->hasMaterializedPixels());
        const QImage image = snapshot.asset.image();
        QVERIFY(snapshot.asset.raster->hasMaterializedPixels());
        QVERIFY(image.width() <= 20 && image.height() <= 20);
        QVERIFY(snapshot.transform.origin.x() >= 290 && snapshot.transform.origin.y() >= 90);
        QCOMPARE(snapshot.bounds.size(), QSizeF(image.size()));
        // Every edge of the crop holds a pixel.
        const auto edgeHolds = [&](int x0, int y0, int dx, int dy, int count) {
            for (int i = 0; i < count; ++i) {
                if (alpha(image, x0 + dx * i, y0 + dy * i) > 0)
                    return true;
            }
            return false;
        };
        QVERIFY(edgeHolds(0, 0, 1, 0, image.width()) && edgeHolds(0, image.height() - 1, 1, 0, image.width()));
        QVERIFY(edgeHolds(0, 0, 0, 1, image.height()) && edgeHolds(image.width() - 1, 0, 0, 1, image.height()));
        const QImage result = render(snapshot.asset, snapshot.transform, QSizeF(600, 200));
        for (int x = 288; x <= 312; ++x)
            QCOMPARE(pixel(result, x, 100), pixel(live, x, 100));
    }
}

void BrushTests::opacityCapsTheWholeStrokeEvenWhereItOverlapsItself()
{
    const auto paint = stroke(200, 80, brush(20, 1, 1, 0, 0, 0.5));
    paint->append(QPointF(20, 40));
    for (int x : {180, 20, 180, 20, 100})
        paint->append(QPointF(x, 40));
    const QImage live = preview(*paint, QSizeF(200, 80));
    const std::vector<int> shown = pixel(live, 100, 40);
    QVERIFY2(std::abs(shown[3] - 128) <= 1 && std::abs(shown[0] - 128) <= 1 && shown[1] == 0, qPrintable(QStringLiteral("%1 %2").arg(shown[0]).arg(shown[3])));
    paint->flush();
    const BrushCommit::Output made = BrushCommit::render(paint->commitInput());
    const QImage result = render(made.asset, paint->transform(made.pixelBounds.translated(paint->committedBounds().topLeft())), QSizeF(200, 80));
    QCOMPARE(pixel(result, 100, 40), shown);
    QCOMPARE(alpha(result, 100, 0), 0);
}

void BrushTests::softStrokeBuildsCoverageWhileKeepingItsFeatheredRim()
{
    const auto dab = stroke(200, 80, red(40, 0));
    dab->append(QPointF(100, 40));
    const int single = alpha(preview(*dab, QSizeF(200, 80)), 100, 50);
    const auto run = stroke(200, 80, red(40, 0));
    run->append(QPointF(20, 40));
    run->append(QPointF(180, 40));
    const int along = alpha(preview(*run, QSizeF(200, 80)), 100, 50);
    QVERIFY2(along > single + 60, qPrintable(QStringLiteral("%1 vs %2").arg(along).arg(single)));
    QVERIFY(along <= 255);
}

void BrushTests::spacedDabsLeaveNoVisibleRippleAlongTheStroke()
{
    for (double hardness : {0.0, 0.5, 1.0}) {
        const auto paint = stroke(900, 300, red(120, hardness));
        paint->append(QPointF(100, 150));
        paint->append(QPointF(800, 150));
        const QImage image = preview(*paint, QSizeF(900, 300));
        // Along the middle of the stroke, and nearer its edge.
        for (int offset : {0, 30, 50}) {
            int low = 255, high = 0;
            for (int x = 300; x <= 600; ++x) {
                low = std::min(low, alpha(image, x, 150 + offset));
                high = std::max(high, alpha(image, x, 150 + offset));
            }
            QVERIFY2(high - low <= 16, qPrintable(QStringLiteral("hardness %1 offset %2 rippled by %3").arg(hardness).arg(offset).arg(high - low)));
        }
    }
}

void BrushTests::sparseMouseSamplesFollowACurveInsteadOfStraightChords()
{
    const auto paint = stroke(300, 300, red(4));
    const QPointF center(150, 150);
    const auto onCircle = [&](double degrees) {
        return QPointF(center.x() + std::cos(degrees * M_PI / 180) * 100, center.y() + std::sin(degrees * M_PI / 180) * 100);
    };
    paint->append(onCircle(0));
    for (int degrees = 30; degrees <= 180; degrees += 30)
        paint->append(onCircle(degrees));
    paint->flush();
    const QImage image = preview(*paint, QSizeF(300, 300));
    // A 30°–60° chord misses the arc by 3.4 px.
    for (double degrees : {15.0, 45.0, 75.0, 105.0, 135.0}) {
        const QPointF point = onCircle(degrees);
        QVERIFY2(alpha(image, int(point.x()), int(point.y())) > 0, qPrintable(QStringLiteral("arc at %1").arg(degrees)));
    }
}

void BrushTests::liveStrokeReachesNewestSampleAndTailIsReplacedExactly()
{
    const auto paint = stroke(300, 120, red(8));
    paint->append(QPointF(20, 60));
    paint->append(QPointF(150, 20));
    paint->append(QPointF(280, 60));
    // No lag: the provisional tail already reaches the cursor.
    QCOMPARE(alpha(preview(*paint, QSizeF(300, 120)), 278, 60), 255);
    paint->flush();
    const QImage settled = preview(*paint, QSizeF(300, 120));
    QCOMPARE(alpha(settled, 215, 40), 0);
    QCOMPARE(alpha(settled, 278, 60), 255);
    // Flushing again changes nothing.
    paint->flush();
    QCOMPARE(preview(*paint, QSizeF(300, 120)), settled);
}

void BrushTests::largeBlankCanvasOnlyAllocatesTouchedTiles()
{
    const auto paint = stroke(10000, 10000, red());
    paint->append(QPointF(100, 100));
    const std::vector<BrushPatch> patches = paint->patches();
    QCOMPARE(patches.size(), size_t(1));
    QVERIFY(patches[0].image.sizeInBytes() <= 256 * 256 * 4);
    // The pixel budget refuses a stroke needing too many tiles.
    const auto starved = stroke(10000, 10000, red());
    starved->pixelLimit = 100'000;
    starved->append(QPointF(100, 100));
    QVERIFY_THROWS_EXCEPTION(ProjectError, starved->append(QPointF(9000, 9000)));
}

void BrushTests::aSelectionClipLimitsTheStroke()
{
    const auto paint = stroke(600, 80, red());
    DocumentSelection selection;
    selection.path.addRect(QRectF(0, 0, 300, 80));
    paint->selectionClip = selection.clip(QSizeF(600, 80));
    paint->append(QPointF(20, 40));
    paint->append(QPointF(580, 40));
    const QImage live = preview(*paint, QSizeF(600, 80));
    QCOMPARE(alpha(live, 100, 40), 255);
    QCOMPARE(alpha(live, 400, 40), 0);
    QCOMPARE(alpha(live, 299, 40), 255);
    QCOMPARE(alpha(live, 300, 40), 0);
}

void BrushTests::aSoftClipScalesEachCoverageByteOnce()
{
    // A soft dab through a soft outline: coverage times clip.
    DocumentSelection selection;
    selection.path.addEllipse(QRectF(10.3, 12.7, 50.4, 41.1));
    const SelectionClip clip = selection.clip(QSizeF(80, 80));
    const QImage within = PixelAdjust::coverage(clip, 80, 80, QTransform());
    const auto open = stroke(80, 80, red(60, 0));
    const auto clipped = stroke(80, 80, red(60, 0));
    clipped->selectionClip = clip;
    open->append(QPointF(40, 40));
    clipped->append(QPointF(40, 40));
    const BrushPatch whole = open->patches().at(0), cut = clipped->patches().at(0);
    QCOMPARE(cut.rect, QRectF(0, 0, 80, 80));
    int partial = 0;
    for (int y = 0; y < 80; ++y) {
        for (int x = 0; x < 80; ++x) {
            const int coverage = whole.image.constScanLine(y)[x * 4 + 3], edge = within.constScanLine(y)[x];
            partial += coverage > 0 && coverage < 255 && edge > 0 && edge < 255;
            QCOMPARE(int(cut.image.constScanLine(y)[x * 4 + 3]), (coverage * edge + 127) / 255);
        }
    }
    QVERIFY(partial > 50);
}

void BrushTests::masksPaintInGrayAndCommitWhole()
{
    QImage black = BrushRaster::context(600, 80, true);
    ImageLayer layer = blankLayer(600, 80);
    layer.mask = LayerMask(LayerMask::assetFrom(black));
    BrushStroke paint(layer, true, brush(20, 1, 1, 1, 1), QSizeF(600, 80));
    QCOMPARE(QSize(paint.width, paint.height), QSize(600, 80));
    paint.append(QPointF(300, 40));
    QCOMPARE(paint.patches()[0].image.format(), QImage::Format_Grayscale8);
    const BrushCommit::Output made = BrushCommit::render(paint.commitInput());
    QCOMPARE(made.pixelBounds, QRectF(0, 0, 600, 80));
    const QImage mask = made.asset.image();
    QCOMPARE(mask.format(), QImage::Format_Grayscale8);
    QCOMPARE(int(mask.constScanLine(40)[300]), 255);
    QCOMPARE(int(mask.constScanLine(40)[100]), 0);
    // A mask paints even when the settings say erase.
    BrushSettings erasing = brush(20, 1, 1, 1, 1);
    erasing.erasing = true;
    BrushStroke painted(layer, true, erasing, QSizeF(600, 80));
    painted.append(QPointF(300, 40));
    QCOMPARE(int(BrushCommit::render(painted.commitInput()).asset.image().constScanLine(40)[300]), 255);
    // A mask grown past its old grid reveals: white beyond.
    const ImportedImage expanded = BrushCommit::expandMask(made.asset, paint.commitInput(), QRectF(-10, 0, 610, 80));
    QCOMPARE(expanded.size(), QSize(610, 80));
    QCOMPARE(int(expanded.image().constScanLine(40)[5]), 255);
    QCOMPARE(int(expanded.image().constScanLine(40)[110]), 0);
}

void BrushTests::wildSettingsAndPointsAreRefused()
{
    for (const BrushSettings &settings : {red(0), red(2001), red(NAN), red(20, 1.1), red(20, -0.1), brush(20, 1, 1, 0, 0, 0.001), brush(20, 1, 1, 0, 0, INFINITY)})
        QVERIFY_THROWS_EXCEPTION(ProjectError, BrushStroke(blankLayer(600, 80), false, settings, QSizeF(600, 80)));
    // A billion-pixel grid is refused while still a double.
    QImage strip = BrushRaster::context(30000, 1, false);
    ImageLayer far(ImportedImage(strip, strip, "Strip"), QPointF(100000, 0));
    far.transform.size = QSizeF(1, 1);
    QVERIFY_THROWS_EXCEPTION(ProjectError, BrushStroke(far, false, red(), QSizeF(10000, 10000)));
    const auto paint = stroke(600, 80, red());
    paint->append(QPointF(NAN, 40));
    paint->append(QPointF(20, 20'000'000));
    QVERIFY(paint->patches().empty());
    paint->append(QPointF(20, 40));
    paint->append(QPointF(20, 40));
    QCOMPARE(paint->patches().size(), size_t(1));
    QCOMPARE(BrushStroke::spacingFraction(1), 0.015);
    QCOMPARE(BrushStroke::spacingFraction(0.5), 0.025);
}

void BrushTests::hardTipsAntialiasTheirRimAndStopAtTheCanvas()
{
    const auto paint = stroke(200, 80, red());
    paint->append(QPointF(100.5, 40.5));
    const QImage live = preview(*paint, QSizeF(200, 80));
    // A pixel centred on the rim is half covered.
    QCOMPARE(alpha(live, 110, 40), 128);
    QCOMPARE(alpha(live, 109, 40), 255);
    QCOMPARE(alpha(live, 111, 40), 0);
    // A layer past the canvas takes no paint out there.
    ImageLayer wide = blankLayer(600, 80);
    wide.transform.origin = QPointF(-100, 0);
    BrushStroke edge(wide, false, red(), QSizeF(600, 80));
    edge.append(QPointF(5, 40));
    const BrushCommit::Output made = BrushCommit::render(edge.commitInput());
    // The crop begins at the canvas edge: grid pixel 100.
    const QPointF shift = edge.committedBounds().topLeft() + made.pixelBounds.topLeft();
    QCOMPARE(shift, QPointF(100, 30));
    QCOMPARE(alpha(made.asset.image(), 105 - int(shift.x()), 40 - int(shift.y())), 255);
    QCOMPARE(alpha(made.asset.image(), 0, 10), 255);
}

void BrushTests::paintingPastTheLayerGrowsItsGrid()
{
    QImage small = BrushRaster::context(20, 20, false);
    small.fill(Qt::blue);
    const ImageLayer layer(ImportedImage(small, small, "Small"), QPointF(10, 10));
    BrushStroke paint(layer, false, red(), QSizeF(600, 80));
    QCOMPARE(QSize(paint.width, paint.height), QSize(600, 80));
    QCOMPARE(paint.sourceRect, QRectF(10, 10, 20, 20));
    paint.append(QPointF(300, 40));
    const PaintSnapshot snapshot = paint.paintSnapshot();
    // The blue stays put; the dab lands past it.
    QCOMPARE(snapshot.bounds, QRectF(10, 10, 300, 40));
    const QImage result = render(snapshot.asset, snapshot.transform, QSizeF(600, 80));
    QCOMPARE(pixel(result, 15, 15), (std::vector<int>{0, 0, 255, 255}));
    QCOMPARE(pixel(result, 300, 40), (std::vector<int>{255, 0, 0, 255}));
    QCOMPARE(alpha(result, 300, 20), 0);
}

void BrushTests::aPaintedLayerPaintsFromItsTilesWithoutFlattening()
{
    QImage base = BrushRaster::context(600, 80, false);
    base.fill(Qt::blue);
    const auto raster = std::make_shared<const RasterSnapshot>(600, 80, base, QRectF(0, 0, 600, 80), std::vector<BrushPatch>{});
    const ImageLayer layer(ImportedImage(raster, PixelAdjust::thumbnail(base), "Painted"), QPointF(0, 0));
    BrushStroke paint(layer, false, red(), QSizeF(600, 80));
    paint.append(QPointF(300, 40));
    const QImage live = preview(paint, QSizeF(600, 80));
    QCOMPARE(pixel(live, 300, 40), (std::vector<int>{255, 0, 0, 255}));
    QCOMPARE(pixel(live, 100, 40), (std::vector<int>{0, 0, 255, 255}));
    // Inside the touched tile, beside the dab: the base stays.
    QCOMPARE(pixel(live, 280, 5), (std::vector<int>{0, 0, 255, 255}));
    QVERIFY(!raster->hasMaterializedPixels());
    const PaintSnapshot snapshot = paint.paintSnapshot();
    QCOMPARE(snapshot.bounds, QRectF(0, 0, 600, 80));
    QVERIFY(!raster->hasMaterializedPixels() && !snapshot.asset.raster->hasMaterializedPixels());
    const BrushCommit::Output made = BrushCommit::render(paint.commitInput());
    QVERIFY(!raster->hasMaterializedPixels());
    QCOMPARE(pixel(made.asset.image(), 300, 40), (std::vector<int>{255, 0, 0, 255}));
    QCOMPARE(pixel(made.asset.image(), 280, 5), (std::vector<int>{0, 0, 255, 255}));
}

void BrushTests::anOrdinarySourceKeepsItsPixelsBesideTheDab()
{
    QImage blue = BrushRaster::context(600, 80, false);
    blue.fill(Qt::blue);
    const ImageLayer layer(ImportedImage(blue, blue, "Blue"), QPointF(0, 0));
    BrushStroke paint(layer, false, red(), QSizeF(600, 80));
    paint.append(QPointF(300, 40));
    const QImage live = preview(paint, QSizeF(600, 80));
    QCOMPARE(pixel(live, 300, 40), (std::vector<int>{255, 0, 0, 255}));
    QCOMPARE(pixel(live, 280, 5), (std::vector<int>{0, 0, 255, 255}));
    const BrushCommit::Output made = BrushCommit::render(paint.commitInput());
    QCOMPARE(made.pixelBounds, QRectF(0, 0, 600, 80));
    QCOMPARE(pixel(made.asset.image(), 280, 5), (std::vector<int>{0, 0, 255, 255}));
    QCOMPARE(pixel(made.asset.image(), 300, 40), (std::vector<int>{255, 0, 0, 255}));
}

void BrushTests::antialiasFollowsTheLayersScale()
{
    // A 64 px source shown at 128: a two-pixel rim.
    QImage small = BrushRaster::context(64, 64, false);
    ImageLayer layer(ImportedImage(small, small, "Small"), QPointF(0, 0));
    layer.transform.size = QSizeF(128, 128);
    BrushStroke paint(layer, false, red(), QSizeF(128, 128));
    paint.append(QPointF(64.5, 65));
    const std::vector<BrushPatch> patches = paint.patches();
    QCOMPARE(patches.size(), size_t(1));
    QCOMPARE(alpha(patches[0].image, 37, 32), 64);
}

void BrushTests::tileKeysFitBigGridsAndACommitThatCannotCropThrows()
{
    if (!placesStarvation())
        QSKIP("the failure point is placed for Qt 6.4's allocations");
    // A tiny layer far across a huge canvas: big keys.
    QImage image = BrushRaster::context(1200, 1200, false);
    image.fill(Qt::blue);
    ImageLayer layer(ImportedImage(image, image, "Far"), QPointF(9999, 9999));
    layer.transform.size = QSizeF(1, 1);
    BrushStroke far(layer, false, red(1), QSizeF(10000, 10000));
    far.append(QPointF(9999.5, 9999.5));
    // A 1 px tip spans 1200 layer pixels: 15 tiles.
    QCOMPARE(far.patches().size(), size_t(225));
    // A crop that cannot allocate is a render error.
    const auto wide = stroke(3000, 3000, red(2000, 1));
    wide->append(QPointF(1500, 1500));
    const BrushCommit::Input input = wide->commitInput();
    malloc_trim(0);
    const AddressSpaceLimit limit(qint64(input.width) * input.height * 4 + 4 * 1024 * 1024);
    QVERIFY_THROWS_EXCEPTION(ExportError, BrushCommit::render(input));
}

void BrushTests::aSelectionClipsOnATransformedGrid()
{
    // Source at 128 from (40,20): the clip follows.
    QImage small = BrushRaster::context(64, 64, false);
    ImageLayer layer(ImportedImage(small, small, "Small"), QPointF(40, 20));
    layer.transform.size = QSizeF(128, 128);
    BrushStroke paint(layer, false, red(), QSizeF(200, 200));
    DocumentSelection selection;
    selection.path.addRect(QRectF(80, 0, 120, 200));
    paint.selectionClip = selection.clip(QSizeF(200, 200));
    paint.append(QPointF(80, 80));
    const QImage live = preview(paint, QSizeF(200, 200));
    QCOMPARE(alpha(live, 85, 80), 255);
    QCOMPARE(alpha(live, 75, 80), 0);
}

void BrushTests::aPlacedMaskPaintsInItsOwnGrid()
{
    QImage image = BrushRaster::context(64, 64, false);
    image.fill(Qt::blue);
    ImageLayer layer(ImportedImage(image, image, "Blue"), QPointF(0, 0));
    QImage black = BrushRaster::context(16, 16, true);
    // A 16 px mask at 32 from (80,40): own grid.
    layer.mask = LayerMask(LayerMask::assetFrom(black), true, LayerTransform{.origin = {80, 40}, .size = {32, 32}}, false);
    BrushStroke paint(layer, true, brush(8, 1, 1, 1, 1), QSizeF(200, 200));
    QCOMPARE(QSize(paint.width, paint.height), QSize(16, 16));
    QCOMPARE(paint.paintTransform, (LayerTransform{.origin = {80, 40}, .size = {32, 32}}));
    paint.append(QPointF(88, 48));
    const BrushCommit::Output made = BrushCommit::render(paint.commitInput());
    const QImage mask = made.asset.image();
    QCOMPARE(mask.size(), QSize(16, 16));
    QCOMPARE(int(mask.constScanLine(4)[4]), 255);
    QCOMPARE(int(mask.constScanLine(12)[12]), 0);
}

QTEST_GUILESS_MAIN(BrushTests)
#include "BrushTests.moc"
