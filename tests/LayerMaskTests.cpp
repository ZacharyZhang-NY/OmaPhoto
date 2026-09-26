#include "Document/BrushStroke.h"
#include "Document/DocumentHistory.h"
#include "Document/EditorSession.h"
#include "IO/ImageExporter.h"
#include "Rendering/RasterSnapshot.h"
#include "AddressSpaceLimit.h"
#include <QtTest>

namespace {
QImage gray(int width, int height, int value)
{
    QImage image = BrushRaster::context(width, height, true);
    image.fill(value);
    return image;
}

int value(const QImage &image, int x, int y)
{
    return image.constScanLine(y)[x];
}

LayerMask wideMask()
{
    return LayerMask(LayerMask::assetFrom(gray(40, 20, 255)));
}
}

class LayerMaskTests : public QObject {
    Q_OBJECT
private slots:
    void solidMasksAreOnePixelOfWhiteOrBlack();
    void validityRequiresEightBitGray();
    void assetFromRejectsOtherFormatsAndBuildsAThumbnail();
    void assetFromThrowsWhenTheThumbnailCannotBeAllocated();
    void paintedMasksCompareWithoutFlattening();
    void masksCompareByPixelIdentityAndFlags();
    void replacingKeepsFlagsAndPlacement();
    void enabledImageIsNilWhileDisabled();
    void uniformMasksNeverTakeAPlacement();
    void linkedMasksFollowTheirLayer();
    void unlinkedMasksStayOnTheDocument();
    void backgroundFollowsTheEdgeMajority();
    void placedResamplesAMaskIntoTheLayerGrid();
    void drawSmoothReplacesAndInterpolates();
    void maskTransformPrefersThePlacement();
    void layersCompareAndRecordTheirMasks();
    void historyCountsMaskPixels();
};

void LayerMaskTests::solidMasksAreOnePixelOfWhiteOrBlack()
{
    const LayerMask reveal = LayerMask::solid(true), hide = LayerMask::solid(false);
    QCOMPARE(reveal.asset.size(), QSize(1, 1));
    QCOMPARE(reveal.asset.image().format(), QImage::Format_Grayscale8);
    QCOMPARE(value(reveal.asset.image(), 0, 0), 255);
    QCOMPARE(value(hide.asset.image(), 0, 0), 0);
    QCOMPARE(reveal.asset.name, QString("Layer Mask"));
    QCOMPARE(reveal.asset.thumbnail.cacheKey(), reveal.asset.image().cacheKey());
    QVERIFY(reveal.isEnabled);
    QVERIFY(reveal.isLinked);
    QVERIFY(!reveal.placement.has_value());
}

void LayerMaskTests::validityRequiresEightBitGray()
{
    QVERIFY(LayerMask::isValid(gray(2, 2, 0)));
    QVERIFY(!LayerMask::isValid(BrushRaster::context(2, 2, false)));
    QVERIFY(!LayerMask::isValid(QImage(2, 2, QImage::Format_Alpha8)));
    QVERIFY(!LayerMask::isValid(QImage(2, 2, QImage::Format_Grayscale16)));
    QVERIFY(!LayerMask::isValid(QImage()));
}

void LayerMaskTests::assetFromRejectsOtherFormatsAndBuildsAThumbnail()
{
    try {
        LayerMask::assetFrom(BrushRaster::context(4, 4, false));
        QFAIL("expected ProjectError");
    } catch (const ProjectError &error) {
        QCOMPARE(error.kind, ProjectError::Kind::invalid);
    }
    QImage halves = gray(200, 100, 0);
    QPainter painter(&halves);
    painter.fillRect(0, 0, 100, 100, Qt::white);
    painter.end();
    const ImportedImage asset = LayerMask::assetFrom(halves);
    QCOMPARE(asset.image().cacheKey(), halves.cacheKey());
    QCOMPARE(asset.name, QString("Layer Mask"));
    QCOMPARE(asset.thumbnail.size(), QSize(96, 48));
    QCOMPARE(asset.thumbnail.format(), QImage::Format_Grayscale8);
    QCOMPARE(value(asset.thumbnail, 10, 10), 255);
    QCOMPARE(value(asset.thumbnail, 40, 40), 255);
    QCOMPARE(value(asset.thumbnail, 56, 10), 0);
    QCOMPARE(value(asset.thumbnail, 90, 40), 0);
    QCOMPARE(LayerMask::assetFrom(gray(8, 4, 9)).thumbnail.size(), QSize(8, 4));
    QCOMPARE(value(LayerMask::assetFrom(gray(8, 4, 9)).thumbnail, 7, 3), 9);
    QCOMPARE(LayerMask::assetFrom(gray(10, 4000, 0)).thumbnail.size(), QSize(1, 96));

    QImage stripes = gray(200, 100, 0);
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 200; x += 2)
            stripes.scanLine(y)[x] = 255;
    }
    const QImage averaged = LayerMask::assetFrom(stripes).thumbnail;
    QCOMPARE(averaged.format(), QImage::Format_Grayscale8);
    for (int x = 8; x < 88; x += 8)
        QVERIFY(value(averaged, x, 24) > 60 && value(averaged, x, 24) < 195);
}

void LayerMaskTests::assetFromThrowsWhenTheThumbnailCannotBeAllocated()
{
    const QImage large = gray(8192, 8192, 255);
    std::optional<AddressSpaceLimit> limit(std::in_place, 16 * 1024 * 1024);
    std::optional<ExportError::Kind> thrown;
    try {
        LayerMask::assetFrom(large);
    } catch (const ExportError &error) {
        thrown = error.kind;
    }
    limit.reset();
    QCOMPARE(thrown, std::optional(ExportError::Kind::render));
}

void LayerMaskTests::paintedMasksCompareWithoutFlattening()
{
    const auto raster = std::make_shared<const RasterSnapshot>(40, 20, QImage(), QRectF(), std::vector<BrushPatch>(), true);
    const auto other = std::make_shared<const RasterSnapshot>(40, 20, QImage(), QRectF(), std::vector<BrushPatch>(), true);
    const LayerMask mask(ImportedImage(raster, raster->thumbnail(), "Layer Mask"));
    QVERIFY(mask == LayerMask(ImportedImage(raster, raster->thumbnail(), "Layer Mask")));
    QVERIFY(!(mask == LayerMask(ImportedImage(other, other->thumbnail(), "Layer Mask"))));
    QVERIFY(!raster->hasMaterializedPixels());
    QVERIFY(!other->hasMaterializedPixels());
}

void LayerMaskTests::masksCompareByPixelIdentityAndFlags()
{
    const QImage pixels = gray(4, 4, 128);
    const LayerMask mask{LayerMask::assetFrom(pixels)};
    const auto differs = [&](auto change) {
        LayerMask other = mask;
        change(other);
        return !(mask == other);
    };
    QVERIFY(mask == LayerMask(mask));
    QVERIFY(mask == LayerMask{LayerMask::assetFrom(pixels)});
    QVERIFY(differs([&](LayerMask &other) { other.asset = LayerMask::assetFrom(pixels.copy()); }));
    QVERIFY(differs([](LayerMask &other) { other.isEnabled = false; }));
    QVERIFY(differs([](LayerMask &other) { other.isLinked = false; }));
    QVERIFY(differs([](LayerMask &other) { other.placement = LayerTransform{.origin = {1, 2}, .size = {3, 4}}; }));
}

void LayerMaskTests::replacingKeepsFlagsAndPlacement()
{
    const LayerTransform apart{.origin = {5, 6}, .size = {7, 8}};
    const LayerMask hidden(LayerMask::assetFrom(gray(4, 4, 10)), false, apart, true);
    QCOMPARE(hidden.isEnabled, false);
    QCOMPARE(hidden.isLinked, true);
    QCOMPARE(hidden.placement, std::optional(apart));
    const LayerMask unlinked(LayerMask::assetFrom(gray(4, 4, 10)), true, std::nullopt, false);
    QCOMPARE(unlinked.isEnabled, true);
    QCOMPARE(unlinked.isLinked, false);
    QVERIFY(!unlinked.placement.has_value());

    const QImage repainted = gray(4, 4, 200);
    const LayerMask replaced = hidden.replacing(LayerMask::assetFrom(repainted));
    QCOMPARE(replaced.asset.image().cacheKey(), repainted.cacheKey());
    QCOMPARE(replaced.isEnabled, false);
    QCOMPARE(replaced.isLinked, true);
    QCOMPARE(replaced.placement, std::optional(apart));
    const LayerMask kept = unlinked.replacing(LayerMask::assetFrom(repainted));
    QCOMPARE(kept.isEnabled, true);
    QCOMPARE(kept.isLinked, false);
    QVERIFY(!kept.placement.has_value());
}

void LayerMaskTests::enabledImageIsNilWhileDisabled()
{
    const QImage pixels = gray(4, 4, 10);
    LayerMask mask{LayerMask::assetFrom(pixels)};
    QCOMPARE(mask.enabledImage().value().cacheKey(), pixels.cacheKey());
    mask.isEnabled = false;
    QVERIFY(!mask.enabledImage().has_value());
}

void LayerMaskTests::uniformMasksNeverTakeAPlacement()
{
    const LayerTransform old{.origin = {0, 0}, .size = {100, 50}}, moved{.origin = {30, 0}, .size = {100, 50}};
    LayerMask solid = LayerMask::solid(true);
    solid.isLinked = false;
    QVERIFY(!solid.placementMovingLayer(old, moved).has_value());
    LayerMask column{LayerMask::assetFrom(gray(1, 2, 255))};
    column.isLinked = false;
    QCOMPARE(column.placementMovingLayer(old, moved), std::optional(old));
    LayerMask row{LayerMask::assetFrom(gray(2, 1, 255))};
    row.isLinked = false;
    QCOMPARE(row.placementMovingLayer(old, moved), std::optional(old));
}

void LayerMaskTests::linkedMasksFollowTheirLayer()
{
    const LayerTransform old{.origin = {0, 0}, .size = {100, 50}}, moved{.origin = {30, -10}, .size = {100, 50}};
    LayerMask mask = wideMask();
    QVERIFY(!mask.placementMovingLayer(old, moved).has_value());
    mask.placement = LayerTransform{.origin = {10, 10}, .size = {40, 20}};
    QCOMPARE(mask.placementMovingLayer(old, moved), std::optional(LayerTransform{.origin = {40, 0}, .size = {40, 20}}));
    mask.placement = LayerTransform{.origin = {0, 0}, .size = {100, 50}, .sampling = LayerSampling::nearest};
    QVERIFY(!mask.placementMovingLayer(old, moved).has_value());
}

void LayerMaskTests::unlinkedMasksStayOnTheDocument()
{
    const LayerTransform old{.origin = {0, 0}, .size = {100, 50}}, moved{.origin = {30, -10}, .size = {100, 50}};
    LayerMask mask = wideMask();
    mask.isLinked = false;
    QCOMPARE(mask.placementMovingLayer(old, moved), std::optional(old));
    QVERIFY(!mask.placementMovingLayer(old, old).has_value());
    const LayerTransform apart{.origin = {10, 10}, .size = {40, 20}};
    mask.placement = apart;
    QCOMPARE(mask.placementMovingLayer(old, moved), std::optional(apart));
    QVERIFY(!mask.placementMovingLayer(old, apart).has_value());
}

void LayerMaskTests::backgroundFollowsTheEdgeMajority()
{
    QCOMPARE(LayerMask::background(gray(6, 4, 255)), 1.0);
    QCOMPARE(LayerMask::background(gray(6, 4, 0)), 0.0);
    QCOMPARE(LayerMask::background(QImage()), 1.0);
    QImage whiteRim = gray(6, 4, 255);
    QImage blackRim = gray(6, 4, 0);
    for (int y = 1; y < 3; ++y) {
        for (int x = 1; x < 5; ++x) {
            whiteRim.scanLine(y)[x] = 0;
            blackRim.scanLine(y)[x] = 255;
        }
    }
    QCOMPARE(LayerMask::background(whiteRim), 1.0);
    QCOMPARE(LayerMask::background(blackRim), 0.0);
    QImage tie = gray(2, 2, 0);
    tie.scanLine(0)[0] = 255;
    tie.scanLine(0)[1] = 255;
    QCOMPARE(LayerMask::background(tie), 1.0);
    tie.scanLine(0)[1] = 254;
    QCOMPARE(LayerMask::background(tie), 0.0);
    QImage wideInterior = gray(10, 10, 0);
    for (int index = 0; index < 10; ++index) {
        wideInterior.scanLine(0)[index] = wideInterior.scanLine(9)[index] = 255;
        wideInterior.scanLine(index)[0] = wideInterior.scanLine(index)[9] = 255;
    }
    QCOMPARE(LayerMask::background(wideInterior), 1.0);

    // Each side's middle pixels tip an eight-of-sixteen tie.
    const QPoint corners[] = {QPoint(0, 0), QPoint(4, 0), QPoint(0, 4), QPoint(4, 4)};
    const struct { QPoint middle[3]; QPoint extra; } sides[] = {
        {{QPoint(1, 0), QPoint(2, 0), QPoint(3, 0)}, QPoint(0, 2)},
        {{QPoint(1, 4), QPoint(2, 4), QPoint(3, 4)}, QPoint(0, 2)},
        {{QPoint(0, 1), QPoint(0, 2), QPoint(0, 3)}, QPoint(2, 0)},
        {{QPoint(4, 1), QPoint(4, 2), QPoint(4, 3)}, QPoint(2, 0)}};
    for (const auto &side : sides) {
        QImage edges = gray(5, 5, 0);
        for (const QPoint point : corners)
            edges.scanLine(point.y())[point.x()] = 255;
        for (const QPoint point : side.middle)
            edges.scanLine(point.y())[point.x()] = 255;
        edges.scanLine(side.extra.y())[side.extra.x()] = 255;
        QCOMPARE(LayerMask::background(edges), 1.0);
        edges.scanLine(side.middle[1].y())[side.middle[1].x()] = 0;
        QCOMPARE(LayerMask::background(edges), 0.0);
    }

    QVERIFY_THROWS_EXCEPTION(std::logic_error, LayerMask::background(BrushRaster::context(4, 4, false)));
}

void LayerMaskTests::placedResamplesAMaskIntoTheLayerGrid()
{
    const LayerTransform layer{.origin = {0, 0}, .size = {100, 100}};
    const LayerTransform placement{.origin = {50, 0}, .size = {50, 100}};
    const QImage black = gray(2, 2, 0);
    const auto compose = [&](QPainter &context) { LayerMask::drawSmooth(black, QRectF(0, 0, 2, 2), context); };
    const QImage full = LayerMask::placed(100, 100, layer, placement, 2, 2, 1, compose);
    QCOMPARE(full.format(), QImage::Format_Grayscale8);
    QCOMPARE(full.size(), QSize(100, 100));
    QCOMPARE(value(full, 25, 50), 255);
    QCOMPARE(value(full, 49, 99), 255);
    QCOMPARE(value(full, 50, 0), 0);
    QCOMPARE(value(full, 99, 99), 0);
    const QImage half = LayerMask::placed(50, 50, layer, placement, 2, 2, 0, [&](QPainter &context) {
        LayerMask::drawSmooth(gray(2, 2, 255), QRectF(0, 0, 2, 2), context);
    });
    QCOMPARE(half.size(), QSize(50, 50));
    QCOMPARE(value(half, 12, 25), 0);
    QCOMPARE(value(half, 37, 25), 255);
    const LayerTransform turned{.origin = {25, 25}, .size = {50, 50}, .rotation = 45};
    const QImage diamond = LayerMask::placed(100, 100, layer, turned, 2, 2, 1, compose);
    QCOMPARE(value(diamond, 50, 50), 0);
    QCOMPARE(value(diamond, 50, 20), 0);
    QCOMPARE(value(diamond, 28, 28), 255);
    const int edge = value(diamond, 50, 14);
    QVERIFY(edge > 0 && edge < 255);

    const LayerTransform wide{.origin = {0, 0}, .size = {200, 100}};
    const LayerTransform lowerHalf{.origin = {0, 50}, .size = {200, 50}};
    QImage leftBlack = gray(4, 2, 255);
    for (int y = 0; y < 2; ++y)
        leftBlack.scanLine(y)[0] = leftBlack.scanLine(y)[1] = 0;
    const QImage oblong = LayerMask::placed(100, 50, wide, lowerHalf, 4, 2, 1, [&](QPainter &context) {
        context.setRenderHint(QPainter::SmoothPixmapTransform, false);
        context.drawImage(QRectF(0, 0, 4, 2), leftBlack);
    });
    QCOMPARE(oblong.size(), QSize(100, 50));
    QCOMPARE(value(oblong, 25, 12), 255);
    QCOMPARE(value(oblong, 75, 12), 255);
    QCOMPARE(value(oblong, 25, 37), 0);
    QCOMPARE(value(oblong, 48, 49), 0);
    QCOMPARE(value(oblong, 52, 26), 255);
    QCOMPARE(value(oblong, 99, 49), 255);

    QImage ramp = gray(2, 1, 0);
    ramp.scanLine(0)[1] = 255;
    const LayerTransform whole{.origin = {0, 0}, .size = {100, 100}};
    const QImage stretched = LayerMask::placed(100, 100, layer, whole, 2, 1, 1, [&](QPainter &context) {
        context.drawImage(QRectF(0, 0, 2, 1), ramp);
    });
    QCOMPARE(value(stretched, 2, 50), 0);
    QCOMPARE(value(stretched, 97, 50), 255);
    QVERIFY(value(stretched, 50, 50) > 60 && value(stretched, 50, 50) < 195);
}

void LayerMaskTests::drawSmoothReplacesAndInterpolates()
{
    QImage surface = gray(8, 2, 200);
    QPainter painter(&surface);
    QImage ramp = gray(2, 1, 0);
    ramp.scanLine(0)[1] = 255;
    LayerMask::drawSmooth(ramp, QRectF(0, 0, 8, 1), painter);
    QCOMPARE(painter.compositionMode(), QPainter::CompositionMode_SourceOver);
    QVERIFY(!painter.testRenderHint(QPainter::SmoothPixmapTransform));
    painter.end();
    QCOMPARE(value(surface, 0, 0), 0);
    QCOMPARE(value(surface, 7, 0), 255);
    int between = 0;
    for (int x = 1; x < 8; ++x) {
        QVERIFY(value(surface, x, 0) >= value(surface, x - 1, 0));
        between += value(surface, x, 0) > 0 && value(surface, x, 0) < 255;
    }
    QVERIFY(between >= 2);
    QCOMPARE(value(surface, 3, 1), 200);
}

void LayerMaskTests::maskTransformPrefersThePlacement()
{
    ImageLayer layer("Layer 1", QSizeF(100, 50));
    QCOMPARE(layer.maskTransform(), layer.transform);
    layer.mask = wideMask();
    QCOMPARE(layer.maskTransform(), layer.transform);
    layer.mask->placement = LayerTransform{.origin = {5, 6}, .size = {7, 8}};
    QCOMPARE(layer.maskTransform(), *layer.mask->placement);
}

void LayerMaskTests::layersCompareAndRecordTheirMasks()
{
    ImageLayer layer("Layer 1", QSizeF(100, 50));
    layer.id = QUuid("e621e1f8-c36c-495a-93fc-0c247a3e6e5f");
    const ProjectLayerRecord bare = layer.hierarchyRecord();
    QCOMPARE(bare.maskFile, std::nullopt);
    QCOMPARE(bare.maskEnabled, std::nullopt);
    QCOMPARE(bare.maskPlacement, std::nullopt);
    QCOMPARE(bare.maskLinked, std::nullopt);

    ImageLayer masked = layer;
    masked.mask = wideMask();
    QVERIFY(!(masked == layer));
    QCOMPARE(masked, ImageLayer(masked));
    ImageLayer disabled = masked;
    disabled.mask->isEnabled = false;
    QVERIFY(!(disabled == masked));
    const ProjectLayerRecord hidden = disabled.hierarchyRecord();
    QCOMPARE(hidden.maskEnabled, std::optional<bool>(false));
    QCOMPARE(hidden.maskLinked, std::optional<bool>(true));

    const ProjectLayerRecord record = masked.hierarchyRecord();
    QCOMPARE(record.maskFile, std::optional<QString>("E621E1F8-C36C-495A-93FC-0C247A3E6E5F.mask.png"));
    QCOMPARE(record.maskEnabled, std::optional<bool>(true));
    QCOMPARE(record.maskPlacement, std::nullopt);
    QCOMPARE(record.maskLinked, std::optional<bool>(true));
    ImageLayer unlinked = masked;
    unlinked.mask->isLinked = false;
    unlinked.mask->placement = LayerTransform{.origin = {5, 6}, .size = {7, 8}};
    const ProjectLayerRecord moved = unlinked.hierarchyRecord();
    QCOMPARE(moved.maskEnabled, std::optional<bool>(true));
    QCOMPARE(moved.maskLinked, std::optional<bool>(false));
    QCOMPARE(moved.maskPlacement, unlinked.mask->placement);
}

void LayerMaskTests::historyCountsMaskPixels()
{
    DocumentHistory history;
    std::optional<CanvasDocument> document = CanvasDocument(40, 20);
    const ImportedImage pixels(BrushRaster::context(40, 20, false), QImage(), "Photo");
    document->layers.push_back(ImageLayer(pixels, QPointF(0, 0)));
    document->layers[0].mask = LayerMask(LayerMask::assetFrom(gray(200, 100, 255)));
    const qint64 maskBytes = document->layers[0].mask->asset.byteCount()
        + document->layers[0].mask->asset.thumbnail.sizeInBytes();
    QCOMPARE(maskBytes, qint64(200 * 100 + 96 * 48));
    history.begin("Disable Layer Mask", document, std::nullopt);
    document->layers[0].mask->isEnabled = false;
    history.end(document, std::nullopt);
    QCOMPARE(history.undoCount(), 1);
    QCOMPARE(history.retainedBytes(document), 0);
    history.begin("Delete Layer Mask", document, std::nullopt);
    document->layers[0].mask.reset();
    history.end(document, std::nullopt);
    QCOMPARE(history.retainedBytes(document), maskBytes);
}

QTEST_APPLESS_MAIN(LayerMaskTests)
#include "LayerMaskTests.moc"
