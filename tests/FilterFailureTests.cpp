#include "AddressSpaceLimit.h"
#include "Document/Filters.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include <QPainter>
#include <QtTest>
#include <malloc.h>

// The filters' pixels when memory runs out.
class FilterFailureTests : public QObject {
    Q_OBJECT
private slots:
    void theBlursFailAsAContextWhenTheirRowsFindNoMemory();
    void aTrimThatFindsNoMemoryIsARenderError();
};

void FilterFailureTests::theBlursFailAsAContextWhenTheirRowsFindNoMemory()
{
    // One pixel wide: its row list asks as its pixels.
    QImage tall(1, 16'000'000, QImage::Format_RGBA8888_Premultiplied);
    tall.fill(Qt::red);
    malloc_trim(0);
    {
        // The 64 MB result fits; the rows do not.
        const AddressSpaceLimit limit(80ll * 1024 * 1024);
        QVERIFY_THROWS_EXCEPTION(ExportError, PixelAdjust::motionBlur(tall, 1, 0));
    }
    malloc_trim(0);
    {
        // Result and 256 MB of sums fit; rows do not.
        const AddressSpaceLimit limit(350ll * 1024 * 1024);
        QVERIFY_THROWS_EXCEPTION(ExportError, PixelAdjust::gaussianBlur(tall, 1, false));
    }
}

void FilterFailureTests::aTrimThatFindsNoMemoryIsARenderError()
{
    // Opaque but for its border: the crop copies 96 MB.
    QImage framed(6000, 4000, QImage::Format_RGBA8888_Premultiplied);
    framed.fill(Qt::transparent);
    QPainter(&framed).fillRect(QRect(1, 1, 5998, 3998), Qt::red);
    malloc_trim(0);
    const AddressSpaceLimit limit(16ll * 1024 * 1024);
    QVERIFY_THROWS_EXCEPTION(ExportError, PixelFilter::trimmed(framed, LayerTransform{.origin = {0, 0}, .size = {6000, 4000}}));
}

QTEST_GUILESS_MAIN(FilterFailureTests)
#include "FilterFailureTests.moc"
