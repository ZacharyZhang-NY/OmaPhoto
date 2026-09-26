#include "AddressSpaceLimit.h"
#include "BrushFixtures.h"
#include "IO/ImageExporter.h"
#include <malloc.h>

// The raster's edges: failed copies, canvas fills and clears.
class BrushRasterTests : public QObject {
    Q_OBJECT
private slots:
    void aTileThatCannotBeCopiedIsARenderError();
    void clearPixelsStaysWithinTheSourceAndFillCoversTheCanvas();
};

namespace {
// Under a limit, every free chunk the copy could take.
struct Starved {
    std::vector<void *> blocks;
    explicit Starved(size_t bytes)
    {
        blocks.reserve(4096);
        while (void *block = malloc(bytes))
            blocks.push_back(block);
    }

    ~Starved()
    {
        for (void *block : blocks)
            free(block);
    }
};
}

void BrushRasterTests::aTileThatCannotBeCopiedIsARenderError()
{
    const auto paint = stroke(600, 300, red());
    paint->append(QPointF(20, 40));
    {
        const AddressSpaceLimit limit(64 * 1024);
        const Starved starved(256 * 256 * 4);
        QVERIFY_THROWS_EXCEPTION(ExportError, paint->append(QPointF(30, 40)));
    }
}

void BrushRasterTests::clearPixelsStaysWithinTheSourceAndFillCoversTheCanvas()
{
    QImage blue = BrushRaster::context(20, 20, false);
    blue.fill(Qt::blue);
    const ImageLayer layer(ImportedImage(blue, blue, "Blue"), QPointF(10, 10));
    BrushStroke clearing(layer, false, red(), QSizeF(600, 300));
    DocumentSelection selection;
    selection.path.addRect(QRectF(0, 0, 20, 300));
    clearing.selectionClip = selection.clip(QSizeF(600, 300));
    clearing.clearPixels();
    // Only the layer's tile; the canvas' rest is untouched.
    QCOMPARE(clearing.patches().size(), size_t(1));
    QCOMPARE(clearing.dirtyDocumentRect(), std::optional(QRectF(0, 0, 600, 300)));
    const BrushCommit::Output cleared = BrushCommit::render(clearing.commitInput());
    const QImage result = render(cleared.asset, clearing.transform(cleared.pixelBounds.translated(clearing.committedBounds().topLeft())), QSizeF(600, 300));
    QCOMPARE(alpha(result, 15, 15), 0);
    QCOMPARE(pixel(result, 25, 15), (std::vector<int>{0, 0, 255, 255}));
    // A fill covers the canvas, past the layer, selected.
    BrushStroke filling(layer, false, red(), QSizeF(600, 300));
    filling.selectionClip = selection.clip(QSizeF(600, 300));
    filling.fill(Qt::red);
    QCOMPARE(filling.patches().size(), size_t(2));
    const BrushCommit::Output filled = BrushCommit::render(filling.commitInput());
    const QImage whole = render(filled.asset, filling.transform(filled.pixelBounds.translated(filling.committedBounds().topLeft())), QSizeF(600, 300));
    QCOMPARE(pixel(whole, 5, 295), (std::vector<int>{255, 0, 0, 255}));
    QCOMPARE(pixel(whole, 15, 15), (std::vector<int>{255, 0, 0, 255}));
    QCOMPARE(pixel(whole, 25, 15), (std::vector<int>{0, 0, 255, 255}));
    QCOMPARE(alpha(whole, 45, 35), 0);
    // A mask never erases: the fill refuses the mode outright.
    QImage gray = BrushRaster::context(4, 4, true);
    QVERIFY_THROWS_EXCEPTION(std::logic_error, BrushRaster::fill(Qt::white, gray, QRectF(0, 0, 4, 4), 1, gray, QPainter::CompositionMode_DestinationOut));
    QVERIFY_THROWS_EXCEPTION(std::logic_error, BrushRaster::fill(Qt::white, gray, QRectF(0, 0, 4, 4), 1, gray, QPainter::CompositionMode_Multiply));
    // A clip is gray coverage the coverage's size, or refused.
    QImage colour = BrushRaster::context(4, 4, false);
    const QImage small = BrushRaster::context(3, 4, true);
    QVERIFY_THROWS_EXCEPTION(std::logic_error, BrushRaster::fill(Qt::white, gray, QRectF(0, 0, 4, 4), 1, colour, QPainter::CompositionMode_SourceOver, small));
    QVERIFY_THROWS_EXCEPTION(std::logic_error, BrushRaster::fill(Qt::white, gray, QRectF(0, 0, 4, 4), 1, colour, QPainter::CompositionMode_SourceOver, colour));
}

QTEST_GUILESS_MAIN(BrushRasterTests)
#include "BrushRasterTests.moc"
