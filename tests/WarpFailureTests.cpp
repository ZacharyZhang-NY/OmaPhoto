#include "AddressSpaceLimit.h"
#include "BrushFixtures.h"
#include "Document/SmudgeLiquify.h"
#include "IO/ImageExporter.h"
#include <QtTest>
#include <malloc.h>

// The Smear's buffers, starved of memory mid-stroke.
namespace {
WarpStroke warp(const QImage &image, BlurToolMode mode, const BrushSettings &settings)
{
    const ImageLayer layer(ImportedImage(image, image, QStringLiteral("Picture")), QPointF(0, 0));
    return WarpStroke(layer, image, layer.transform, QSizeF(image.size()), mode, settings);
}

QImage stripes(int side)
{
    QImage image = BrushRaster::context(side, side, false);
    for (int x = 0; x < side; ++x)
        QPainter(&image).fillRect(x, 0, 1, side, x % 2 ? Qt::red : Qt::blue);
    return image;
}
}

class WarpFailureTests : public QObject {
    Q_OBJECT
private slots:
    // Large asks map memory, so the address limit counts them.
    void initTestCase() { mallopt(M_MMAP_THRESHOLD, 64 * 1024); }
    void aLiquifyDabWithoutScratchIsSkipped();
    void smudgeWithoutRoomToCarryIsARenderError();
    void liquifyWithoutItsOffsetsIsARenderError();
};

void WarpFailureTests::aLiquifyDabWithoutScratchIsSkipped()
{
    const QImage image = stripes(2400);
    WarpStroke stroke = warp(image, BlurToolMode::liquify, brush(2000, 0.5, 0, 0, 0, 1));
    stroke.append(QPointF(1200, 1200));
    {
        // The dab's scratch, about 33 MB, cannot be had.
        const AddressSpaceLimit limit(8ll * 1024 * 1024);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("a Liquify dab finds no memory"));
        stroke.append(QPointF(1250, 1200));
    }
    QCOMPARE(stroke.image(), image);
    // With memory back the next dab pushes.
    stroke.append(QPointF(1300, 1200));
    QVERIFY(stroke.image() != image);
}

void WarpFailureTests::smudgeWithoutRoomToCarryIsARenderError()
{
    const QImage image = stripes(2400);
    WarpStroke stroke = warp(image, BlurToolMode::smudge, brush(2000, 0.5, 0, 0, 0, 1));
    {
        // The carried colour, about 64 MB, cannot be had.
        const AddressSpaceLimit limit(8ll * 1024 * 1024);
        try {
            stroke.append(QPointF(1200, 1200));
            QFAIL("the carried colour was had");
        } catch (const ExportError &error) {
            QCOMPARE(error.kind, ExportError::Kind::render);
        }
    }
    QCOMPARE(stroke.image(), image);
}

void WarpFailureTests::liquifyWithoutItsOffsetsIsARenderError()
{
    // Room for two 36 MB copies, not the offsets.
    const QImage image = stripes(3000);
    {
        const AddressSpaceLimit limit(90ll * 1024 * 1024);
        try {
            warp(image, BlurToolMode::liquify, brush(100, 0.5, 0, 0, 0, 1));
            QFAIL("the offsets were had");
        } catch (const ExportError &error) {
            QCOMPARE(error.kind, ExportError::Kind::render);
        }
    }
    // Smudge needs neither.
    {
        const AddressSpaceLimit limit(52ll * 1024 * 1024);
        QCOMPARE(warp(image, BlurToolMode::smudge, brush(100, 0.5, 0, 0, 0, 1)).image(), image);
    }
}

QTEST_GUILESS_MAIN(WarpFailureTests)
#include "WarpFailureTests.moc"
