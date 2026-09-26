#include "Rendering/TiledLayerRenderer.h"
#include "RenderFixtures.h"
#include <QtTest>

class TiledLayerTests : public QObject {
    Q_OBJECT
private slots:
    void init();
    void tiledLayersDrawLikeOneImage_data();
    void tiledLayersDrawLikeOneImage();
    void tilesAtTheLayersEdgeKeepItsOutline_data();
    void tilesAtTheLayersEdgeKeepItsOutline();
    void theOutlineOpensByOneDevicePixel_data();
    void theOutlineOpensByOneDevicePixel();
    void piecesOpenOnlyWhereTheirImageEnds_data();
    void piecesOpenOnlyWhereTheirImageEnds();
    void piecesMeetingOnAPixelCentreLeaveNoGap_data();
    void piecesMeetingOnAPixelCentreLeaveNoGap();
    void oddSizedLayersKeepTheirOverhang();
    void clippedPaintersDrawTheTilesTheyShow();
    void emptyLayersDrawNothing();
    void transparentTilesReplaceTheBase();
    void opacityBlendModeAndMaskApplyOnce();
};

void TiledLayerTests::init()
{
    QTest::failOnWarning(QRegularExpression("saved states|Unbalanced save/restore"));
}

void TiledLayerTests::tiledLayersDrawLikeOneImage_data()
{
    QTest::addColumn<double>("scale");
    QTest::addColumn<double>("rotation");
    QTest::addColumn<bool>("flipped");
    QTest::newRow("0.2x") << 0.2 << 0.0 << false;
    QTest::newRow("0.7x") << 0.7 << 0.0 << false;
    QTest::newRow("0.3x turned") << 0.3 << 25.0 << false;
    QTest::newRow("1x") << 1.0 << 0.0 << false;
    QTest::newRow("0.4x flipped") << 0.4 << 0.0 << true;
    QTest::newRow("a right angle") << 0.6 << 90.0 << false;
    QTest::newRow("a right angle, flipped") << 0.6 << 270.0 << true;
}

void TiledLayerTests::tiledLayersDrawLikeOneImage()
{
    QFETCH(double, scale);
    QFETCH(double, rotation);
    QFETCH(bool, flipped);
    const QImage base = noise(1600, 1000, 7);
    // The second tile crosses a piece boundary at x=1024.
    const std::vector<BrushPatch> committed{{QRectF(1100, 400, 256, 256), noise(256, 256, 99)},
                                            {QRectF(900, 100, 256, 256), noise(256, 256, 42)}};
    const auto raster = painted(base, committed);
    const LayerTransform transform{.origin = {0, 0}, .size = {1600, 1000}, .rotation = rotation,
                                   .flipX = flipped, .flipY = flipped, .sampling = LayerSampling::high};
    // Fixed-point texture stepping: 3 measured upright on noise.
    const int tolerance = rotation == 0 ? 4 : 12;
    const int side = int(std::ceil(1700 * scale));
    const QPointF center(side / 2.0, side / 2.0);
    const QImage finished = composite(base, committed);
    const QImage expected = render(side, [&](QPainter &painter) { LayerRenderer::draw(finished, transform, center, painter, {.scale = scale}); });
    const QImage drawn = render(side, [&](QPainter &painter) { TiledLayerRenderer::drawRaster(raster, transform, center, painter, {.scale = scale}); });
    const int difference = largestDifference(expected, drawn);
    QVERIFY2(difference <= tolerance, qPrintable(QString("a committed painted layer differs by %1").arg(difference)));
    QVERIFY(largestDifference(expected, render(side, [](QPainter &) {})) > 100);
    QVERIFY(!raster->hasMaterializedPixels());
}

void TiledLayerTests::tilesAtTheLayersEdgeKeepItsOutline_data()
{
    QTest::addColumn<double>("scale");
    QTest::addColumn<double>("rotation");
    QTest::addColumn<LayerSampling>("sampling");
    QTest::addColumn<bool>("transparentBase");
    QTest::addColumn<int>("tolerance");
    QTest::newRow("0.3x turned") << 0.3 << 25.0 << LayerSampling::high << false << 12;
    QTest::newRow("1x turned") << 1.0 << 25.0 << LayerSampling::smooth << false << 12;
    QTest::newRow("0.7x") << 0.7 << 0.0 << LayerSampling::high << false << 4;
    QTest::newRow("nearest turned") << 0.5 << 25.0 << LayerSampling::nearest << false << 12;
    QTest::newRow("turned past a right angle") << 0.6 << 205.0 << LayerSampling::high << false << 12;
    // Below a device pixel per step, outlines still need room.
    QTest::newRow("0.002x turned, tiles alone") << 0.002 << 25.0 << LayerSampling::high << true << 12;
    QTest::newRow("0.01x turned, tiles alone") << 0.01 << 25.0 << LayerSampling::high << true << 12;
    // Off the halving grid, pieces keep edge spill: 10 measured.
    QTest::newRow("0.05x tiles alone") << 0.05 << 0.0 << LayerSampling::high << true << 12;
    QTest::newRow("0.3x turned, tiles alone") << 0.3 << 25.0 << LayerSampling::high << true << 12;
}

void TiledLayerTests::tilesAtTheLayersEdgeKeepItsOutline()
{
    QFETCH(double, scale);
    QFETCH(double, rotation);
    QFETCH(LayerSampling, sampling);
    QFETCH(bool, transparentBase);
    QFETCH(int, tolerance);
    // Nearest picks whole texels: a ramp keeps one-texel slips small.
    const auto pixels = [&](int width, int height, int seed) {
        return sampling == LayerSampling::nearest ? ramp(width, height, seed) : noise(width, height, quint32(seed));
    };
    const QImage base = transparentBase ? BrushRaster::context(800, 600, false) : pixels(800, 600, 7);
    const std::vector<BrushPatch> corners{{QRectF(0, 0, 256, 256), pixels(256, 256, 99)},
                                          {QRectF(544, 344, 256, 256), pixels(256, 256, 42)}};
    const auto raster = painted(base, corners);
    const LayerTransform transform{.origin = {0, 0}, .size = {800, 600}, .rotation = rotation, .sampling = sampling};
    const int side = int(std::ceil(1100 * scale)) + 4;
    const QPointF center(side / 2.0, side / 2.0);
    const QImage finished = composite(base, corners);
    const QImage expected = render(side, [&](QPainter &painter) { LayerRenderer::draw(finished, transform, center, painter, {.scale = scale}); });
    const QImage drawn = render(side, [&](QPainter &painter) { TiledLayerRenderer::drawRaster(raster, transform, center, painter, {.scale = scale}); });
    // Every pixel counts here, the antialiased outline included.
    int largest = 0, slipped = 0;
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side * 4; ++x) {
            const int difference = std::abs(expected.constScanLine(y)[x] - drawn.constScanLine(y)[x]);
            largest = std::max(largest, difference);
            slipped += difference > tolerance;
        }
    }
    if (sampling == LayerSampling::nearest) {
        // Nearest slips single pixels along tile and layer edges.
        QVERIFY2(slipped < 150, qPrintable(QString("%1 samples slipped").arg(slipped)));
        return;
    }
    QVERIFY2(largest <= tolerance, qPrintable(QString("the outline differs by %1").arg(largest)));
}

void TiledLayerTests::theOutlineOpensByOneDevicePixel_data()
{
    QTest::addColumn<QSizeF>("size");
    QTest::addColumn<double>("rotation");
    QTest::addColumn<QPointF>("painterScale");
    QTest::addColumn<QPointF>("center");
    QTest::addColumn<QList<QPoint>>("outline");
    QTest::addColumn<int>("alpha");
    // Outline pixels left, above, right and below the layer.
    QTest::newRow("squashed") << QSizeF(801.2, 153.2) << 0.0 << QPointF(0.25, 0.25) << QPointF(420, 420)
                              << QList<QPoint>{{4, 90}, {30, 85}, {205, 115}, {170, 124}} << 35;
    QTest::newRow("narrowed") << QSizeF(201.2, 601.2) << 0.0 << QPointF(0.25, 0.25) << QPointF(420, 420)
                              << QList<QPoint>{{79, 60}, {88, 29}, {130, 150}, {122, 180}} << 34;
    QTest::newRow("unequal painter scales") << QSizeF(201.2, 630) << 0.0 << QPointF(0.25, 0.01) << QPointF(420, 500)
                                            << QList<QPoint>{{79, 3}, {88, 1}, {130, 6}, {122, 8}} << 34;
    QTest::newRow("a flat painter, turned") << QSizeF(201.2, 630) << 25.0 << QPointF(0.25, 0.01) << QPointF(420, 2000)
                                            << QList<QPoint>{} << 0;
    QTest::newRow("a thin painter, turned") << QSizeF(630, 201.2) << 25.0 << QPointF(0.01, 0.25) << QPointF(2000, 420)
                                            << QList<QPoint>{} << 0;
}

void TiledLayerTests::theOutlineOpensByOneDevicePixel()
{
    QFETCH(QSizeF, size);
    QFETCH(double, rotation);
    QFETCH(QPointF, painterScale);
    QFETCH(QPointF, center);
    QFETCH(QList<QPoint>, outline);
    QFETCH(int, alpha);
    // Both sides are whole halving steps: no edge spill.
    const QImage base = BrushRaster::context(832, 640, false);
    const std::vector<BrushPatch> corners{{QRectF(0, 0, 256, 256), ramp(256, 256, 99)},
                                          {QRectF(576, 384, 256, 256), ramp(256, 256, 42)}};
    const auto raster = painted(base, corners);
    // Upright, every edge lies 0.35 pixels past a pixel centre.
    const LayerTransform transform{.origin = {0, 0}, .size = size, .rotation = rotation, .sampling = LayerSampling::high};
    const auto scaled = [&](const std::function<void(QPainter &)> &draw) {
        return render(210, [&](QPainter &painter) {
            painter.scale(painterScale.x(), painterScale.y());
            draw(painter);
        });
    };
    const QImage finished = composite(base, corners);
    const QImage expected = scaled([&](QPainter &painter) { LayerRenderer::draw(finished, transform, center, painter); });
    const QImage drawn = scaled([&](QPainter &painter) { TiledLayerRenderer::drawRaster(raster, transform, center, painter); });
    for (const QPoint &point : outline) {
        QCOMPARE(qAlpha(expected.pixel(point)), alpha);
        QCOMPARE(qAlpha(drawn.pixel(point)), alpha);
    }
    int largest = 0;
    for (int y = 0; y < 210; ++y) {
        for (int x = 0; x < 210 * 4; ++x)
            largest = std::max(largest, std::abs(expected.constScanLine(y)[x] - drawn.constScanLine(y)[x]));
    }
    QVERIFY2(largest <= 4, qPrintable(QString("the outline differs by %1").arg(largest)));
}

void TiledLayerTests::piecesOpenOnlyWhereTheirImageEnds_data()
{
    QTest::addColumn<bool>("across");
    QTest::addColumn<bool>("patchAtTheEnd");
    QTest::newRow("an inner left edge") << true << true;
    QTest::newRow("an inner right edge") << true << false;
    QTest::newRow("an inner top edge") << false << true;
    QTest::newRow("an inner bottom edge") << false << false;
}

void TiledLayerTests::piecesOpenOnlyWhereTheirImageEnds()
{
    QFETCH(bool, across);
    QFETCH(bool, patchAtTheEnd);
    // One device pixel spans 1440 grid pixels, past a margin.
    const double length = 2304, shown = 1.6;
    const auto oriented = [&](double along, double aside) { return across ? QPointF(along, aside) : QPointF(aside, along); };
    const QPointF extent = oriented(length, 640);
    const QImage base = noise(int(extent.x()), int(extent.y()), 5);
    const std::vector<BrushPatch> patches{{QRectF(oriented(patchAtTheEnd ? length - 64 : 0, 288), QSizeF(64, 64)), noise(64, 64, 9)}};
    const auto raster = painted(base, patches);
    // The base owns a pixel centred 96 grid pixels inside.
    const double watched = patchAtTheEnd ? 5 : 7, inside = patchAtTheEnd ? 96 : length - 96;
    const double start = watched + 0.5 - inside * shown / length;
    const QPointF size = oriented(shown, 8), center = oriented(start + shown / 2, 6);
    const LayerTransform transform{.origin = {0, 0}, .size = {size.x(), size.y()}, .sampling = LayerSampling::high};
    const QImage finished = composite(base, patches);
    const QImage expected = render(12, [&](QPainter &painter) { LayerRenderer::draw(finished, transform, center, painter); });
    const QImage drawn = render(12, [&](QPainter &painter) { TiledLayerRenderer::drawRaster(raster, transform, center, painter); });
    QCOMPARE(qAlpha(expected.pixel(oriented(watched, 6).toPoint())), 143);
    int largest = 0;
    for (int y = 0; y < 12; ++y) {
        for (int x = 0; x < 12 * 4; ++x)
            largest = std::max(largest, std::abs(expected.constScanLine(y)[x] - drawn.constScanLine(y)[x]));
    }
    QVERIFY2(largest <= 4, qPrintable(QString("the far end differs by %1").arg(largest)));
}

void TiledLayerTests::piecesMeetingOnAPixelCentreLeaveNoGap_data()
{
    QTest::addColumn<bool>("flipX");
    QTest::addColumn<bool>("flipY");
    QTest::newRow("upright") << false << false;
    QTest::newRow("flipped across") << true << false;
    QTest::newRow("flipped down") << false << true;
}

void TiledLayerTests::piecesMeetingOnAPixelCentreLeaveNoGap()
{
    QFETCH(bool, flipX);
    QFETCH(bool, flipY);
    // Cells start at (1,1): halved, shared edges hit pixel centres.
    const QImage base = solid(1600, 1600, qRgba(255, 0, 0, 255));
    const auto raster = std::make_shared<const RasterSnapshot>(1600, 1600, base, QRectF(base.rect()),
        std::vector<BrushPatch>{BrushPatch{QRectF(900, 900, 256, 256), solid(256, 256, qRgba(0, 0, 255, 255))}}, false, QPointF(1, 1));
    const LayerTransform transform{.origin = {0, 0}, .size = {1600, 1600}, .flipX = flipX, .flipY = flipY, .sampling = LayerSampling::smooth};
    QImage surface = BrushRaster::context(800, 800, false);
    QPainter painter(&surface);
    TiledLayerRenderer::drawRaster(raster, transform, QPointF(400, 400), painter, {.scale = 0.5});
    painter.end();
    const int left = flipX ? 800 - 578 : 450, top = flipY ? 800 - 578 : 450;
    for (int y = top + 2; y < top + 126; ++y) {
        for (int x = left + 2; x < left + 126; ++x)
            QCOMPARE(surface.pixel(x, y), qRgba(0, 0, 255, 255));
    }
}

void TiledLayerTests::oddSizedLayersKeepTheirOverhang()
{
    // 605x405 at 0.4x: the last halved texel passes the grid.
    const QImage base = noise(605, 405, 71);
    const std::vector<BrushPatch> corner{{QRectF(349, 149, 256, 256), noise(256, 256, 72)}};
    const auto raster = painted(base, corner);
    const LayerTransform transform{.origin = {0, 0}, .size = {605, 405}, .sampling = LayerSampling::high};
    // The grid's far edges land on whole device pixels.
    const QPointF center(10 + 121, 10 + 81);
    const QImage expected = render(270, [&](QPainter &painter) { LayerRenderer::draw(composite(base, corner), transform, center, painter, {.scale = 0.4}); });
    const QImage drawn = render(270, [&](QPainter &painter) { TiledLayerRenderer::drawRaster(raster, transform, center, painter, {.scale = 0.4}); });
    // Right of and below the grid: half a texel.
    for (const QPoint &past : {QPoint(252, 100), QPoint(200, 172)}) {
        QCOMPARE(qAlpha(expected.pixel(past)), 51);
        QCOMPARE(qAlpha(drawn.pixel(past)), 51);
    }
    QVERIFY(largestAnywhere(expected, drawn) <= 4);
}

void TiledLayerTests::clippedPaintersDrawTheTilesTheyShow()
{
    // A stretched grid: columns and rows scale apart.
    const QImage base = noise(800, 600, 73);
    const std::vector<BrushPatch> committed{{QRectF(500, 300, 256, 256), noise(256, 256, 74)}};
    const auto raster = painted(base, committed);
    const LayerTransform transform{.origin = {0, 0}, .size = {400, 1200}, .sampling = LayerSampling::high};
    const QPointF center(210, 610);
    const QRect shown(270, 700, 100, 300);
    const auto clipped = [&](const std::function<void(QPainter &)> &draw) {
        QImage surface = BrushRaster::context(420, 1220, false);
        QPainter painter(&surface);
        painter.setClipRect(shown);
        draw(painter);
        return surface;
    };
    const QImage expected = clipped([&](QPainter &painter) { LayerRenderer::draw(composite(base, committed), transform, center, painter); });
    const QImage drawn = clipped([&](QPainter &painter) { TiledLayerRenderer::drawRaster(raster, transform, center, painter); });
    QVERIFY(largestAnywhere(expected, drawn) <= 4);
    QCOMPARE(drawn.pixel(269, 800), qRgba(0, 0, 0, 0));
    QVERIFY(qAlpha(drawn.pixel(300, 800)) == 255);
}

void TiledLayerTests::emptyLayersDrawNothing()
{
    const auto raster = painted(noise(64, 64, 75), {BrushPatch{QRectF(0, 0, 32, 32), noise(32, 32, 76)}});
    const QImage blank = BrushRaster::context(80, 80, false);
    for (const QSizeF &size : {QSizeF(0, 64), QSizeF(64, 0)}) {
        const LayerTransform transform{.origin = {8, 8}, .size = size, .sampling = LayerSampling::high};
        QCOMPARE(render(80, [&](QPainter &painter) { TiledLayerRenderer::drawRaster(raster, transform, QPointF(40, 40), painter); }), blank);
    }
    const LayerTransform transform{.origin = {8, 8}, .size = {64, 64}, .sampling = LayerSampling::high};
    for (const QSize &grid : {QSize(0, 64), QSize(64, 0)}) {
        QCOMPARE(render(80, [&](QPainter &painter) {
            TiledLayerRenderer::drawStroke(grid.width(), grid.height(), QRectF(0, 0, 64, 64), {}, noise(64, 64, 75), nullptr, transform, QPointF(40, 40), painter);
        }), blank);
    }
}

void TiledLayerTests::transparentTilesReplaceTheBase()
{
    const QImage base = solid(600, 400, qRgba(255, 0, 0, 255));
    const auto raster = painted(base, {BrushPatch{QRectF(256, 0, 256, 256), BrushRaster::context(256, 256, false)}});
    const LayerTransform transform{.origin = {0, 0}, .size = {600, 400}, .sampling = LayerSampling::nearest};
    QImage surface = solid(600, 400, qRgba(0, 0, 255, 255));
    QPainter painter(&surface);
    TiledLayerRenderer::drawRaster(raster, transform, transform.center(), painter);
    painter.end();
    QCOMPARE(surface.pixel(100, 100), qRgba(255, 0, 0, 255));
    QCOMPARE(surface.pixel(300, 100), qRgba(0, 0, 255, 255));
    QCOMPARE(surface.pixel(511, 255), qRgba(0, 0, 255, 255));
    QCOMPARE(surface.pixel(512, 100), qRgba(255, 0, 0, 255));
    QCOMPARE(surface.pixel(300, 256), qRgba(255, 0, 0, 255));
}

void TiledLayerTests::opacityBlendModeAndMaskApplyOnce()
{
    const QImage base = noise(700, 500, 3);
    const BrushPatch patch{QRectF(256, 256, 256, 244), noise(256, 244, 11, 180)};
    const auto raster = painted(base, {patch});
    const QImage finished = composite(base, {patch});
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
        TiledLayerRenderer::drawRaster(raster, transform, transform.center(), second, option);
        first.end();
        second.end();
        int largest = 0;
        for (int y = 12; y < 508; ++y) {
            for (int x = 22 * 4; x < 718 * 4; ++x)
                largest = std::max(largest, std::abs(expected.constScanLine(y)[x] - drawn.constScanLine(y)[x]));
        }
        QVERIFY2(largest <= 2, qPrintable(QString("differs by %1").arg(largest)));
        QVERIFY(drawn != noise(760, 540, 21));
    }
}

QTEST_MAIN(TiledLayerTests)
#include "TiledLayerTests.moc"
