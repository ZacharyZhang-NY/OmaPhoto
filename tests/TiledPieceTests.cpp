#include "Rendering/TiledLayerRenderer.h"
#include "AddressSpaceLimit.h"
#include "RenderFixtures.h"
#include <QtTest>

class TiledPieceTests : public QObject {
    Q_OBJECT
private slots:
    void alignedGrowsOutwardToTheStepGrid();
    void interiorsFollowChangesCellByCell();
    void interiorsSkipWhatCannotShow();
    void supportGrowsWithTheLevel();
    void aPieceHoldsItsInteriorAndAMargin();
    void aPieceStaysInsideItsBounds();
    void piecesThatCannotExistAreNil();
    void piecesThatCannotBeAllocatedAreLoggedAndNil();
    void committedPiecesAreBuiltOncePerLevel();
    void thePieceCacheDropsTheLeastRecentlyUsed();
};

void TiledPieceTests::alignedGrowsOutwardToTheStepGrid()
{
    QCOMPARE(TiledLayerRenderer::aligned(QRectF(3, 5, 10, 10), 8, QPointF(0, 0)), QRectF(0, 0, 16, 16));
    QCOMPARE(TiledLayerRenderer::aligned(QRectF(8, 16, 8, 8), 8, QPointF(0, 0)), QRectF(8, 16, 8, 8));
    QCOMPARE(TiledLayerRenderer::aligned(QRectF(3, 5, 10, 10), 8, QPointF(2, 1)), QRectF(2, 1, 16, 16));
    QCOMPARE(TiledLayerRenderer::aligned(QRectF(-3, -9, 2, 2), 4, QPointF(0, 0)), QRectF(-4, -12, 4, 8));
    QCOMPARE(TiledLayerRenderer::aligned(QRectF(3.5, 5.5, 1, 1), 1, QPointF(0, 0)), QRectF(3, 5, 2, 2));
}

void TiledPieceTests::interiorsFollowChangesCellByCell()
{
    using TiledLayerRenderer::interiors;
    QCOMPARE(interiors({QRectF(300, 300, 100, 100)}, 8, 1024, 1, QPointF(0, 0), std::nullopt),
             std::vector<QRectF>({QRectF(292, 292, 116, 116)}));
    QCOMPARE(interiors({QRectF(1000, 300, 100, 100)}, 8, 1024, 1, QPointF(0, 0), std::nullopt),
             std::vector<QRectF>({QRectF(992, 292, 32, 116), QRectF(1024, 292, 84, 116)}));
    QCOMPARE(interiors({QRectF(10, 10, 20, 20), QRectF(100, 50, 20, 20)}, 0, 256, 1, QPointF(0, 0), std::nullopt),
             std::vector<QRectF>({QRectF(10, 10, 110, 60)}));
    QCOMPARE(interiors({QRectF(10, 10, 20, 20)}, 3, 256, 8, QPointF(0, 0), std::nullopt),
             std::vector<QRectF>({QRectF(0, 0, 40, 40)}));
    QCOMPARE(interiors({QRectF(10, 10, 20, 20)}, 0, 256, 8, QPointF(5, 5), std::nullopt),
             std::vector<QRectF>({QRectF(5, 5, 32, 32)}));
    QCOMPARE(interiors({QRectF(250, 10, 12, 300)}, 0, 256, 1, QPointF(0, 0), std::nullopt),
             std::vector<QRectF>({QRectF(250, 10, 6, 246), QRectF(256, 10, 6, 246), QRectF(250, 256, 6, 54), QRectF(256, 256, 6, 54)}));
    QVERIFY(interiors({}, 8, 256, 1, QPointF(0, 0), std::nullopt).empty());
}

void TiledPieceTests::interiorsSkipWhatCannotShow()
{
    using TiledLayerRenderer::interiors;
    const std::vector<QRectF> changes{QRectF(10, 10, 20, 20), QRectF(600, 10, 20, 20)};
    QCOMPARE(interiors(changes, 4, 256, 1, QPointF(0, 0), QRectF(0, 0, 100, 100)), std::vector<QRectF>({QRectF(6, 6, 28, 28)}));
    QCOMPARE(interiors(changes, 4, 256, 1, QPointF(0, 0), QRectF(500, 0, 200, 100)), std::vector<QRectF>({QRectF(596, 6, 28, 28)}));
    QVERIFY(interiors(changes, 4, 256, 1, QPointF(0, 0), QRectF(200, 200, 50, 50)).empty());
    QCOMPARE(interiors(changes, 4, 1024, 1, QPointF(0, 0), QRectF(0, 0, 100, 100)), std::vector<QRectF>({QRectF(6, 6, 28, 28)}));
    QCOMPARE(interiors(changes, 4, 1024, 1, QPointF(0, 0), std::nullopt), std::vector<QRectF>({QRectF(6, 6, 618, 28)}));
    QCOMPARE(interiors({QRectF(250, 10, 12, 20)}, 0, 256, 1, QPointF(0, 0), QRectF(0, 0, 255, 100)),
             std::vector<QRectF>({QRectF(250, 10, 6, 20)}));
}

void TiledPieceTests::supportGrowsWithTheLevel()
{
    QCOMPARE(TiledLayerRenderer::support(0), 8.0);
    QCOMPARE(TiledLayerRenderer::support(1), 32.0);
    QCOMPARE(TiledLayerRenderer::support(3), 128.0);
    QCOMPARE(TiledLayerRenderer::committedCell, 1024.0);
    QCOMPARE(TiledLayerRenderer::strokeCell, 256.0);
}

void TiledPieceTests::aPieceHoldsItsInteriorAndAMargin()
{
    QRectF composed;
    const auto piece = TiledLayerRenderer::piece(QRectF(100, 200, 50, 60), 0, QPointF(0, 0), std::nullopt, false,
                                                 [&](QPainter &painter, const QRectF &region) {
                                                     composed = region;
                                                     painter.fillRect(QRectF(100, 200, 50, 60), QColor(0, 255, 0));
                                                 }).value();
    QCOMPARE(piece.interior, QRectF(100, 200, 50, 60));
    QCOMPARE(piece.region, QRectF(92, 192, 66, 76));
    QCOMPARE(composed, piece.region);
    QCOMPARE(piece.image.size(), QSize(66, 76));
    QCOMPARE(piece.image.format(), QImage::Format_RGBA8888_Premultiplied);
    QCOMPARE(piece.image.pixel(8, 8), qRgba(0, 255, 0, 255));
    QCOMPARE(piece.image.pixel(7, 8), qRgba(0, 0, 0, 0));
    QCOMPARE(piece.image.pixel(57, 67), qRgba(0, 255, 0, 255));
    QCOMPARE(piece.image.pixel(58, 67), qRgba(0, 0, 0, 0));

    const auto reduced = TiledLayerRenderer::piece(QRectF(128, 128, 64, 64), 2, QPointF(0, 0), std::nullopt, true,
                                                   [](QPainter &painter, const QRectF &region) { painter.fillRect(region, Qt::white); }).value();
    QCOMPARE(reduced.region, QRectF(64, 64, 192, 192));
    QCOMPARE(reduced.image.size(), QSize(48, 48));
    QCOMPARE(reduced.image.format(), QImage::Format_Grayscale8);
    QCOMPARE(reduced.image.constScanLine(24)[24], uchar(255));
    QCOMPARE(TiledLayerRenderer::piece(QRectF(130, 130, 10, 10), 2, QPointF(2, 1), std::nullopt, true,
                                       [](QPainter &, const QRectF &) {}).value().region, QRectF(66, 65, 140, 140));
}

void TiledPieceTests::aPieceStaysInsideItsBounds()
{
    const auto noop = [](QPainter &, const QRectF &) {};
    const auto piece = TiledLayerRenderer::piece(QRectF(0, 0, 40, 40), 0, QPointF(0, 0), QRectF(0, 0, 100, 30), false, noop).value();
    QCOMPARE(piece.region, QRectF(0, 0, 48, 30));
    QCOMPARE(piece.interior, QRectF(0, 0, 40, 30));
    QCOMPARE(piece.image.size(), QSize(48, 30));
    const auto halved = TiledLayerRenderer::piece(QRectF(0, 0, 40, 40), 1, QPointF(0, 0), QRectF(0, 0, 101, 31), false, noop).value();
    QCOMPARE(halved.region, QRectF(0, 0, 72, 32));
    QCOMPARE(halved.image.size(), QSize(36, 16));
}

void TiledPieceTests::piecesThatCannotExistAreNil()
{
    const auto noop = [](QPainter &, const QRectF &) {};
    QVERIFY(!TiledLayerRenderer::piece(QRectF(500, 500, 40, 40), 0, QPointF(0, 0), QRectF(0, 0, 100, 100), false, noop).has_value());
    QVERIFY(!TiledLayerRenderer::piece(QRectF(0, 0, 9000, 9000), 0, QPointF(0, 0), std::nullopt, false, noop).has_value());
    QVERIFY(TiledLayerRenderer::piece(QRectF(0, 0, 7980, 7980), 0, QPointF(0, 0), std::nullopt, true, noop).has_value());
}

void TiledPieceTests::piecesThatCannotBeAllocatedAreLoggedAndNil()
{
    const auto noop = [](QPainter &, const QRectF &) {};
    const QRectF interior(0, 0, 7900, 7900);
    const qint64 pieceBytes = qint64(7908) * 7908;
    std::optional<AddressSpaceLimit> limit(std::in_place, 16 * 1024 * 1024);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("a tile piece could not be allocated: .*"));
    const bool allocated = TiledLayerRenderer::piece(interior, 0, QPointF(0, 0), std::nullopt, true, noop).has_value();
    limit.reset();
    QVERIFY(!allocated);

    // Room for the piece, not for its halving as well.
    limit.emplace(pieceBytes + pieceBytes / 2);
    QTest::ignoreMessage(QtWarningMsg, "a tile piece could not be halved");
    const bool halved = TiledLayerRenderer::piece(QRectF(0, 0, 7844, 7844), 1, QPointF(0, 0), std::nullopt, true, noop).has_value();
    limit.reset();
    QVERIFY(!halved);
}

void TiledPieceTests::committedPiecesAreBuiltOncePerLevel()
{
    TiledPieceCache cache;
    const auto raster = painted(noise(1200, 300, 1), {BrushPatch{QRectF(900, 0, 256, 256), noise(256, 256, 2)}});
    const auto first = cache.pieces(raster, 0);
    QCOMPARE(int(first.size()), 2);
    QCOMPARE(first[0].interior, QRectF(892, 0, 132, 264));
    QCOMPARE(first[1].interior, QRectF(1024, 0, 140, 264));
    QCOMPARE(first[0].image.pixel(16, 0), noise(256, 256, 2).pixel(0, 0));
    QCOMPARE(cache.pieces(raster, 0)[0].image.cacheKey(), first[0].image.cacheKey());
    const auto halved = cache.pieces(raster, 1);
    QVERIFY(halved[0].image.cacheKey() != first[0].image.cacheKey());
    QCOMPARE(halved[0].interior, QRectF(868, 0, 156, 288));
    QCOMPARE(halved[0].region, QRectF(836, 0, 220, 300));
    QCOMPARE(halved[0].image.size(), QSize(110, 150));
    QVERIFY(!raster->hasMaterializedPixels());

    // A snapshot's halving grid starts at its alignment.
    const auto shifted = std::make_shared<const RasterSnapshot>(1200, 300, noise(1200, 300, 1), QRectF(0, 0, 1200, 300),
        std::vector<BrushPatch>{BrushPatch{QRectF(900, 0, 256, 256), noise(256, 256, 2)}}, false, QPointF(3, 5));
    const auto offGrid = cache.pieces(shifted, 2);
    QCOMPARE(int(offGrid.size()), 4);
    QCOMPARE(offGrid[0].interior, QRectF(835, -3, 192, 8));
    QCOMPARE(offGrid[0].region, QRectF(771, -3, 320, 72));
    QCOMPARE(offGrid[1].interior, QRectF(1027, -3, 176, 8));
    QCOMPARE(offGrid[2].interior, QRectF(835, 5, 192, 296));
    QCOMPARE(offGrid[2].region, QRectF(771, -3, 320, 304));
    QCOMPARE(offGrid[2].image.size(), QSize(80, 76));
    QCOMPARE(offGrid[3].interior, QRectF(1027, 5, 176, 296));
}

void TiledPieceTests::thePieceCacheDropsTheLeastRecentlyUsed()
{
    TiledPieceCache cache(3 * 132 * 132);
    const auto make = [&](quint32 seed) { return painted(noise(300, 300, seed), {BrushPatch{QRectF(100, 100, 100, 100), noise(100, 100, seed + 50)}}); };
    const auto a = make(1), b = make(2), c = make(3), d = make(4);
    const qint64 firstA = cache.pieces(a, 0)[0].image.cacheKey(), firstB = cache.pieces(b, 0)[0].image.cacheKey();
    cache.pieces(c, 0);
    QCOMPARE(cache.pieces(a, 0)[0].image.cacheKey(), firstA);
    cache.pieces(d, 0);
    QCOMPARE(cache.pieces(a, 0)[0].image.cacheKey(), firstA);
    QVERIFY(cache.pieces(b, 0)[0].image.cacheKey() != firstB);
    TiledPieceCache tiny(10);
    const qint64 only = tiny.pieces(a, 0)[0].image.cacheKey();
    QCOMPARE(tiny.pieces(a, 0)[0].image.cacheKey(), only);
}

QTEST_MAIN(TiledPieceTests)
#include "TiledPieceTests.moc"
