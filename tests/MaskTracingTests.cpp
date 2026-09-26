#include "Document/BrushStroke.h"
#include "Document/MaskTracing.h"
#include "AddressSpaceLimit.h"
#include <QtTest>

namespace {
// '#' marks dark mask pixels and opaque layer pixels.
QImage mask(const QStringList &rows, int dark = 0, int light = 255)
{
    QImage image = BrushRaster::context(rows[0].size(), rows.size(), true);
    for (int y = 0; y < rows.size(); ++y) {
        for (int x = 0; x < rows[y].size(); ++x)
            image.scanLine(y)[x] = rows[y][x] == '#' ? dark : light;
    }
    return image;
}

QImage layer(const QStringList &rows, int opaque = 255, int clear = 0)
{
    QImage image = BrushRaster::context(rows[0].size(), rows.size(), false);
    for (int y = 0; y < rows.size(); ++y) {
        for (int x = 0; x < rows[y].size(); ++x) {
            const int alpha = rows[y][x] == '#' ? opaque : clear;
            image.setPixel(x, y, qRgba(0, 0, 0, alpha));
        }
    }
    return image;
}

QStringList filled(const QPainterPath &path, int width, int height)
{
    QImage surface = BrushRaster::context(width, height, true);
    QPainter painter(&surface);
    painter.fillPath(path, Qt::white);
    painter.end();
    QStringList rows;
    for (int y = 0; y < height; ++y) {
        QString row;
        for (int x = 0; x < width; ++x)
            row += surface.constScanLine(y)[x] == 255 ? '#' : '.';
        rows << row;
    }
    return rows;
}

// The corners of a path with one closed subpath.
QList<QPointF> corners(const QPainterPath &path)
{
    QList<QPointF> result;
    for (int index = 0; index + 1 < path.elementCount(); ++index)
        result << QPointF(path.elementAt(index));
    return result;
}
}

class MaskTracingTests : public QObject {
    Q_OBJECT
private slots:
    void aSinglePixelBecomesAClockwiseSquare();
    void aBlockKeepsOnlyItsFourCorners();
    void anEllShapeHasSixCorners();
    void aRingTracesItsHoleTheOtherWay();
    void everyPatternFillsBackToItself_data();
    void everyPatternFillsBackToItself();
    void darkMeansBelowHalfGray();
    void opaqueMeansAtLeastHalfAlpha();
    void nothingToTraceIsNil();
    void otherPixelFormatsAreConverted();
    void transparentPixelsCountAsDarkOverTheClearedBitmap();
    void allocationFailureIsNil();
    void aDevicePixelRatioNeverShrinksTheTrace();
};

void MaskTracingTests::aSinglePixelBecomesAClockwiseSquare()
{
    const QPainterPath path = MaskTracing::darkPixels(mask({"....", "..#.", "...."})).value();
    QCOMPARE(path.fillRule(), Qt::WindingFill);
    QCOMPARE(corners(path), QList<QPointF>({QPointF(2, 1), QPointF(3, 1), QPointF(3, 2), QPointF(2, 2)}));
}

void MaskTracingTests::aBlockKeepsOnlyItsFourCorners()
{
    const QPainterPath path = MaskTracing::darkPixels(mask({".....", ".###.", ".###.", "....."})).value();
    QCOMPARE(corners(path), QList<QPointF>({QPointF(1, 1), QPointF(4, 1), QPointF(4, 3), QPointF(1, 3)}));
}

void MaskTracingTests::anEllShapeHasSixCorners()
{
    const QPainterPath path = MaskTracing::darkPixels(mask({"#..", "#..", "###"})).value();
    QCOMPARE(corners(path), QList<QPointF>({QPointF(0, 0), QPointF(1, 0), QPointF(1, 2), QPointF(3, 2),
                                            QPointF(3, 3), QPointF(0, 3)}));
}

void MaskTracingTests::aRingTracesItsHoleTheOtherWay()
{
    const QStringList ring{"#####", "#...#", "#...#", "#####"};
    const QPainterPath path = MaskTracing::darkPixels(mask(ring)).value();
    QCOMPARE(filled(path, 5, 4), ring);
    QVERIFY(path.contains(QPointF(0.5, 0.5)));
    QVERIFY(!path.contains(QPointF(2.5, 1.5)));
    int moves = 0;
    for (int index = 0; index < path.elementCount(); ++index)
        moves += path.elementAt(index).isMoveTo();
    QCOMPARE(moves, 2);
}

void MaskTracingTests::everyPatternFillsBackToItself_data()
{
    QTest::addColumn<QStringList>("rows");
    QTest::newRow("diagonal touch") << QStringList{"#..", ".#.", "..#"};
    QTest::newRow("checkerboard") << QStringList{"#.#.#", ".#.#.", "#.#.#"};
    QTest::newRow("padded width") << QStringList{"##..#", ".###.", "#...#"};
    QTest::newRow("nested") << QStringList{"#######", "#.....#", "#.###.#", "#.#.#.#", "#.###.#", "#.....#", "#######"};
    QTest::newRow("full") << QStringList{"###", "###"};
    QTest::newRow("comb") << QStringList{"#.#.#.#", "#.#.#.#", "#######"};
}

void MaskTracingTests::everyPatternFillsBackToItself()
{
    QFETCH(QStringList, rows);
    const int width = rows[0].size(), height = rows.size();
    QCOMPARE(filled(MaskTracing::darkPixels(mask(rows)).value(), width, height), rows);
    QCOMPARE(filled(MaskTracing::opaquePixels(layer(rows)).value(), width, height), rows);
}

void MaskTracingTests::darkMeansBelowHalfGray()
{
    QCOMPARE(filled(MaskTracing::darkPixels(mask({"#.", ".."}, 127, 128)).value(), 2, 2), QStringList({"#.", ".."}));
    QVERIFY(!MaskTracing::darkPixels(mask({"#.", ".."}, 128, 255)).has_value());
}

void MaskTracingTests::opaqueMeansAtLeastHalfAlpha()
{
    QCOMPARE(filled(MaskTracing::opaquePixels(layer({"#.", ".."}, 128, 127)).value(), 2, 2), QStringList({"#.", ".."}));
    QVERIFY(!MaskTracing::opaquePixels(layer({"#.", ".."}, 127, 0)).has_value());
}

void MaskTracingTests::nothingToTraceIsNil()
{
    QVERIFY(!MaskTracing::darkPixels(mask({"..", ".."})).has_value());
    QVERIFY(!MaskTracing::opaquePixels(layer({"..", ".."})).has_value());
    QVERIFY(!MaskTracing::darkPixels(QImage()).has_value());
    QVERIFY(!MaskTracing::opaquePixels(QImage()).has_value());
}

void MaskTracingTests::otherPixelFormatsAreConverted()
{
    const QStringList rows{"#..", ".##"};
    QCOMPARE(filled(MaskTracing::opaquePixels(layer(rows).convertToFormat(QImage::Format_ARGB32)).value(), 3, 2), rows);
    QCOMPARE(filled(MaskTracing::darkPixels(mask(rows).convertToFormat(QImage::Format_RGB32)).value(), 3, 2), rows);
}

void MaskTracingTests::transparentPixelsCountAsDarkOverTheClearedBitmap()
{
    QImage faint = BrushRaster::context(2, 1, false);
    faint.setPixel(0, 0, qRgba(16, 16, 16, 16));
    faint.setPixel(1, 0, qRgba(255, 255, 255, 255));
    QCOMPARE(filled(MaskTracing::darkPixels(faint).value(), 2, 1), QStringList({"#."}));
    QImage half = BrushRaster::context(2, 1, false);
    half.setPixel(0, 0, qRgba(127, 127, 127, 127));
    half.setPixel(1, 0, qRgba(128, 128, 128, 128));
    QCOMPARE(filled(MaskTracing::darkPixels(half).value(), 2, 1), QStringList({"#."}));
}

void MaskTracingTests::allocationFailureIsNil()
{
    const QImage large = BrushRaster::context(8192, 8192, true);
    std::optional<AddressSpaceLimit> limit(std::in_place, 16 * 1024 * 1024);
    const bool traced = MaskTracing::opaquePixels(large).has_value();
    limit.reset();
    QVERIFY(!traced);
}

void MaskTracingTests::aDevicePixelRatioNeverShrinksTheTrace()
{
    const QStringList rows{"####", "#..#"};
    QImage dense = layer(rows);
    dense.setDevicePixelRatio(2);
    QCOMPARE(filled(MaskTracing::opaquePixels(dense).value(), 4, 2), rows);
    QImage denseMask = mask(rows);
    denseMask.setDevicePixelRatio(2);
    QCOMPARE(filled(MaskTracing::darkPixels(denseMask).value(), 4, 2), rows);
}

QTEST_APPLESS_MAIN(MaskTracingTests)
#include "MaskTracingTests.moc"
