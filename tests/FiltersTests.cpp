#include "Document/Filters.h"
#include <QtTest>

// PixelFilter::trimmed: the crop and the placement that keeps it.
namespace {
QImage clearImage(int width, int height, QImage::Format format = QImage::Format_RGBA8888_Premultiplied)
{
    QImage image(width, height, format);
    image.fill(Qt::transparent);
    return image;
}

void paint(QImage &image, const QRect &area)
{
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x)
            image.setPixelColor(x, y, Qt::red);
    }
}
}

class FiltersTests : public QObject {
    Q_OBJECT
private slots:
    void trimmedCropsToThePixelsThatAreThere();
    void trimmedKeepsTheCropInPlaceUnderTurnAndFlip();
    void trimmedLeavesAFullOrEmptyImageAlone();
    void trimmedReadsAnyFormat();
};

void FiltersTests::trimmedCropsToThePixelsThatAreThere()
{
    QImage image = clearImage(8, 4);
    paint(image, QRect(2, 1, 4, 2));
    // Twice its size, nearest: every pixel is two units.
    const LayerTransform placed{.origin = {10, 20}, .size = {16, 8}, .sampling = LayerSampling::nearest};
    const PixelFilter::Trimmed trimmed = PixelFilter::trimmed(image, placed);
    QCOMPARE(trimmed.image.size(), QSize(4, 2));
    QCOMPARE(trimmed.image.pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(trimmed.image.pixelColor(3, 1), QColor(Qt::red));
    QCOMPARE(trimmed.transform.size, QSizeF(8, 4));
    QCOMPARE(trimmed.transform.origin, QPointF(14, 22));
    QCOMPARE(trimmed.transform.sampling, placed.sampling);
}

void FiltersTests::trimmedKeepsTheCropInPlaceUnderTurnAndFlip()
{
    QImage image = clearImage(8, 4);
    paint(image, QRect(0, 0, 2, 1));
    // A quarter turn: the crop's middle swings about the centre.
    LayerTransform turned{.origin = {0, 0}, .size = {16, 8}, .rotation = 90};
    PixelFilter::Trimmed trimmed = PixelFilter::trimmed(image, turned);
    QCOMPARE(trimmed.transform.size, QSizeF(4, 2));
    QCOMPARE(trimmed.transform.origin, QPointF(9, -3));
    QCOMPARE(trimmed.transform.rotation, 90.0);
    LayerTransform flipped{.origin = {0, 0}, .size = {16, 8}, .flipX = true};
    trimmed = PixelFilter::trimmed(image, flipped);
    QCOMPARE(trimmed.transform.origin, QPointF(12, 0));
    QVERIFY(trimmed.transform.flipX);
}

void FiltersTests::trimmedLeavesAFullOrEmptyImageAlone()
{
    const LayerTransform placed{.origin = {3, 4}, .size = {8, 4}};
    QImage full = clearImage(8, 4);
    paint(full, full.rect());
    PixelFilter::Trimmed trimmed = PixelFilter::trimmed(full, placed);
    QCOMPARE(trimmed.image.cacheKey(), full.cacheKey());
    QCOMPARE(trimmed.transform, placed);
    // Nothing there: nothing to cut, the image stays.
    const QImage empty = clearImage(8, 4);
    trimmed = PixelFilter::trimmed(empty, placed);
    QCOMPARE(trimmed.image.cacheKey(), empty.cacheKey());
    QCOMPARE(trimmed.transform, placed);
}

void FiltersTests::trimmedReadsAnyFormat()
{
    // Sixteen bits a channel: the alpha byte lies elsewhere.
    QImage image = clearImage(8, 4, QImage::Format_RGBA64);
    image.setPixelColor(5, 3, Qt::green);
    const PixelFilter::Trimmed trimmed = PixelFilter::trimmed(image, {.origin = {0, 0}, .size = {8, 4}});
    QCOMPARE(trimmed.image.size(), QSize(1, 1));
    QCOMPARE(trimmed.image.format(), QImage::Format_RGBA64);
    QCOMPARE(trimmed.image.pixelColor(0, 0), QColor(Qt::green));
    QCOMPARE(trimmed.transform.origin, QPointF(5, 3));
    QCOMPARE(trimmed.transform.size, QSizeF(1, 1));
}

QTEST_GUILESS_MAIN(FiltersTests)
#include "FiltersTests.moc"
