#include "Document/DocumentLimits.h"
#include "Document/DocumentHistory.h"
#include "IO/ImageExporter.h"
#include "Rendering/RasterSnapshot.h"
#include <QtTest>
#include <climits>
#include <latch>
#include <thread>

namespace {
QImage solid(int width, int height, QColor color)
{
    QImage image = BrushRaster::context(width, height, false);
    image.fill(color);
    return image;
}

// Red holds x, green holds y.
QImage coordinates(int width, int height)
{
    QImage image = BrushRaster::context(width, height, false);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x)
            image.setPixelColor(x, y, QColor(x % 256, y % 256, 0));
    }
    return image;
}

ImportedImage painted(std::shared_ptr<const RasterSnapshot> raster)
{
    return ImportedImage(raster, raster->thumbnail(), "Painted");
}
}

class RasterSnapshotTests : public QObject {
    Q_OBJECT
private slots:
    void contextsStartClearInTheLayerFormats();
    void contextAllocationFailureThrowsARenderError();
    void drawReplacesPixelsInsteadOfBlending();
    void drawCopiesOnlyItsSourcePart();
    void visibleRectFollowsDeviceTransformAndClip();
    void drawFillsAWindowMappedDevice();
    void replacingAnImportKeepsItAsTheBase();
    void growingTheCanvasShiftsBaseAndPatches();
    void newerPatchesSplitOlderOnes();
    void untouchedPatchesAreSharedNotCopied();
    void aNeighbourInTheSameCellStaysWhole();
    void drawKeepsHardEdgesUnderAnAntialiasingPainter();
    void concurrentFlatteningHappensOnce();
    void patchesOutsideTheCropAreClippedAway();
    void shrinkingCropsTheBaseOnDraw();
    void maskRastersRevealTheGrownArea();
    void drawScalesIntoTheRequestedRect();
    void flatteningIsLazyAndCached();
    void thumbnailFitsNinetySixPixels();
    void paintedAssetsReportSizeIdentityAndBytesWithoutFlattening();
    void historyBudgetNeverFlattensPaintedTiles();
};

void RasterSnapshotTests::contextsStartClearInTheLayerFormats()
{
    const QImage color = BrushRaster::context(3, 2, false);
    QCOMPARE(color.format(), QImage::Format_RGBA8888_Premultiplied);
    QCOMPARE(color.size(), QSize(3, 2));
    QCOMPARE(color.pixel(2, 1), qRgba(0, 0, 0, 0));
    const QImage mask = BrushRaster::context(3, 2, true);
    QCOMPARE(mask.format(), QImage::Format_Grayscale8);
    QCOMPARE(mask.constScanLine(1)[2], uchar(0));
}

void RasterSnapshotTests::contextAllocationFailureThrowsARenderError()
{
    try {
        BrushRaster::context(INT_MAX, INT_MAX, false);
        QFAIL("expected ExportError");
    } catch (const ExportError &error) {
        QCOMPARE(error.kind, ExportError::Kind::render);
        QCOMPARE(QString(error.what()), QString("The canvas could not be rendered. Try a smaller canvas."));
    }
    QCOMPARE(QString(ExportError(ExportError::Kind::tooLarge).what()),
             QStringLiteral("Image export supports canvases up to %1 megapixels and 30,000 pixels per side.").arg(DocumentLimits::maxSurfaceMegapixels()));
    QCOMPARE(QString(ExportError(ExportError::Kind::encode).what()), QString("The image could not be encoded."));
}

void RasterSnapshotTests::drawReplacesPixelsInsteadOfBlending()
{
    QImage surface = solid(4, 4, Qt::red);
    QPainter painter(&surface);
    BrushRaster::draw(BrushRaster::context(2, 2, false), QRectF(1, 1, 2, 2), painter);
    QCOMPARE(painter.compositionMode(), QPainter::CompositionMode_SourceOver);
    painter.end();
    QCOMPARE(surface.pixel(1, 1), qRgba(0, 0, 0, 0));
    QCOMPARE(surface.pixel(2, 2), qRgba(0, 0, 0, 0));
    QCOMPARE(surface.pixel(0, 0), qRgba(255, 0, 0, 255));
    QCOMPARE(surface.pixel(3, 3), qRgba(255, 0, 0, 255));
}

void RasterSnapshotTests::drawCopiesOnlyItsSourcePart()
{
    QImage surface = solid(6, 4, Qt::red);
    QPainter painter(&surface);
    BrushRaster::draw(coordinates(8, 8), QRectF(1, 1, 4, 2), painter, QRectF(2, 3, 4, 2));
    painter.end();
    QCOMPARE(surface.pixel(1, 1), qRgba(2, 3, 0, 255));
    QCOMPARE(surface.pixel(4, 2), qRgba(5, 4, 0, 255));
    QCOMPARE(surface.pixel(0, 1), qRgba(255, 0, 0, 255));
    QCOMPARE(surface.pixel(5, 2), qRgba(255, 0, 0, 255));
    QCOMPARE(surface.pixel(1, 3), qRgba(255, 0, 0, 255));
}

void RasterSnapshotTests::visibleRectFollowsDeviceTransformAndClip()
{
    QImage surface = BrushRaster::context(100, 50, false);
    QPainter painter(&surface);
    QCOMPARE(BrushRaster::visibleRect(painter), QRectF(0, 0, 100, 50));
    painter.translate(10, 0);
    QCOMPARE(BrushRaster::visibleRect(painter), QRectF(-10, 0, 100, 50));
    painter.scale(2, 2);
    QCOMPARE(BrushRaster::visibleRect(painter), QRectF(-5, 0, 50, 25));
    painter.setClipRect(QRectF(5, 5, 10, 10));
    QCOMPARE(BrushRaster::visibleRect(painter), QRectF(5, 5, 10, 10));
    painter.setClipRect(QRectF(40, 20, 100, 100));
    QCOMPARE(BrushRaster::visibleRect(painter), QRectF(40, 20, 5, 5));
    painter.end();

    QPainter windowed(&surface);
    windowed.setWindow(0, 0, 200, 100);
    QCOMPARE(BrushRaster::visibleRect(windowed), QRectF(0, 0, 200, 100));
    windowed.end();

    QImage dense = BrushRaster::context(100, 50, false);
    dense.setDevicePixelRatio(2);
    QPainter scaled(&dense);
    QCOMPARE(BrushRaster::visibleRect(scaled), QRectF(0, 0, 50, 25));
}

void RasterSnapshotTests::drawFillsAWindowMappedDevice()
{
    const RasterSnapshot raster(200, 100, solid(200, 100, Qt::red), QRectF(0, 0, 200, 100),
                                {BrushPatch{QRectF(100, 50, 100, 50), solid(100, 50, Qt::blue)}});
    QImage surface = solid(100, 50, Qt::green);
    QPainter painter(&surface);
    painter.setWindow(0, 0, 200, 100);
    raster.draw(QRectF(0, 0, 200, 100), painter);
    painter.end();
    QCOMPARE(surface.pixel(0, 0), qRgba(255, 0, 0, 255));
    QCOMPARE(surface.pixel(99, 0), qRgba(255, 0, 0, 255));
    QCOMPARE(surface.pixel(0, 49), qRgba(255, 0, 0, 255));
    QCOMPARE(surface.pixel(50, 25), qRgba(0, 0, 255, 255));
    QCOMPARE(surface.pixel(99, 49), qRgba(0, 0, 255, 255));
}

void RasterSnapshotTests::replacingAnImportKeepsItAsTheBase()
{
    const QImage pixels = solid(8, 8, Qt::red);
    const ImportedImage source(pixels, QImage(), "Photo");
    const auto raster = RasterSnapshot::replacing(source, QRectF(0, 0, 8, 8),
        {BrushPatch{QRectF(2, 2, 4, 4), solid(4, 4, Qt::blue)}}, QRectF(0, 0, 8, 8));
    QCOMPARE(raster->width, 8);
    QCOMPARE(raster->height, 8);
    QCOMPARE(raster->base.cacheKey(), pixels.cacheKey());
    QCOMPARE(raster->baseRect, QRectF(0, 0, 8, 8));
    QCOMPARE(raster->alignment, QPointF(0, 0));
    QVERIFY(!raster->isMask);
    QCOMPARE(int(raster->patches.size()), 1);
    QCOMPARE(raster->patches[0].rect, QRectF(2, 2, 4, 4));
    const QImage flat = raster->makeImage();
    QCOMPARE(flat.pixel(1, 1), qRgba(255, 0, 0, 255));
    QCOMPARE(flat.pixel(2, 2), qRgba(0, 0, 255, 255));
    QCOMPARE(flat.pixel(5, 5), qRgba(0, 0, 255, 255));
    QCOMPARE(flat.pixel(6, 6), qRgba(255, 0, 0, 255));
}

void RasterSnapshotTests::growingTheCanvasShiftsBaseAndPatches()
{
    const ImportedImage source(solid(8, 8, Qt::red), QImage(), "Photo");
    const auto first = RasterSnapshot::replacing(source, QRectF(10, 20, 8, 8),
        {BrushPatch{QRectF(6, 18, 4, 4), solid(4, 4, Qt::blue)}}, QRectF(6, 18, 12, 10));
    QCOMPARE(first->width, 12);
    QCOMPARE(first->height, 10);
    QCOMPARE(first->baseRect, QRectF(4, 2, 8, 8));
    QCOMPARE(first->alignment, QPointF(4, 2));
    QCOMPARE(first->patches[0].rect, QRectF(0, 0, 4, 4));
    const QImage flat = first->makeImage();
    QCOMPARE(flat.pixel(0, 0), qRgba(0, 0, 255, 255));
    QCOMPARE(flat.pixel(3, 3), qRgba(0, 0, 255, 255));
    QCOMPARE(flat.pixel(4, 4), qRgba(255, 0, 0, 255));
    QCOMPARE(flat.pixel(11, 9), qRgba(255, 0, 0, 255));
    QCOMPARE(flat.pixel(11, 0), qRgba(0, 0, 0, 0));
    QCOMPARE(flat.pixel(0, 9), qRgba(0, 0, 0, 0));

    const auto second = RasterSnapshot::replacing(painted(first), QRectF(6, 18, 12, 10), {}, QRectF(4, 15, 14, 13));
    QCOMPARE(second->baseRect, QRectF(6, 5, 8, 8));
    QCOMPARE(second->alignment, QPointF(6, 5));
    QCOMPARE(second->patches[0].rect, QRectF(2, 3, 4, 4));
    QCOMPARE(second->base.cacheKey(), first->base.cacheKey());
}

void RasterSnapshotTests::newerPatchesSplitOlderOnes()
{
    const QImage tile = coordinates(256, 256);
    const auto old = std::make_shared<const RasterSnapshot>(256, 256, QImage(), QRectF(),
        std::vector<BrushPatch>{BrushPatch{QRectF(0, 0, 256, 256), tile}});
    const auto raster = RasterSnapshot::replacing(painted(old), QRectF(0, 0, 256, 256),
        {BrushPatch{QRectF(100, 90, 56, 40), solid(56, 40, Qt::blue)}}, QRectF(0, 0, 256, 256));
    QCOMPARE(int(raster->patches.size()), 5);
    const QRectF expected[] = {QRectF(0, 0, 256, 90), QRectF(0, 130, 256, 126), QRectF(0, 90, 100, 40),
                               QRectF(156, 90, 100, 40), QRectF(100, 90, 56, 40)};
    for (int index = 0; index < 5; ++index) {
        QCOMPARE(raster->patches[index].rect, expected[index]);
        QCOMPARE(QSizeF(raster->patches[index].image.size()), expected[index].size());
    }
    QVERIFY(raster->base.isNull());
    const QImage flat = raster->makeImage();
    for (const QPoint point : {QPoint(0, 0), QPoint(255, 89), QPoint(99, 90), QPoint(156, 129), QPoint(0, 130), QPoint(255, 255)})
        QCOMPARE(flat.pixel(point), qRgba(point.x(), point.y(), 0, 255));
    for (const QPoint point : {QPoint(100, 90), QPoint(155, 129)})
        QCOMPARE(flat.pixel(point), qRgba(0, 0, 255, 255));
}

void RasterSnapshotTests::untouchedPatchesAreSharedNotCopied()
{
    const QImage near = solid(256, 256, Qt::green), far = solid(256, 256, Qt::yellow);
    const auto old = std::make_shared<const RasterSnapshot>(1024, 256, QImage(), QRectF(),
        std::vector<BrushPatch>{BrushPatch{QRectF(0, 0, 256, 256), near}, BrushPatch{QRectF(768, 0, 256, 256), far}});
    const auto raster = RasterSnapshot::replacing(painted(old), QRectF(0, 0, 1024, 256),
        {BrushPatch{QRectF(0, 0, 256, 256), solid(256, 256, Qt::blue)}}, QRectF(0, 0, 1024, 256));
    QCOMPARE(int(raster->patches.size()), 2);
    QCOMPARE(raster->patches[0].rect, QRectF(768, 0, 256, 256));
    QCOMPARE(raster->patches[0].image.cacheKey(), far.cacheKey());
    QCOMPARE(raster->patches[1].rect, QRectF(0, 0, 256, 256));
    QCOMPARE(raster->makeImage().pixel(10, 10), qRgba(0, 0, 255, 255));
}

void RasterSnapshotTests::aNeighbourInTheSameCellStaysWhole()
{
    const QImage neighbour = solid(100, 100, Qt::green);
    const auto old = std::make_shared<const RasterSnapshot>(256, 256, QImage(), QRectF(),
        std::vector<BrushPatch>{BrushPatch{QRectF(0, 0, 100, 100), neighbour}});
    const auto raster = RasterSnapshot::replacing(painted(old), QRectF(0, 0, 256, 256),
        {BrushPatch{QRectF(150, 150, 50, 50), solid(50, 50, Qt::blue)}}, QRectF(0, 0, 256, 256));
    QCOMPARE(int(raster->patches.size()), 2);
    QCOMPARE(raster->patches[0].rect, QRectF(0, 0, 100, 100));
    QCOMPARE(raster->patches[0].image.cacheKey(), neighbour.cacheKey());
    QCOMPARE(raster->patches[1].rect, QRectF(150, 150, 50, 50));
}

void RasterSnapshotTests::drawKeepsHardEdgesUnderAnAntialiasingPainter()
{
    const RasterSnapshot raster(4, 1, solid(4, 1, Qt::red), QRectF(0, 0, 4, 1), {});
    QImage surface = solid(6, 1, Qt::green);
    QPainter painter(&surface);
    painter.setRenderHint(QPainter::Antialiasing, true);
    raster.draw(QRectF(0.5, 0, 4, 1), painter);
    QVERIFY(painter.testRenderHint(QPainter::Antialiasing));
    painter.end();
    int red = 0;
    for (int x = 0; x < 6; ++x) {
        const QRgb pixel = surface.pixel(x, 0);
        QVERIFY(pixel == qRgba(255, 0, 0, 255) || pixel == qRgba(0, 255, 0, 255));
        red += pixel == qRgba(255, 0, 0, 255);
    }
    QCOMPARE(red, 4);

    const RasterSnapshot mask(4, 1, QImage(), QRectF(), {}, true);
    QImage coverage = BrushRaster::context(6, 1, true);
    QPainter masking(&coverage);
    masking.setRenderHint(QPainter::Antialiasing, true);
    mask.draw(QRectF(0.5, 0, 4, 1), masking);
    masking.end();
    int white = 0;
    for (int x = 0; x < 6; ++x) {
        const uchar value = coverage.constScanLine(0)[x];
        QVERIFY(value == 0 || value == 255);
        white += value == 255;
    }
    QCOMPARE(white, 4);
}

void RasterSnapshotTests::concurrentFlatteningHappensOnce()
{
    std::vector<BrushPatch> patches;
    for (int index = 0; index < 16; ++index)
        patches.push_back({QRectF(index % 4 * 256, index / 4 * 256, 256, 256), coordinates(256, 256)});
    const RasterSnapshot raster(1024, 1024, QImage(), QRectF(), patches);
    constexpr int workers = 16;
    std::latch start(1);
    std::vector<qint64> keys(workers);
    std::vector<std::thread> threads;
    for (int index = 0; index < workers; ++index) {
        threads.emplace_back([&, index] {
            start.wait();
            keys[index] = raster.makeImage().cacheKey();
        });
    }
    start.count_down();
    for (std::thread &thread : threads)
        thread.join();
    for (qint64 key : keys)
        QCOMPARE(key, keys[0]);
    QCOMPARE(raster.makeImage().pixel(300, 700), qRgba(300 % 256, 700 % 256, 0, 255));
}

void RasterSnapshotTests::patchesOutsideTheCropAreClippedAway()
{
    const auto raster = RasterSnapshot::replacing(std::nullopt, QRectF(0, 0, 40, 40),
        {BrushPatch{QRectF(-10, -10, 20, 20), coordinates(20, 20)}, BrushPatch{QRectF(60, 0, 10, 10), solid(10, 10, Qt::blue)}},
        QRectF(0, 0, 40, 40));
    QCOMPARE(int(raster->patches.size()), 1);
    QCOMPARE(raster->patches[0].rect, QRectF(0, 0, 10, 10));
    QCOMPARE(raster->patches[0].image.size(), QSize(10, 10));
    QCOMPARE(raster->patches[0].image.pixel(0, 0), qRgba(10, 10, 0, 255));
    QCOMPARE(raster->patches[0].image.pixel(9, 9), qRgba(19, 19, 0, 255));
    QVERIFY(raster->base.isNull());
    QCOMPARE(raster->baseRect, QRectF(0, 0, 40, 40));
}

void RasterSnapshotTests::shrinkingCropsTheBaseOnDraw()
{
    const ImportedImage source(coordinates(40, 30), QImage(), "Photo");
    const auto raster = RasterSnapshot::replacing(source, QRectF(0, 0, 40, 30), {}, QRectF(10, 5, 20, 20));
    QCOMPARE(raster->baseRect, QRectF(-10, -5, 40, 30));
    const QImage flat = raster->makeImage();
    QCOMPARE(flat.size(), QSize(20, 20));
    QCOMPARE(flat.pixel(0, 0), qRgba(10, 5, 0, 255));
    QCOMPARE(flat.pixel(19, 19), qRgba(29, 24, 0, 255));

    QImage surface = solid(30, 30, Qt::green);
    QPainter painter(&surface);
    raster->draw(QRectF(5, 5, 20, 20), painter);
    painter.end();
    QCOMPARE(surface.pixel(5, 5), qRgba(10, 5, 0, 255));
    QCOMPARE(surface.pixel(24, 24), qRgba(29, 24, 0, 255));
    for (const QPoint outside : {QPoint(4, 5), QPoint(5, 4), QPoint(25, 24), QPoint(24, 25)})
        QCOMPARE(surface.pixel(outside), qRgba(0, 255, 0, 255));
}

void RasterSnapshotTests::maskRastersRevealTheGrownArea()
{
    QImage gray = BrushRaster::context(8, 8, true);
    gray.fill(100);
    const ImportedImage source(gray, QImage(), "Mask");
    const auto raster = RasterSnapshot::replacing(source, QRectF(4, 2, 8, 8), {}, QRectF(0, 0, 16, 12), true);
    QVERIFY(raster->isMask);
    const QImage flat = raster->makeImage();
    QCOMPARE(flat.format(), QImage::Format_Grayscale8);
    QCOMPARE(flat.constScanLine(0)[0], uchar(255));
    QCOMPARE(flat.constScanLine(11)[15], uchar(255));
    QCOMPARE(flat.constScanLine(2)[4], uchar(100));
    QCOMPARE(flat.constScanLine(9)[11], uchar(100));
    QCOMPARE(flat.constScanLine(10)[11], uchar(255));
}

void RasterSnapshotTests::drawScalesIntoTheRequestedRect()
{
    const ImportedImage source(solid(8, 8, Qt::red), QImage(), "Photo");
    const auto raster = RasterSnapshot::replacing(source, QRectF(0, 0, 8, 8),
        {BrushPatch{QRectF(4, 0, 4, 8), solid(4, 8, Qt::blue)}}, QRectF(0, 0, 8, 8));
    QImage surface = solid(12, 6, Qt::green);
    QPainter painter(&surface);
    raster->draw(QRectF(2, 1, 4, 4), painter);
    QVERIFY(!painter.hasClipping());
    painter.end();
    QCOMPARE(surface.pixel(2, 1), qRgba(255, 0, 0, 255));
    QCOMPARE(surface.pixel(3, 4), qRgba(255, 0, 0, 255));
    QCOMPARE(surface.pixel(4, 1), qRgba(0, 0, 255, 255));
    QCOMPARE(surface.pixel(5, 4), qRgba(0, 0, 255, 255));
    QCOMPARE(surface.pixel(1, 1), qRgba(0, 255, 0, 255));
    QCOMPARE(surface.pixel(6, 4), qRgba(0, 255, 0, 255));
    QCOMPARE(surface.pixel(2, 5), qRgba(0, 255, 0, 255));
    QVERIFY(!raster->hasMaterializedPixels());

    QImage pair = BrushRaster::context(2, 1, false);
    pair.setPixelColor(0, 0, Qt::red);
    pair.setPixelColor(1, 0, Qt::blue);
    QImage enlarged = BrushRaster::context(8, 1, false);
    QPainter enlarging(&enlarged);
    enlarging.setRenderHint(QPainter::SmoothPixmapTransform, true);
    RasterSnapshot(2, 1, pair, QRectF(0, 0, 2, 1), {}).draw(QRectF(0, 0, 8, 1), enlarging);
    enlarging.end();
    for (int x = 0; x < 8; ++x)
        QCOMPARE(enlarged.pixel(x, 0), x < 4 ? qRgba(255, 0, 0, 255) : qRgba(0, 0, 255, 255));
}

void RasterSnapshotTests::flatteningIsLazyAndCached()
{
    const ImportedImage source(solid(8, 8, Qt::red), QImage(), "Photo");
    const auto raster = RasterSnapshot::replacing(source, QRectF(0, 0, 8, 8), {}, QRectF(0, 0, 8, 8));
    QVERIFY(!raster->hasMaterializedPixels());
    raster->thumbnail();
    QVERIFY(!raster->hasMaterializedPixels());
    const QImage first = raster->makeImage();
    QVERIFY(raster->hasMaterializedPixels());
    QCOMPARE(raster->makeImage().cacheKey(), first.cacheKey());
}

void RasterSnapshotTests::thumbnailFitsNinetySixPixels()
{
    const RasterSnapshot wide(4000, 2000, QImage(), QRectF(), {BrushPatch{QRectF(0, 0, 2000, 2000), solid(2, 2, Qt::blue)}});
    const QImage thumbnail = wide.thumbnail();
    QCOMPARE(thumbnail.size(), QSize(96, 48));
    QCOMPARE(thumbnail.pixel(47, 47), qRgba(0, 0, 255, 255));
    QCOMPARE(thumbnail.pixel(48, 47), qRgba(0, 0, 0, 0));
    QCOMPARE(RasterSnapshot(8, 4, QImage(), QRectF(), {}).thumbnail().size(), QSize(8, 4));
    QCOMPARE(RasterSnapshot(4000, 10, QImage(), QRectF(), {}).thumbnail().size(), QSize(96, 1));
    QCOMPARE(RasterSnapshot(8, 4, QImage(), QRectF(3, 2, 1, 1), {}).alignment, QPointF(3, 2));
    QCOMPARE(RasterSnapshot(8, 4, QImage(), QRectF(3, 2, 1, 1), {}, false, QPointF(7, 9)).alignment, QPointF(7, 9));
}

void RasterSnapshotTests::paintedAssetsReportSizeIdentityAndBytesWithoutFlattening()
{
    const auto raster = std::make_shared<const RasterSnapshot>(40, 30, QImage(), QRectF(), std::vector<BrushPatch>());
    const auto mask = std::make_shared<const RasterSnapshot>(40, 30, QImage(), QRectF(), std::vector<BrushPatch>(), true);
    const ImportedImage asset = painted(raster);
    QCOMPARE(asset.size(), QSize(40, 30));
    QCOMPARE(asset.byteCount(), qint64(40 * 30 * 4));
    QCOMPARE(painted(mask).byteCount(), qint64(40 * 30));
    QVERIFY(asset.identity() == painted(raster).identity());
    QVERIFY(!(asset.identity() == painted(mask).identity()));
    QVERIFY(!raster->hasMaterializedPixels());
    QCOMPARE(asset.image().size(), QSize(40, 30));
    QVERIFY(raster->hasMaterializedPixels());

    const QImage pixels = solid(5, 4, Qt::red);
    const ImportedImage plain(pixels, QImage(), "Photo");
    QCOMPARE(plain.size(), QSize(5, 4));
    QCOMPARE(plain.byteCount(), qint64(5 * 4 * 4));
    QCOMPARE(plain.image().cacheKey(), pixels.cacheKey());
    QVERIFY(plain.identity() == ImportedImage(pixels, QImage(), "Copy").identity());
    QVERIFY(!(plain.identity() == ImportedImage(pixels.copy(), QImage(), "Photo").identity()));
    QVERIFY(!(plain.identity() == asset.identity()));
}

void RasterSnapshotTests::historyBudgetNeverFlattensPaintedTiles()
{
    const auto raster = std::make_shared<const RasterSnapshot>(40, 30, QImage(), QRectF(), std::vector<BrushPatch>());
    DocumentHistory history;
    std::optional<CanvasDocument> document = CanvasDocument(40, 30);
    document.value().layers.push_back(ImageLayer(painted(raster), QPointF(0, 0)));
    QVERIFY(document.value().layers[0] == ImageLayer(document.value().layers[0]));
    history.begin("Select Layer", document, std::nullopt);
    history.end(document, document.value().layers[0].id);
    QCOMPARE(history.undoCount(), 0);
    QVERIFY(!raster->hasMaterializedPixels());
    history.begin("Delete Layer", document, std::nullopt);
    const qint64 thumbnailBytes = document.value().layers[0].asset.value().thumbnail.sizeInBytes();
    document.value().layers.clear();
    history.end(document, std::nullopt);
    QCOMPARE(history.retainedBytes(document), qint64(40 * 30 * 4) + thumbnailBytes);
    QVERIFY(!raster->hasMaterializedPixels());
}

QTEST_APPLESS_MAIN(RasterSnapshotTests)
#include "RasterSnapshotTests.moc"
