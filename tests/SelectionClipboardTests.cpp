#include "Document/BrushStroke.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore.h"
#include "SelectionFixtures.h"
#include "AddressSpaceLimit.h"
#include <QClipboard>
#include <QColorSpace>
#include <QGuiApplication>
#include <QMimeData>
#include <QRegularExpression>
#include <malloc.h>

// Copy, Copy Merged, Paste, Layer via Copy; the clipboard.
namespace {
// A 100×40 layer: red on the left, blue right.
std::unique_ptr<EditorSession> makeSession()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(100, 40, true);
    QImage image = BrushRaster::context(100, 40, false);
    QPainter painter(&image);
    painter.fillRect(QRect(0, 0, 50, 40), Qt::red);
    painter.fillRect(QRect(50, 0, 50, 40), Qt::blue);
    painter.end();
    session->insert(ImportedImage(image, image, "Halves"));
    session->deleteLayer(session->document().value().layers[0].id);
    return session;
}

void select(EditorSession &session, const QRectF &rect)
{
    session.applySelection(rectPath(rect), SelectionMode::replace, "Select");
}

// Within two of each channel, for rounding on the way.
bool near(const std::vector<int> &pixel, const std::vector<int> &expected)
{
    for (size_t i = 0; i < 4; ++i) {
        if (std::abs(pixel[i] - expected[i]) > 2)
            return false;
    }
    return true;
}

std::vector<int> pixel(const QImage &image, int x, int y)
{
    const QImage bytes = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    const uchar *at = bytes.constScanLine(y) + x * 4;
    return {at[0], at[1], at[2], at[3]};
}

// Renders one layer alone, to read its own pixels.
std::vector<int> layerPixel(const EditorSession &session, QUuid id, int x, int y)
{
    ProjectSnapshot solo = session.projectSnapshot().value();
    std::erase_if(solo.manifest.layers, [&](const ProjectLayerRecord &layer) { return layer.id != id; });
    return pixel(ImageExporter::render(solo).image, x, y);
}
}

class SelectionClipboardTests : public QObject {
    Q_OBJECT
private slots:
    void copyAndPastePutsPixelsOnANewLayerInPlace();
    void layerViaCopyCopiesTheSelectionOrDuplicatesTheLayer();
    void lassoShapedSelectionCopiesAndPastes();
    void copyMergedTakesEveryVisibleLayerNotJustTheActiveOne();
    void aCopyTakesTheLayersOwnPixelsAtTheirTransformedPlaces();
    void aMaskCopiesAsOpaqueGray();
    void aSoftSelectionEdgeClipsBothMaskPasses();
    void theCopyRegionRoundsOffFloatNoiseAndStopsAtTheCanvas();
    void anotherAppsImagePastesCentredAndUnstalesOurs();
    void copyingAndPastingAreRefusedWhereSwiftRefusesThem();
    void anImageTooLargeToCopyWarnsWithoutAnAlert();
    void cutLeavesAHoleAndPasteRestoresThePixels();
};

void SelectionClipboardTests::copyAndPastePutsPixelsOnANewLayerInPlace()
{
    const auto session = makeSession();
    const QUuid source = session->activeLayerID().value();
    // Straddles red and blue.
    select(*session, QRectF(40, 10, 20, 20));
    session->copySelection();
    const int count = session->history.undoCount();
    QVERIFY(session->canPaste());
    QVERIFY(QGuiApplication::clipboard()->mimeData()->hasFormat("image/png"));
    session->paste();
    QCOMPARE(session->history.undoCount(), count + 1);
    QCOMPARE(session->history.undoName(), QString("Paste"));
    const ImageLayer pasted = session->activeLayer().value();
    QVERIFY(pasted.id != source && pasted.name == "Layer 1" && !session->selection().has_value());
    QCOMPARE(indexOf(session->document().value().layers, pasted.id), indexOf(session->document().value().layers, source) + 1);
    QCOMPARE(pasted.transform.origin, QPointF(40, 10));
    QCOMPARE(pasted.size(), QSizeF(20, 20));
    QCOMPARE(layerPixel(*session, pasted.id, 45, 15), (std::vector<int>{255, 0, 0, 255}));
    QCOMPARE(layerPixel(*session, pasted.id, 55, 15), (std::vector<int>{0, 0, 255, 255}));
    QCOMPARE(layerPixel(*session, pasted.id, 30, 15)[3], 0);
    // The source is untouched.
    QCOMPARE(layerPixel(*session, source, 45, 15), (std::vector<int>{255, 0, 0, 255}));
    session->undo();
    QCOMPARE(session->document().value().layers.size(), size_t(1));
    // Pasted again: the next free names; a folder's child.
    session->paste();
    session->paste();
    QCOMPARE(session->activeLayer().value().name, QString("Layer 2"));
    session->addGroup();
    const QUuid folder = session->activeLayerID().value();
    session->paste();
    QCOMPARE(session->activeLayer().value().parentID, std::optional(folder));
    QCOMPARE(session->activeLayer().value().name, QString("Layer 3"));
}

void SelectionClipboardTests::layerViaCopyCopiesTheSelectionOrDuplicatesTheLayer()
{
    const auto session = makeSession();
    const ImageLayer source = session->activeLayer().value();
    select(*session, QRectF(60, 0, 10, 40));
    session->layerViaCopy();
    QCOMPARE(session->history.undoName(), QString("Layer via Copy"));
    QCOMPARE(session->document().value().layers.size(), size_t(2));
    QCOMPARE(session->activeLayer().value().size(), QSizeF(10, 40));
    QVERIFY(!session->selection().has_value());
    QCOMPARE(layerPixel(*session, session->activeLayerID().value(), 65, 20), (std::vector<int>{0, 0, 255, 255}));
    session->selectLayer(source.id);
    session->layerViaCopy();
    QCOMPARE(session->history.undoName(), QString("Duplicate Layer"));
    QCOMPARE(session->activeLayer().value().name, source.name + " copy");
    QVERIFY(session->activeLayer().value().asset.value().identity() == source.asset.value().identity());
    // An empty selection and a folder make no layer.
    const int count = session->history.undoCount();
    session->addGroup();
    session->layerViaCopy();
    session->selectLayer(source.id);
    select(*session, QRectF(0, 0, 10, 10));
    session->applySelection(rectPath(QRectF(0, 0, 100, 40)), SelectionMode::subtract, "Subtract");
    session->layerViaCopy();
    QCOMPARE(session->history.undoCount(), count + 3);
}

void SelectionClipboardTests::lassoShapedSelectionCopiesAndPastes()
{
    const auto session = makeSession();
    session->selectTool(NavigationTool::lasso);
    // A triangle over the red half.
    session->beginLasso(QPointF(10, 5), SelectionMode::replace);
    session->extendLasso(QPointF(40, 5));
    session->extendLasso(QPointF(10, 35));
    session->finishLasso();
    QVERIFY(session->history.undoName() == "Lasso" && session->canCopyPixels());
    session->copySelection();
    session->paste();
    const ImageLayer pasted = session->activeLayer().value();
    QCOMPARE(pasted.transform.origin, QPointF(10, 5));
    QCOMPARE(pasted.size(), QSizeF(30, 30));
    QCOMPARE(layerPixel(*session, pasted.id, 15, 10), (std::vector<int>{255, 0, 0, 255}));
    // Past the diagonal, and a soft edge along it.
    QCOMPARE(layerPixel(*session, pasted.id, 38, 33)[3], 0);
    int soft = 0;
    for (int x = 10; x < 40; ++x) {
        const int alpha = layerPixel(*session, pasted.id, x, 40 - x + 4)[3];
        soft += alpha > 0 && alpha < 255;
    }
    QVERIFY(soft > 0);
}

void SelectionClipboardTests::copyMergedTakesEveryVisibleLayerNotJustTheActiveOne()
{
    const auto session = makeSession();
    // A small green layer over the red and blue one.
    QImage square = BrushRaster::context(20, 20, false);
    square.fill(Qt::green);
    session->insert(ImportedImage(square, square, "Green"), QPointF(70, 20));
    const QUuid green = session->activeLayerID().value();
    QCOMPARE(layerWith(*session, green).transform.origin, QPointF(60, 10));
    select(*session, QRectF(55, 5, 30, 30));
    // The active layer alone: just the green square.
    session->copySelection();
    session->paste();
    QUuid pasted = session->activeLayerID().value();
    QCOMPARE(layerPixel(*session, pasted, 65, 15), (std::vector<int>{0, 255, 0, 255}));
    QCOMPARE(layerPixel(*session, pasted, 58, 8)[3], 0);
    session->undo();
    session->selectLayer(green);
    select(*session, QRectF(55, 5, 30, 30));
    QVERIFY(session->canCopyMerged());
    session->copyMergedSelection();
    session->paste();
    pasted = session->activeLayerID().value();
    QCOMPARE(layerPixel(*session, pasted, 65, 15), (std::vector<int>{0, 255, 0, 255}));
    QCOMPARE(layerPixel(*session, pasted, 58, 8), (std::vector<int>{0, 0, 255, 255}));
    session->undo();
    // A triangle: the far corner clear, its hypotenuse soft.
    session->selectLayer(green);
    session->selectTool(NavigationTool::lasso);
    lasso(*session, {QPointF(55, 5), QPointF(85, 5), QPointF(55, 35)});
    QCOMPARE(session->history.undoName(), QString("Lasso"));
    session->copyMergedSelection();
    session->paste();
    pasted = session->activeLayerID().value();
    QCOMPARE(layerPixel(*session, pasted, 62, 12), (std::vector<int>{0, 255, 0, 255}));
    QCOMPARE(layerPixel(*session, pasted, 83, 33)[3], 0);
    const int edge = layerPixel(*session, pasted, 69, 20)[3];
    QVERIFY(edge > 0 && edge < 255);
    session->undo();
    // Hidden layers are left out.
    session->selectLayer(green);
    session->toggleLayerVisibility(green);
    select(*session, QRectF(55, 5, 30, 30));
    session->copyMergedSelection();
    session->paste();
    pasted = session->activeLayerID().value();
    QCOMPARE(layerPixel(*session, pasted, 65, 15), (std::vector<int>{0, 0, 255, 255}));
    // With nothing drawn there is nothing to merge.
    const auto blank = std::make_unique<EditorSession>();
    blank->createDocument(8, 8, true);
    QVERIFY(!blank->canCopyMerged());
}

void SelectionClipboardTests::aCopyTakesTheLayersOwnPixelsAtTheirTransformedPlaces()
{
    const auto session = makeSession();
    // A 20 by 10 yellow bar centred at (50, 20).
    QImage bar = BrushRaster::context(20, 10, false);
    bar.fill(Qt::yellow);
    session->insert(ImportedImage(bar, bar, "Bar"), QPointF(50, 20));
    const QUuid id = session->activeLayerID().value();
    QCOMPARE(layerWith(*session, id).transform.origin, QPointF(40, 15));
    // Hidden, faint, masked out and turned upright: the pixels stay.
    QImage black = BrushRaster::context(20, 10, true);
    black.fill(Qt::black);
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, id, LayerMask::assetFrom(black));
        record(snapshot, id).isVisible = false;
        record(snapshot, id).opacity = 0.25;
        record(snapshot, id).transform.rotation = 90;
    });
    session->selectLayerTarget(id, false);
    select(*session, QRectF(30, 5, 40, 30));
    session->copySelection();
    session->paste();
    const ImageLayer pasted = session->activeLayer().value();
    QCOMPARE(pasted.transform.origin, QPointF(30, 5));
    QCOMPARE(layerPixel(*session, pasted.id, 50, 12), (std::vector<int>{255, 255, 0, 255}));
    QCOMPARE(layerPixel(*session, pasted.id, 50, 28), (std::vector<int>{255, 255, 0, 255}));
    QCOMPARE(layerPixel(*session, pasted.id, 42, 20)[3], 0);
    QCOMPARE(layerPixel(*session, pasted.id, 58, 20)[3], 0);
}

void SelectionClipboardTests::aMaskCopiesAsOpaqueGray()
{
    const auto session = makeSession();
    const QUuid id = session->activeLayerID().value();
    QImage mask = BrushRaster::context(100, 40, true);
    mask.fill(Qt::white);
    for (int y = 0; y < 40; ++y)
        std::fill_n(mask.scanLine(y), 30, uchar(0));
    rewrite(*session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, id, LayerMask::assetFrom(mask)); });
    session->selectLayerTarget(id, true);
    select(*session, QRectF(20, 10, 20, 20));
    QVERIFY(session->canCopyPixels());
    session->copySelection();
    session->paste();
    const QUuid pasted = session->activeLayerID().value();
    QCOMPARE(layerPixel(*session, pasted, 25, 15), (std::vector<int>{0, 0, 0, 255}));
    QCOMPARE(layerPixel(*session, pasted, 35, 15), (std::vector<int>{255, 255, 255, 255}));
    QCOMPARE(layerPixel(*session, pasted, 45, 15)[3], 0);
    session->undo();
    // A mask placed apart: outside it, its edges' mean gray.
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        record(snapshot, id).maskPlacement = LayerTransform{.origin = {60, 0}, .size = {100, 40}};
        record(snapshot, id).maskLinked = false;
    });
    session->selectLayerTarget(id, true);
    select(*session, QRectF(20, 10, 20, 20));
    session->copySelection();
    session->paste();
    const std::vector<int> ground = layerPixel(*session, session->activeLayerID().value(), 25, 15);
    const int tone = qRound(LayerMask::background(layerWith(*session, id).mask.value().asset.thumbnail) * 255);
    QVERIFY(tone > 0);
    QCOMPARE(ground, (std::vector<int>{tone, tone, tone, 255}));
    session->undo();
    // Placed away, this mask's black edges cover the region.
    QImage inverse = BrushRaster::context(100, 40, true);
    inverse.fill(Qt::black);
    for (int y = 5; y < 35; ++y)
        std::fill_n(inverse.scanLine(y) + 15, 30, uchar(255));
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, id, LayerMask::assetFrom(inverse));
        record(snapshot, id).maskPlacement = LayerTransform{.origin = {60, 0}, .size = {100, 40}};
        record(snapshot, id).maskLinked = false;
    });
    session->selectLayerTarget(id, true);
    select(*session, QRectF(20, 10, 20, 20));
    session->copySelection();
    session->paste();
    QCOMPARE(layerPixel(*session, session->activeLayerID().value(), 25, 15), (std::vector<int>{0, 0, 0, 255}));
    session->undo();
    // A folder copies its mask alone; without one, nothing.
    session->selectLayer(id);
    session->addGroup();
    QVERIFY(!session->canCopyPixels());
    session->addLayerMask(false);
    QVERIFY(session->canCopyPixels());
}

void SelectionClipboardTests::aSoftSelectionEdgeClipsBothMaskPasses()
{
    const auto session = makeSession();
    const QUuid id = session->activeLayerID().value();
    session->addMask(true);
    session->selectLayerTarget(id, true);
    // Half a pixel in: half coverage at the left column.
    select(*session, QRectF(20.5, 10, 20, 20));
    session->copySelection();
    session->paste();
    const QUuid pasted = session->activeLayerID().value();
    // Ground at half, then white at half over it: 192.
    QVERIFY(near(layerPixel(*session, pasted, 20, 15), {128, 128, 128, 192}));
    QCOMPARE(layerPixel(*session, pasted, 25, 15), (std::vector<int>{255, 255, 255, 255}));
    QCOMPARE(layerPixel(*session, pasted, 19, 15), (std::vector<int>{0, 0, 0, 0}));
    // The layer's pixels take the coverage once.
    session->selectLayerTarget(id, false);
    select(*session, QRectF(20.5, 10, 20, 20));
    session->copySelection();
    session->paste();
    QVERIFY(near(layerPixel(*session, session->activeLayerID().value(), 20, 15), {128, 0, 0, 128}));
}

void SelectionClipboardTests::theCopyRegionRoundsOffFloatNoiseAndStopsAtTheCanvas()
{
    const auto session = makeSession();
    QCOMPARE(session->selectionCopyRegion(), std::optional(QRectF(0, 0, 100, 40)));
    session->applySelection(rectPath(QRectF(9.9995, 10.0004, 30.0009, 19.9993)), SelectionMode::replace, "Select");
    QCOMPARE(session->selectionCopyRegion(), std::optional(QRectF(10, 10, 30, 20)));
    session->applySelection(rectPath(QRectF(10.3, 10.3, 30, 20)), SelectionMode::replace, "Select");
    QCOMPARE(session->selectionCopyRegion(), std::optional(QRectF(10, 10, 31, 21)));
    // Beyond the canvas the region stops; nudged off it, nothing.
    session->selectAll();
    session->nudgeSelection(90, 0);
    QCOMPARE(session->selectionCopyRegion(), std::optional(QRectF(90, 0, 10, 40)));
    session->nudgeSelection(10, 0);
    QVERIFY(!session->selectionCopyRegion().has_value());
    QVERIFY(session->canCopyPixels());
    const int count = session->history.undoCount();
    session->copySelection();
    session->layerViaCopy();
    QCOMPARE(session->history.undoCount(), count);
    QVERIFY(!session->pixelClipboard().has_value());
}

void SelectionClipboardTests::anotherAppsImagePastesCentredAndUnstalesOurs()
{
    const auto session = makeSession();
    select(*session, QRectF(0, 0, 10, 10));
    session->copySelection();
    QVERIFY(session->canPaste());
    // Another app copies a picture: Paste centres that instead.
    QImage picture(20, 10, QImage::Format_ARGB32);
    picture.fill(QColor(0, 255, 0, 255));
    QGuiApplication::clipboard()->setImage(picture);
    QVERIFY(session->canPaste());
    session->paste();
    const ImageLayer pasted = session->activeLayer().value();
    QCOMPARE(pasted.transform.origin, QPointF(40, 15));
    QCOMPARE(pasted.size(), QSizeF(20, 10));
    QCOMPARE(pasted.asset.value().image().format(), QImage::Format_RGBA8888_Premultiplied);
    QCOMPARE(layerPixel(*session, pasted.id, 50, 20), (std::vector<int>{0, 255, 0, 255}));
    // Linear gray 128 is sRGB 188, as CoreGraphics converts it.
    QImage linear(4, 4, QImage::Format_ARGB32);
    linear.fill(QColor(128, 128, 128, 255));
    linear.setColorSpace(QColorSpace::SRgbLinear);
    QGuiApplication::clipboard()->setImage(linear);
    session->paste();
    const ImageLayer converted = session->activeLayer().value();
    QVERIFY(near(layerPixel(*session, converted.id, 50, 20), {188, 188, 188, 255}));
    QCOMPARE(converted.asset.value().image().colorSpace(), QColorSpace(QColorSpace::SRgb));
    // Text on the clipboard pastes nothing.
    QGuiApplication::clipboard()->setText(QStringLiteral("words"));
    QVERIFY(!session->canPaste());
    const int count = session->history.undoCount();
    session->paste();
    QCOMPARE(session->history.undoCount(), count);
    // Our own copy again: back in place.
    session->copySelection();
    session->paste();
    QCOMPARE(session->activeLayer().value().transform.origin, QPointF(0, 0));
}

void SelectionClipboardTests::copyingAndPastingAreRefusedWhereSwiftRefusesThem()
{
    EditorSession session;
    QVERIFY(!session.canCopyPixels() && !session.canCopyMerged() && !session.canPaste());
    session.createDocument(20, 10, true);
    // A blank layer has no pixels to copy.
    QVERIFY(!session.canCopyPixels());
    QImage image = BrushRaster::context(20, 10, false);
    image.fill(Qt::red);
    session.insert(ImportedImage(image, image, "Red"));
    QVERIFY(session.canCopyPixels());
    session.copySelection();
    QVERIFY(session.canPaste());
    // An empty selection copies nothing; busy, nothing at all.
    select(session, QRectF(0, 0, 5, 5));
    session.applySelection(rectPath(QRectF(0, 0, 20, 10)), SelectionMode::subtract, "Subtract");
    QVERIFY(!session.canCopyPixels() && !session.canCopyMerged());
    session.deselect();
    session.setIsProjectBusy(true);
    QVERIFY(!session.canCopyPixels() && !session.canPaste());
    session.setIsProjectBusy(false);
    QVERIFY(session.canPaste());
    // A paste's signal, a copy's silence.
    QSignalSpy changes(&session, &EditorSession::changed);
    session.copySelection();
    QCOMPARE(changes.count(), 0);
    session.paste();
    QVERIFY(changes.count() > 0);
    // A cleared clipboard holds no data object at all.
    QGuiApplication::clipboard()->clear();
    QVERIFY(!session.canPaste());
    const int count = session.history.undoCount();
    session.paste();
    QCOMPARE(session.history.undoCount(), count);
}

void SelectionClipboardTests::anImageTooLargeToCopyWarnsWithoutAnAlert()
{
    const auto session = makeSession();
    // Half a megabyte of bits; its working copy needs sixteen.
    const QImage large(2000, 2000, QImage::Format_Mono);
    QVERIFY(!large.isNull());
    QGuiApplication::clipboard()->setImage(large);
    QVERIFY(session->canPaste());
    const int count = session->history.undoCount();
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("the clipboard's image could not be copied")));
    {
        malloc_trim(0);
        const AddressSpaceLimit limit(4 * 1024 * 1024);
        session->paste();
    }
    QCOMPARE(session->history.undoCount(), count);
    QVERIFY(!session->brushError().has_value());
}

void SelectionClipboardTests::cutLeavesAHoleAndPasteRestoresThePixels()
{
    const auto session = makeSession();
    const QUuid source = session->activeLayerID().value();
    select(*session, QRectF(0, 0, 10, 10));
    const int count = session->history.undoCount();
    bool done = false;
    session->cutSelection([&] { done = true; });
    QTRY_VERIFY(done);
    QVERIFY(session->pixelClipboard().has_value());
    QCOMPARE(session->history.undoName(), QString("Clear"));
    QCOMPARE(session->history.undoCount(), count + 1);
    QCOMPARE(layerPixel(*session, source, 5, 5)[3], 0);
    QCOMPARE(layerPixel(*session, source, 15, 5), (std::vector<int>{255, 0, 0, 255}));
    // The pixels come back where they were.
    session->paste();
    QCOMPARE(layerPixel(*session, session->activeLayerID().value(), 5, 5), (std::vector<int>{255, 0, 0, 255}));
    // Without a selection, nothing is cut.
    session->deselect();
    done = false;
    session->cutSelection([&] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(session->history.undoName(), QString("Paste"));
}

QTEST_MAIN(SelectionClipboardTests)
#include "SelectionClipboardTests.moc"
