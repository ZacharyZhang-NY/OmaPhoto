#include "UI/CanvasThumbnail.h"
#include "Document/LayerMask.h"
#include "RenderFixtures.h"
#include <QtTest>

// Thumbnails take the canvas's shape; pixels sit where they are.
namespace {
// One pixel's RGBA bytes of a thumbnail, top row first.
QList<int> read(const QPixmap &thumbnail, int x, int y)
{
    const QImage image = thumbnail.toImage().convertToFormat(QImage::Format_RGBA8888);
    const uchar *pixel = image.constScanLine(y) + x * 4;
    return {pixel[0], pixel[1], pixel[2], pixel[3]};
}
}

class CanvasThumbnailTests : public QObject {
    Q_OBJECT
private slots:
    void thumbnailsTakeTheCanvasShape();
    void layerPixelsSitWhereTheyAreOnTheCanvas();
    void masksFillTheCanvasWithTheirEdgeTone();
};

void CanvasThumbnailTests::thumbnailsTakeTheCanvasShape()
{
    QCOMPARE(CanvasThumbnail::fittedSize(QSizeF(400, 200), 36), QSize(36, 18));
    QCOMPARE(CanvasThumbnail::fittedSize(QSizeF(300, 600), 36), QSize(18, 36));
    QCOMPARE(CanvasThumbnail::fittedSize(QSizeF(0, 0), 30), QSize(30, 30));
    QCOMPARE(CanvasThumbnail::fittedSize(QSizeF(3000, 7), 36), QSize(36, 1));
    // Rounded, as Swift rounds: 22.5 is 23, not 22.
    QCOMPARE(CanvasThumbnail::fittedSize(QSizeF(400, 250), 36), QSize(36, 23));
    QCOMPARE(CanvasThumbnail::fittedSize(QSizeF(std::nan(""), 7), 36), QSize(36, 36));
}

void CanvasThumbnailTests::layerPixelsSitWhereTheyAreOnTheCanvas()
{
    const QImage red = solid(100, 100, qRgba(255, 0, 0, 255));
    const QPixmap thumbnail = CanvasThumbnail::layer(red, LayerTransform{.origin = {0, 0}, .size = {100, 100}}, QSizeF(400, 200), 36);
    QCOMPARE(thumbnail.deviceIndependentSize(), QSizeF(36, 18));
    QCOMPARE(thumbnail.width(), 72);
    QCOMPARE(read(thumbnail, 4, 4), (QList<int>{255, 0, 0, 255}));
    QCOMPARE(read(thumbnail, 12, 12)[0], 255);
    // The rest shows the checkerboard; nothing is flipped.
    QVERIFY(read(thumbnail, 60, 30)[0] < 200);
    QVERIFY(read(thumbnail, 4, 30)[0] < 200);
    const QPixmap blank = CanvasThumbnail::layer(QImage(), LayerTransform{.origin = {0, 0}, .size = {400, 200}}, QSizeF(400, 200), 36);
    QCOMPARE(read(blank, 4, 4)[3], 255);
    // Two tones of gray, six points a tile.
    QCOMPARE(read(blank, 4, 4)[0], 82);
    QCOMPARE(read(blank, 16, 4)[0], 56);
    QCOMPARE(read(blank, 16, 16)[0], 82);
}

void CanvasThumbnailTests::masksFillTheCanvasWithTheirEdgeTone()
{
    const LayerTransform transform{.origin = {100, 50}, .size = {100, 100}};
    const QSizeF canvas(400, 200);
    const QImage hideAll = LayerMask::solid(false).asset.thumbnail;
    const QPixmap hidden = CanvasThumbnail::mask(hideAll, transform, canvas, 30);
    QVERIFY(read(hidden, 1, 1)[0] < 10);
    QVERIFY(read(hidden, 55, 25)[0] < 10);
    // White edges round a black middle: white around, black within.
    QImage framed(20, 20, QImage::Format_Grayscale8);
    framed.fill(255);
    for (int y = 5; y < 15; ++y) {
        for (int x = 5; x < 15; ++x)
            framed.setPixel(x, y, qRgb(0, 0, 0));
    }
    QCOMPARE(LayerMask::background(framed), 1.0);
    const QPixmap shown = CanvasThumbnail::mask(framed, transform, canvas, 30);
    QCOMPARE(shown.deviceIndependentSize(), QSizeF(30, 15));
    QVERIFY(read(shown, 2, 2)[0] > 245);
    QVERIFY(read(shown, 22, 15)[0] < 10);
    // A stroke reaching the edge: the rest still reads white.
    for (int y = 8; y < 12; ++y) {
        for (int x = 0; x < 20; ++x)
            framed.setPixel(x, y, qRgb(0, 0, 0));
    }
    QVERIFY(read(CanvasThumbnail::mask(framed, transform, canvas, 30), 2, 2)[0] > 245);
    // Mostly black at the edge: black beyond.
    QImage dark(4, 3, QImage::Format_Grayscale8);
    dark.fill(100);
    QVERIFY(read(CanvasThumbnail::mask(dark, transform, canvas, 30), 2, 2)[0] < 10);
}

QTEST_MAIN(CanvasThumbnailTests)
#include "CanvasThumbnailTests.moc"
