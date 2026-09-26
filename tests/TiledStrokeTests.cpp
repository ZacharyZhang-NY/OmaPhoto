#include "Rendering/TiledLayerRenderer.h"
#include "Document/LayerMask.h"
#include "RenderFixtures.h"
#include <QtTest>

namespace {
QImage grayNoise(int width, int height, quint32 seed)
{
    QImage image = BrushRaster::context(width, height, true);
    quint32 state = seed;
    for (int y = 0; y < height; ++y) {
        uchar *row = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            state = state * 1'664'525u + 1'013'904'223u;
            row[x] = uchar(state >> 24);
        }
    }
    return image;
}

QImage maskComposite(const QImage &base, const QRectF &at, const std::vector<BrushPatch> &patches, QSize size)
{
    QImage result = gray(size.width(), size.height(), 255);
    QPainter painter(&result);
    BrushRaster::draw(base, at, painter);
    for (const BrushPatch &patch : patches)
        BrushRaster::draw(patch.image, patch.rect, painter);
    return result;
}

// A square surface behind a painter scaled like dense displays.
QImage scaled(int side, double device, const std::function<void(QPainter &)> &draw)
{
    return render(int(std::ceil(side * device)), [&](QPainter &painter) {
        painter.scale(device, device);
        draw(painter);
    });
}

ImportedImage asset(const QImage &image)
{
    return ImportedImage(image, image, "Mask");
}
}

class TiledStrokeTests : public QObject {
    Q_OBJECT
private slots:
    void init();
    void strokesDrawLikeOneImage_data();
    void strokesDrawLikeOneImage();
    void maskStrokesDrawLikeOneMask_data();
    void maskStrokesDrawLikeOneMask();
    void paintingAtTheLayersEdgeDoesNotChangeIt_data();
    void paintingAtTheLayersEdgeDoesNotChangeIt();
    void translucentStrokesDrawLikeOneImage_data();
    void translucentStrokesDrawLikeOneImage();
    void paintPastTheOldBoundsIsRevealed_data();
    void paintPastTheOldBoundsIsRevealed();
    void aRasterAtAnOffsetKeepsItsPieces();
    void aLiveStrokeMatchesItsCommit_data();
    void aLiveStrokeMatchesItsCommit();
    void maskStrokesOverNoMaskAndOverAPaintedMask();
    void aMaskStrokeTakesNoSecondMask();
    void strokesApplyOpacityBlendModeAndMaskOnce();
};

void TiledStrokeTests::init()
{
    QTest::failOnWarning(QRegularExpression("saved states|Unbalanced save/restore"));
}

void TiledStrokeTests::strokesDrawLikeOneImage_data()
{
    QTest::addColumn<double>("scale");
    QTest::addColumn<double>("rotation");
    QTest::addColumn<LayerSampling>("sampling");
    QTest::addColumn<double>("device");
    QTest::newRow("0.2x") << 0.2 << 0.0 << LayerSampling::high << 1.0;
    QTest::newRow("0.7x") << 0.7 << 0.0 << LayerSampling::high << 1.0;
    QTest::newRow("0.3x turned") << 0.3 << 25.0 << LayerSampling::high << 1.0;
    // Nearest never halves: tiles and image pick the same texels.
    QTest::newRow("0.25x nearest") << 0.25 << 0.0 << LayerSampling::nearest << 1.0;
    // A 2x painter shows 0.35x at 0.7: no halving.
    QTest::newRow("0.35x on a 2x painter") << 0.35 << 0.0 << LayerSampling::high << 2.0;
}

void TiledStrokeTests::strokesDrawLikeOneImage()
{
    QFETCH(double, scale);
    QFETCH(double, rotation);
    QFETCH(LayerSampling, sampling);
    QFETCH(double, device);
    const QImage base = noise(1600, 1000, 7);
    const std::vector<BrushPatch> committed{{QRectF(1100, 400, 256, 256), noise(256, 256, 99)}};
    const auto raster = painted(base, committed);
    const LayerTransform transform{.origin = {0, 0}, .size = {1600, 1000}, .rotation = rotation, .sampling = sampling};
    const int tolerance = rotation == 0 ? 4 : 12;
    const int side = int(std::ceil(1700 * scale));
    const QPointF center(side / 2.0, side / 2.0);
    const QRectF grid(0, 0, 1600, 1000);
    // A live stroke over the committed layer, across a boundary.
    const std::vector<BrushPatch> stroke{{QRectF(900, 500, 256, 256), noise(256, 256, 5)}};
    const QImage finished = composite(composite(base, committed), stroke);
    const QImage expectedLive = scaled(side, device, [&](QPainter &painter) { LayerRenderer::draw(finished, transform, center, painter, {.scale = scale}); });
    const QImage live = scaled(side, device, [&](QPainter &painter) {
        TiledLayerRenderer::drawStroke(1600, 1000, grid, stroke, QImage(), raster, transform, center, painter, {.scale = scale});
    });
    QVERIFY2(largestDifference(expectedLive, live) <= tolerance, "a live stroke on a painted layer");
    QVERIFY(!raster->hasMaterializedPixels());

    // A first stroke on a plain image.
    const QImage first = composite(base, stroke);
    const QImage expectedFirst = scaled(side, device, [&](QPainter &painter) { LayerRenderer::draw(first, transform, center, painter, {.scale = scale}); });
    const QImage drawnFirst = scaled(side, device, [&](QPainter &painter) {
        TiledLayerRenderer::drawStroke(1600, 1000, grid, stroke, base, nullptr, transform, center, painter, {.scale = scale});
    });
    QVERIFY2(largestDifference(expectedFirst, drawnFirst) <= tolerance, "a first stroke on an image");
    // Far past the tolerance: a stale stroke cannot pass.
    QVERIFY(largestDifference(expectedFirst, expectedLive) > 50);
}

void TiledStrokeTests::maskStrokesDrawLikeOneMask_data()
{
    strokesDrawLikeOneImage_data();
}

void TiledStrokeTests::maskStrokesDrawLikeOneMask()
{
    QFETCH(double, scale);
    QFETCH(double, rotation);
    QFETCH(LayerSampling, sampling);
    QFETCH(double, device);
    const QImage layer = noise(1600, 1000, 11);
    const QImage oldMask = grayNoise(1600, 1000, 12);
    const std::vector<BrushPatch> tile{{QRectF(900, 500, 256, 256), grayNoise(256, 256, 13)}};
    const LayerTransform transform{.origin = {0, 0}, .size = {1600, 1000}, .rotation = rotation, .sampling = sampling};
    const int tolerance = rotation == 0 ? 4 : 12;
    const int side = int(std::ceil(1700 * scale));
    const QPointF center(side / 2.0, side / 2.0);
    const QRectF grid(0, 0, 1600, 1000);
    const QImage unmasked = scaled(side, device, [&](QPainter &painter) { LayerRenderer::draw(layer, transform, center, painter, {.scale = scale}); });
    const QImage finishedMask = maskComposite(oldMask, grid, tile, QSize(1600, 1000));
    const QImage expected = scaled(side, device, [&](QPainter &painter) {
        LayerRenderer::draw(layer, transform, center, painter, {.scale = scale, .mask = finishedMask});
    });
    const QImage live = scaled(side, device, [&](QPainter &painter) {
        TiledLayerRenderer::drawMaskStroke(1600, 1000, grid, tile, asset(oldMask), layer, nullptr, transform, center, painter, {.scale = scale});
    });
    QVERIFY2(largestDifference(expected, live, unmasked) <= tolerance, "a live mask stroke");
    const QImage stale = scaled(side, device, [&](QPainter &painter) {
        LayerRenderer::draw(layer, transform, center, painter, {.scale = scale, .mask = oldMask});
    });
    QVERIFY(largestDifference(expected, stale, unmasked) > 50);
}

void TiledStrokeTests::paintingAtTheLayersEdgeDoesNotChangeIt_data()
{
    QTest::addColumn<double>("zoom");
    QTest::newRow("10.749x") << 10.749;
    QTest::newRow("1x") << 1.0;
}

void TiledStrokeTests::paintingAtTheLayersEdgeDoesNotChangeIt()
{
    QFETCH(double, zoom);
    // Each patch holds the pixels beneath it: nothing may change.
    const QImage image = noise(3360, 1812, 11);
    const LayerTransform transform{.origin = {0, 0}, .size = {336, 181}, .sampling = LayerSampling::high};
    // The layer's corner sits 50 points into a retina view.
    const QPointF center(336 * zoom / 2 + 50, 181 * zoom / 2 + 50);
    const QRectF grid(0, 0, 3360, 1812);
    const auto retina = [](const std::function<void(QPainter &)> &draw) {
        return render(1200, [&](QPainter &painter) {
            painter.scale(2, 2);
            draw(painter);
        });
    };
    const LayerMask mask = LayerMask::solid(true);
    const QImage plain = retina([&](QPainter &painter) { LayerRenderer::draw(image, transform, center, painter, {.scale = zoom}); });
    const QImage masked = retina([&](QPainter &painter) {
        LayerRenderer::draw(image, transform, center, painter, {.scale = zoom, .mask = mask.asset.image()});
    });
    for (const QPoint &origin : {QPoint(0, 0), QPoint(256, 256)}) {
        const QRect rect(origin, QSize(256, 256));
        const QImage painting = retina([&](QPainter &painter) {
            TiledLayerRenderer::drawStroke(3360, 1812, grid, {BrushPatch{QRectF(rect), image.copy(rect)}}, image, nullptr, transform, center,
                                           painter, {.scale = zoom});
        });
        QVERIFY2(largestAnywhere(plain, painting) <= 2, "painting at the edge");
        // A mask stroke shows the layer through its painted mask.
        const QImage maskPainting = retina([&](QPainter &painter) {
            TiledLayerRenderer::drawMaskStroke(3360, 1812, grid, {BrushPatch{QRectF(rect), gray(256, 256, 255)}}, mask.asset, image, nullptr,
                                               transform, center, painter, {.scale = zoom});
        });
        QVERIFY2(largestAnywhere(masked, maskPainting) <= 2, "painting a mask at the edge");
    }
}

void TiledStrokeTests::translucentStrokesDrawLikeOneImage_data()
{
    QTest::addColumn<double>("scale");
    QTest::addColumn<double>("rotation");
    QTest::newRow("0.3x") << 0.3 << 0.0;
    QTest::newRow("0.7x turned") << 0.7 << 25.0;
}

void TiledStrokeTests::translucentStrokesDrawLikeOneImage()
{
    QFETCH(double, scale);
    QFETCH(double, rotation);
    // Translucent pixels show any double draw as a line.
    const QImage base = noise(1600, 1000, 21, 128);
    const std::vector<BrushPatch> tile{{QRectF(900, 500, 256, 256), noise(256, 256, 22, 128)}};
    const LayerTransform transform{.origin = {0, 0}, .size = {1600, 1000}, .rotation = rotation, .sampling = LayerSampling::high};
    const int side = int(std::ceil(1700 * scale));
    const QPointF center(side / 2.0, side / 2.0);
    const QImage inside = render(side, [&](QPainter &painter) { LayerRenderer::draw(noise(1600, 1000, 1), transform, center, painter, {.scale = scale}); });
    const QImage finished = composite(base, tile);
    const QImage expected = render(side, [&](QPainter &painter) { LayerRenderer::draw(finished, transform, center, painter, {.scale = scale}); });
    const QImage live = render(side, [&](QPainter &painter) {
        TiledLayerRenderer::drawStroke(1600, 1000, QRectF(0, 0, 1600, 1000), tile, base, nullptr, transform, center, painter, {.scale = scale});
    });
    QVERIFY2(largestDifference(expected, live, inside) <= (rotation == 0 ? 4 : 12), "a translucent live stroke");
}

void TiledStrokeTests::paintPastTheOldBoundsIsRevealed_data()
{
    QTest::addColumn<double>("scale");
    QTest::addColumn<double>("rotation");
    QTest::newRow("1x") << 1.0 << 0.0;
    QTest::newRow("0.5x") << 0.5 << 0.0;
    QTest::newRow("0.6x turned") << 0.6 << 25.0;
}

void TiledStrokeTests::paintPastTheOldBoundsIsRevealed()
{
    QFETCH(double, scale);
    QFETCH(double, rotation);
    // The old 400x300 layer sits inside a grid paint grew.
    const QImage old = noise(400, 300, 31);
    const QImage oldMask = grayNoise(400, 300, 32);
    const QRectF sourceRect(128, 96, 400, 300);
    // One tile straddles the old edge, one lies past it.
    const std::vector<BrushPatch> patches{{QRectF(450, 200, 128, 128), noise(128, 128, 33)}, {QRectF(16, 16, 64, 64), noise(64, 64, 34)}};
    QImage finished = BrushRaster::context(640, 480, false);
    {
        QPainter painter(&finished);
        BrushRaster::draw(old, sourceRect, painter);
        for (const BrushPatch &patch : patches)
            BrushRaster::draw(patch.image, patch.rect, painter);
    }
    const QImage finishedMask = maskComposite(oldMask, sourceRect, {}, QSize(640, 480));
    const LayerTransform transform{.origin = {0, 0}, .size = {640, 480}, .rotation = rotation, .sampling = LayerSampling::high};
    const int side = int(std::ceil(820 * scale));
    const QPointF center(side / 2.0, side / 2.0);
    const QImage expected = render(side, [&](QPainter &painter) {
        LayerRenderer::draw(finished, transform, center, painter, {.scale = scale, .mask = finishedMask});
    });
    const QImage live = render(side, [&](QPainter &painter) {
        TiledLayerRenderer::drawStroke(640, 480, sourceRect, patches, old, nullptr, transform, center, painter, {.scale = scale, .mask = oldMask});
    });
    // Turned, the old outline stays masked until commit, like macOS.
    QTransform toGrid;
    toGrid.translate(320, 240);
    toGrid.scale(1 / scale, 1 / scale);
    toGrid.rotate(-rotation);
    toGrid.translate(-center.x(), -center.y());
    const QRectF outline = sourceRect.adjusted(-2, -2, 2, 2), within = sourceRect.adjusted(2, 2, -2, -2);
    int largest = 0;
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            const QPointF grid = toGrid.map(QPointF(x + 0.5, y + 0.5));
            const bool pieces = QRectF(443, 193, 142, 142).contains(grid) || QRectF(9, 9, 78, 78).contains(grid);
            if (rotation != 0 && !pieces && outline.contains(grid) && !within.contains(grid))
                continue;
            for (int channel = 0; channel < 4; ++channel)
                largest = std::max(largest, std::abs(expected.constScanLine(y)[x * 4 + channel] - live.constScanLine(y)[x * 4 + channel]));
        }
    }
    QVERIFY2(largest <= (rotation == 0 ? 4 : 12), qPrintable(QString("paint past the old bounds differs by %1").arg(largest)));
}

void TiledStrokeTests::aRasterAtAnOffsetKeepsItsPieces()
{
    // Paint grew the grid: the layer now sits at (128,96).
    const QImage base = noise(600, 400, 41);
    const std::vector<BrushPatch> committed{{QRectF(300, 100, 256, 256), noise(256, 256, 42)}};
    const auto raster = painted(base, committed);
    const QRectF sourceRect(128, 96, 600, 400);
    const std::vector<BrushPatch> stroke{{QRectF(40, 40, 64, 64), noise(64, 64, 43)}, {QRectF(500, 300, 128, 128), noise(128, 128, 44)}};
    QImage finished = BrushRaster::context(800, 608, false);
    {
        QPainter painter(&finished);
        BrushRaster::draw(composite(base, committed), sourceRect, painter);
        for (const BrushPatch &patch : stroke)
            BrushRaster::draw(patch.image, patch.rect, painter);
    }
    const LayerTransform transform{.origin = {0, 0}, .size = {800, 608}, .sampling = LayerSampling::high};
    const QImage expected = render(440, [&](QPainter &painter) { LayerRenderer::draw(finished, transform, QPointF(220, 220), painter, {.scale = 0.5}); });
    const QImage live = render(440, [&](QPainter &painter) {
        TiledLayerRenderer::drawStroke(800, 608, sourceRect, stroke, QImage(), raster, transform, QPointF(220, 220), painter, {.scale = 0.5});
    });
    QVERIFY2(largestAnywhere(expected, live) <= 4, "a painted layer at an offset");
    QVERIFY(!raster->hasMaterializedPixels());
}

void TiledStrokeTests::aLiveStrokeMatchesItsCommit_data()
{
    QTest::addColumn<bool>("paintedBefore");
    QTest::newRow("over an image") << false;
    QTest::newRow("over a painted layer") << true;
}

void TiledStrokeTests::aLiveStrokeMatchesItsCommit()
{
    QFETCH(bool, paintedBefore);
    // Odd origins: a halving grid started elsewhere shifts every pixel.
    const QImage base = noise(600, 400, 61);
    const std::vector<BrushPatch> committed{{QRectF(300, 100, 256, 256), noise(256, 256, 62)}};
    // An earlier edit grew the painted layer one pixel left.
    const auto before = std::make_shared<const RasterSnapshot>(601, 400, base, QRectF(1, 0, 600, 400), committed, false, QPointF(1, 0));
    const ImportedImage source = paintedBefore ? ImportedImage(before, base, "Layer") : ImportedImage(base, base, "Layer");
    const QRectF sourceRect = paintedBefore ? QRectF(128, 97, 601, 400) : QRectF(129, 97, 600, 400);
    // One tile over old pixels, one past the grid's right.
    const std::vector<BrushPatch> stroke{{QRectF(500, 300, 128, 128), noise(128, 128, 63)}, {QRectF(800, 200, 96, 64), noise(96, 64, 64)}};
    const LayerTransform transform{.origin = {0, 0}, .size = {860, 600}, .sampling = LayerSampling::high};
    const QPointF center(190, 140);
    const QImage live = render(400, [&](QPainter &painter) {
        TiledLayerRenderer::drawStroke(860, 600, sourceRect, stroke, paintedBefore ? QImage() : base, paintedBefore ? before : nullptr, transform,
                                       center, painter, {.scale = 0.4});
    });
    // The commit keeps the grid: paint past it grows it.
    const auto commit = RasterSnapshot::replacing(source, sourceRect, stroke, QRectF(0, 0, 896, 600));
    QCOMPARE(commit->alignment, QPointF(129, 97));
    const LayerTransform grown{.origin = {0, 0}, .size = {896, 600}, .sampling = LayerSampling::high};
    const QImage kept = render(400, [&](QPainter &painter) {
        TiledLayerRenderer::drawRaster(commit, grown, center + QPointF(18 * 0.4, 0), painter, {.scale = 0.4});
    });
    // A commit's larger pieces redraw the old outline with spill.
    const QRectF outer = sourceRect.adjusted(-3, -3, 3, 3), inner = sourceRect.adjusted(3, 3, -3, -3);
    int largest = 0, onTheOutline = 0;
    for (int y = 0; y < 400; ++y) {
        for (int x = 0; x < 400; ++x) {
            const QPointF grid((x + 0.5 - 18) / 0.4, (y + 0.5 - 20) / 0.4);
            int &worst = outer.contains(grid) && !inner.contains(grid) ? onTheOutline : largest;
            for (int channel = 0; channel < 4; ++channel)
                worst = std::max(worst, std::abs(kept.constScanLine(y)[x * 4 + channel] - live.constScanLine(y)[x * 4 + channel]));
        }
    }
    QVERIFY2(largest <= 4, qPrintable(QString("the commit shifts pixels by %1").arg(largest)));
    QVERIFY2(onTheOutline <= 12, qPrintable(QString("the old outline changes by %1").arg(onTheOutline)));
    QVERIFY(largestAnywhere(kept, render(400, [](QPainter &) {})) > 100);
}

void TiledStrokeTests::maskStrokesOverNoMaskAndOverAPaintedMask()
{
    const QImage layer = noise(800, 600, 51);
    const std::vector<BrushPatch> tile{{QRectF(200, 200, 128, 128), grayNoise(128, 128, 52)}};
    const LayerTransform transform{.origin = {0, 0}, .size = {800, 600}, .sampling = LayerSampling::high};
    const QRectF grid(0, 0, 800, 600);
    const QPointF center(300, 300);
    const LayerRenderer::Options half{.scale = 0.7};
    const QImage unmasked = render(600, [&](QPainter &painter) { LayerRenderer::draw(layer, transform, center, painter, half); });
    const auto through = [&](const QImage &mask) {
        return render(600, [&](QPainter &painter) { LayerRenderer::draw(layer, transform, center, painter, {.scale = 0.7, .mask = mask}); });
    };
    // No old mask: everything shows but what the stroke hides.
    const QImage bare = render(600, [&](QPainter &painter) {
        TiledLayerRenderer::drawMaskStroke(800, 600, grid, tile, std::nullopt, layer, nullptr, transform, center, painter, half);
    });
    QVERIFY(largestDifference(through(maskComposite(gray(1, 1, 255), grid, tile, QSize(800, 600))), bare, unmasked) <= 4);
    QVERIFY(largestDifference(unmasked, bare, unmasked) > 50);

    // A solid old mask stretches over the layer and hides.
    const QImage hidden = render(600, [&](QPainter &painter) {
        TiledLayerRenderer::drawMaskStroke(800, 600, grid, tile, LayerMask::solid(false).asset, layer, nullptr, transform, center, painter, half);
    });
    QVERIFY(largestDifference(through(maskComposite(gray(1, 1, 0), grid, tile, QSize(800, 600))), hidden, unmasked) <= 4);

    // A painted layer shows through the stroke from its tiles.
    const std::vector<BrushPatch> committed{{QRectF(150, 100, 256, 256), noise(256, 256, 55)}};
    const auto raster = painted(layer, committed);
    const QImage tiled = render(600, [&](QPainter &painter) {
        TiledLayerRenderer::drawMaskStroke(800, 600, grid, tile, std::nullopt, QImage(), raster, transform, center, painter, half);
    });
    const QImage shown = render(600, [&](QPainter &painter) {
        LayerRenderer::draw(composite(layer, committed), transform, center, painter,
                            {.scale = 0.7, .mask = maskComposite(gray(1, 1, 255), grid, tile, QSize(800, 600))});
    });
    QVERIFY(largestDifference(shown, tiled, unmasked) <= 4);
    QVERIFY(!raster->hasMaterializedPixels());

    // Layer and mask at an odd offset: every pixel judged.
    const QRectF placed(65, 33, 800, 600);
    const std::vector<BrushPatch> edge{{QRectF(0, 200, 128, 128), grayNoise(128, 128, 57)}};
    const std::vector<BrushPatch> moved{{edge[0].rect.translated(65, 33), edge[0].image}};
    const LayerTransform wide{.origin = {0, 0}, .size = {928, 664}, .sampling = LayerSampling::high};
    for (const bool paintedLayer : {false, true}) {
        const QImage offset = render(600, [&](QPainter &painter) {
            TiledLayerRenderer::drawMaskStroke(928, 664, placed, moved, asset(grayNoise(800, 600, 56)), paintedLayer ? QImage() : layer,
                                               paintedLayer ? raster : nullptr, wide, center - QPointF(0.4, 0.4), painter, {.scale = 0.4});
        });
        const QImage expectedOffset = render(600, [&](QPainter &painter) {
            LayerRenderer::draw(paintedLayer ? composite(layer, committed) : layer, transform, center, painter,
                                {.scale = 0.4, .mask = maskComposite(grayNoise(800, 600, 56), grid, edge, QSize(800, 600))});
        });
        QVERIFY(largestAnywhere(expectedOffset, offset) <= 4);
    }

    // A mask matching one side only stretches over the layer.
    for (const QSize &strip : {QSize(800, 1), QSize(1, 600)}) {
        const QImage band = grayNoise(strip.width(), strip.height(), 58);
        const QImage stretched = render(600, [&](QPainter &painter) {
            TiledLayerRenderer::drawMaskStroke(800, 600, grid, tile, asset(band), layer, nullptr, transform, center, painter, half);
        });
        QVERIFY(largestDifference(through(maskComposite(band, grid, tile, QSize(800, 600))), stretched, unmasked) <= 4);
    }

    // A painted old mask stays in tiles.
    const QImage maskBase = grayNoise(800, 600, 53);
    const std::vector<BrushPatch> maskTiles{{QRectF(100, 150, 256, 256), grayNoise(256, 256, 54)}};
    const auto maskRaster = std::make_shared<const RasterSnapshot>(800, 600, maskBase, grid, maskTiles, true);
    const ImportedImage paintedMask(maskRaster, maskBase, "Mask");
    const QImage live = render(600, [&](QPainter &painter) {
        TiledLayerRenderer::drawMaskStroke(800, 600, grid, tile, paintedMask, layer, nullptr, transform, center, painter, half);
    });
    const QImage finishedMask = maskComposite(maskComposite(maskBase, grid, maskTiles, QSize(800, 600)), grid, tile, QSize(800, 600));
    QVERIFY(largestDifference(through(finishedMask), live, unmasked) <= 4);
    QVERIFY(largestDifference(through(maskBase), live, unmasked) > 50);
}

void TiledStrokeTests::aMaskStrokeTakesNoSecondMask()
{
    QImage surface = BrushRaster::context(64, 64, false);
    QPainter painter(&surface);
    const LayerTransform transform{.origin = {0, 0}, .size = {64, 64}, .sampling = LayerSampling::high};
    QVERIFY_EXCEPTION_THROWN(TiledLayerRenderer::drawMaskStroke(64, 64, QRectF(0, 0, 64, 64), {}, std::nullopt, noise(64, 64, 1), nullptr, transform,
                                                                QPointF(32, 32), painter, {.mask = gray(64, 64, 255)}),
                             std::logic_error);
}

void TiledStrokeTests::strokesApplyOpacityBlendModeAndMaskOnce()
{
    const QImage base = noise(700, 500, 3);
    // Translucent paint across four stroke cells.
    const std::vector<BrushPatch> stroke{{QRectF(200, 200, 128, 128), noise(128, 128, 11, 180)}};
    const QImage finished = composite(base, stroke);
    QImage mask = gray(700, 500, 255);
    for (int y = 0; y < 500; ++y) {
        for (int x = 0; x < 700; ++x)
            mask.scanLine(y)[x] = uchar((x + y) % 256);
    }
    const LayerTransform transform{.origin = {20, 10}, .size = {700, 500}, .sampling = LayerSampling::high};
    const LayerRenderer::Options options[] = {{.opacity = 0.6}, {.blendMode = LayerBlendMode::multiply}, {.mask = mask},
                                              {.opacity = 0.5, .blendMode = LayerBlendMode::luminosity, .mask = mask}};
    for (const LayerRenderer::Options &option : options) {
        QImage expected = noise(760, 540, 21), drawn = expected;
        QPainter first(&expected), second(&drawn);
        LayerRenderer::draw(finished, transform, transform.center(), first, option);
        TiledLayerRenderer::drawStroke(700, 500, QRectF(0, 0, 700, 500), stroke, base, nullptr, transform, transform.center(), second, option);
        first.end();
        second.end();
        QVERIFY2(largestAnywhere(expected, drawn) <= 2, "opacity, blend mode and mask apply once");
        QVERIFY(drawn != noise(760, 540, 21));
    }
}

QTEST_MAIN(TiledStrokeTests)
#include "TiledStrokeTests.moc"
