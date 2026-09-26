#include "Document/BrushStroke.h"
#include "Document/PixelAdjust.h"
#include "Document/PixelInvert.h"
#include <QtTest>

// The invert kernel and the selection blend behind it.
namespace {
SelectionClip leftHalf(int width, int height)
{
    QImage cover = BrushRaster::context(width / 2, height, true);
    cover.fill(Qt::white);
    return SelectionClip{QRectF(0, 0, width / 2, height), cover};
}
}

class PixelInvertTests : public QObject {
    Q_OBJECT
private slots:
    void invertKeepsAlphaAndSwapsMaskGray();
    void coverageFollowsTheLayersPixelGrid();
    void blendWeighsByCoverageAndRefusesMismatchedImages();
    void aSelectionLimitsTheInvert();
};

void PixelInvertTests::invertKeepsAlphaAndSwapsMaskGray()
{
    QImage image = BrushRaster::context(2, 1, false);
    image.setPixel(0, 0, qRgba(200, 40, 0, 200));
    image.setPixel(1, 0, qRgba(0, 0, 0, 0));
    const QImage inverted = PixelInvert::run({image, false, QTransform(), std::nullopt});
    QCOMPARE(inverted.format(), QImage::Format_RGBA8888_Premultiplied);
    const uchar *first = inverted.constScanLine(0);
    QCOMPARE(int(first[0]), 0);
    QCOMPARE(int(first[1]), 160);
    QCOMPARE(int(first[2]), 200);
    QCOMPARE(int(first[3]), 200);
    // Clear stays clear.
    QCOMPARE(int(first[7]), 0);
    QImage mask(3, 1, QImage::Format_Grayscale8);
    mask.scanLine(0)[0] = 0;
    mask.scanLine(0)[1] = 100;
    mask.scanLine(0)[2] = 255;
    const QImage swapped = PixelInvert::run({mask, true, QTransform(), std::nullopt});
    QCOMPARE(swapped.format(), QImage::Format_Grayscale8);
    QCOMPARE(int(swapped.constScanLine(0)[0]), 255);
    QCOMPARE(int(swapped.constScanLine(0)[1]), 155);
    QCOMPARE(int(swapped.constScanLine(0)[2]), 0);
    // Another format is read as premultiplied first.
    QImage plain(1, 1, QImage::Format_ARGB32);
    plain.setPixel(0, 0, qRgba(255, 0, 0, 128));
    const QImage halved = PixelInvert::run({plain, false, QTransform(), std::nullopt});
    const uchar *half = halved.constScanLine(0);
    QCOMPARE(int(half[0]), 0);
    QCOMPARE(int(half[1]), 128);
    QCOMPARE(int(half[3]), 128);
}

void PixelInvertTests::coverageFollowsTheLayersPixelGrid()
{
    // A 4×2 image at 1:1: the left half selected, exactly.
    const SelectionClip half = leftHalf(4, 2);
    const QImage exact = PixelAdjust::coverage(half, 4, 2, BrushRaster::pixelToDocument({.origin = {0, 0}, .size = {4, 2}}, 4, 2));
    QCOMPARE(int(exact.constScanLine(1)[1]), 255);
    QCOMPARE(int(exact.constScanLine(1)[2]), 0);
    // The image placed 2×: the selection lands at half size.
    const QImage placed = PixelAdjust::coverage(half, 4, 2, BrushRaster::pixelToDocument({.origin = {0, 0}, .size = {8, 4}}, 4, 2));
    QCOMPARE(int(placed.constScanLine(0)[0]), 255);
    QCOMPARE(int(placed.constScanLine(0)[1]), 0);
    // A black-and-white clip enlarged 2×: its boundary turns soft.
    QImage both = BrushRaster::context(10, 2, true);
    for (int y = 0; y < 2; ++y)
        std::fill_n(both.scanLine(y), 5, uchar(255));
    const SelectionClip inside{QRectF(0, 0, 10, 2), both};
    const QImage fine = PixelAdjust::coverage(inside, 20, 4, BrushRaster::pixelToDocument({.origin = {0, 0}, .size = {10, 2}}, 20, 4));
    QCOMPARE(int(fine.constScanLine(1)[2]), 255);
    QCOMPARE(int(fine.constScanLine(1)[15]), 0);
    for (const int column : {9, 10}) {
        const int boundary = fine.constScanLine(1)[column];
        QVERIFY2(boundary > 0 && boundary < 255, qPrintable(QString::number(boundary)));
    }
    // A clip with no coverage covers nothing.
    const QImage none = PixelAdjust::coverage({QRectF(), std::nullopt}, 4, 2, QTransform());
    QCOMPARE(int(none.constScanLine(0)[0]), 0);
}

void PixelInvertTests::blendWeighsByCoverageAndRefusesMismatchedImages()
{
    QImage top = BrushRaster::context(2, 1, false), bottom = BrushRaster::context(2, 1, false);
    top.fill(QColor::fromRgba(qRgba(255, 255, 255, 255)));
    bottom.fill(QColor::fromRgba(qRgba(100, 100, 100, 255)));
    QImage cover = BrushRaster::context(2, 1, true);
    cover.scanLine(0)[0] = 255;
    cover.scanLine(0)[1] = 64;
    const SelectionClip clip{QRectF(0, 0, 2, 1), cover};
    const QTransform grid = BrushRaster::pixelToDocument({.origin = {0, 0}, .size = {2, 1}}, 2, 1);
    const QImage blended = PixelAdjust::blend(top, bottom, clip, grid, false);
    QCOMPARE(int(blended.constScanLine(0)[0]), 255);
    // 64/255 of white over gray 100, rounded up from 138.9.
    QCOMPARE(int(blended.constScanLine(0)[4]), 139);
    QCOMPARE(int(blended.constScanLine(0)[7]), 255);
    QImage gray = BrushRaster::context(2, 1, true);
    QVERIFY_THROWS_EXCEPTION(std::logic_error, PixelAdjust::blend(top, gray, clip, grid, false));
    QVERIFY_THROWS_EXCEPTION(std::logic_error, PixelAdjust::blend(top, BrushRaster::context(3, 1, false), clip, grid, false));
}

void PixelInvertTests::aSelectionLimitsTheInvert()
{
    QImage image = BrushRaster::context(4, 2, false);
    image.fill(Qt::red);
    const QImage result = PixelInvert::run({image, false, BrushRaster::pixelToDocument({.origin = {0, 0}, .size = {4, 2}}, 4, 2), leftHalf(4, 2)});
    QCOMPARE(result.pixelColor(0, 0), QColor(Qt::cyan));
    QCOMPARE(result.pixelColor(3, 1), QColor(Qt::red));
}

QTEST_GUILESS_MAIN(PixelInvertTests)
#include "PixelInvertTests.moc"
