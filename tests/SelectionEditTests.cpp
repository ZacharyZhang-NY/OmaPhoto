#include "Document/BrushStroke.h"
#include "IO/ImageExporter.h"
#include "AddressSpaceLimit.h"
#include "RenderFixtures.h"
#include "SelectionFixtures.h"
#include <QElapsedTimer>

// Masks from a selection, the delete target, and Invert.
namespace {
std::unique_ptr<EditorSession> editSession(int width = 100, int height = 40)
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(width, height, true);
    return session;
}

void select(EditorSession &session, const QRectF &rect, bool antialiased = true)
{
    session.setSelectionAntialiased(antialiased);
    session.applySelection(rectPath(rect), SelectionMode::replace, "Select");
}

// Premultiplied bytes of the rendered document at a pixel.
std::vector<int> pixel(const QImage &image, int x, int y)
{
    const QImage bytes = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    const uchar *at = bytes.constScanLine(y) + x * 4;
    return {at[0], at[1], at[2], at[3]};
}

QImage render(const EditorSession &session)
{
    return ImageExporter::render(session.projectSnapshot().value()).image;
}

// A 100×40 layer: red left half, blue right half.
void twoColorLayer(EditorSession &session)
{
    QImage image = BrushRaster::context(100, 40, false);
    QPainter painter(&image);
    painter.fillRect(QRect(0, 0, 50, 40), Qt::red);
    painter.fillRect(QRect(50, 0, 50, 40), Qt::blue);
    painter.end();
    session.insert(ImportedImage(image, image, "Two"));
}

// Runs the invert and waits for its step.
void invert(EditorSession &session)
{
    bool done = false;
    session.invertPixels([&] { done = true; });
    QTRY_VERIFY(done);
}
}

class SelectionEditTests : public QObject {
    Q_OBJECT
private slots:
    void maskButtonAddsWhiteMaskOrHidesTheSelection();
    void layerMenuMasksUseTheSelection();
    void maskFromSelectionLinesUpOnScaledLayers();
    void deletingWithTheMaskTargetedRemovesOnlyTheMask();
    void invertKeepsTransparencyStaysInSelectionAndWorksOnMasks();
    void invertIsFastOnLargeImagesAndHandlesUniformMasksWithASelection();
    void invertWorksInEveryTool();
    void invertIsRefusedWhereSwiftRefusesIt();
    void anInvertComputedFromOtherPixelsIsDropped();
    void aPaintedAssetThatCannotFlattenReportsAndFreesTheProject();
};

void SelectionEditTests::maskButtonAddsWhiteMaskOrHidesTheSelection()
{
    const auto session = editSession();
    session->insert(filledRed());
    session->addMask();
    QVERIFY(session->activeLayer().value().mask.has_value());
    QCOMPARE(session->history.undoName(), QString("Add Reveal-All Mask"));
    QCOMPARE(pixel(render(*session), 30, 20)[3], 255);
    session->undo();
    QVERIFY(!session->activeLayer().value().mask.has_value());
    select(*session, QRectF(20, 10, 30, 20));
    session->addMask();
    QCOMPARE(session->history.undoName(), QString("Add Mask from Selection"));
    QVERIFY(!session->selection().has_value() && session->isMaskSelected());
    const QImage result = render(*session);
    // The selected area is black, hidden; the rest white.
    QCOMPARE(pixel(result, 30, 20)[3], 0);
    QCOMPARE(pixel(result, 5, 5)[3], 255);
    session->undo();
    QVERIFY(!session->activeLayer().value().mask.has_value() && session->selection().has_value());
    // A layer with a mask takes no second one.
    session->redo();
    const int count = session->history.undoCount();
    select(*session, QRectF(0, 0, 10, 10));
    session->addMask();
    QCOMPARE(session->history.undoCount(), count + 1);
}

void SelectionEditTests::layerMenuMasksUseTheSelection()
{
    const auto session = editSession();
    session->insert(filledRed());
    select(*session, QRectF(20, 10, 30, 20));
    session->addMask(false);
    QCOMPARE(session->history.undoName(), QString("Add Mask from Selection"));
    QVERIFY(!session->selection().has_value() && session->isMaskSelected());
    const QImage result = render(*session);
    // The selected area is white, visible; the rest black.
    QCOMPARE(pixel(result, 30, 20)[3], 255);
    QCOMPARE(pixel(result, 5, 5)[3], 0);
    session->undo();
    QVERIFY(!session->activeLayer().value().mask.has_value() && session->selection().has_value());
    session->deselect();
    // No selection: a plain black mask.
    session->addMask(false);
    QCOMPARE(session->history.undoName(), QString("Add Hide-All Mask"));
    QCOMPARE(pixel(render(*session), 30, 20)[3], 0);
}

void SelectionEditTests::maskFromSelectionLinesUpOnScaledLayers()
{
    const auto session = editSession(100, 100);
    QImage image = BrushRaster::context(50, 50, false);
    image.fill(Qt::blue);
    session->insert(ImportedImage(image, image, "Blue"));
    const QUuid id = session->activeLayerID().value();
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        record(snapshot, id).transform.origin = QPointF(0, 0);
        record(snapshot, id).transform.size = QSizeF(100, 100);
    });
    select(*session, QRectF(0, 0, 50, 50));
    session->addMask();
    // The mask uses the layer's pixel grid.
    QCOMPARE(session->activeLayer().value().mask.value().asset.size(), QSize(50, 50));
    const QImage result = render(*session);
    QCOMPARE(pixel(result, 25, 25)[3], 0);
    QCOMPARE(pixel(result, 75, 75)[3], 255);
    QCOMPARE(pixel(result, 75, 25)[3], 255);
    // A blank layer's grid is its rounded size.
    session->undo();
    session->addBlankLayer();
    select(*session, QRectF(10, 10, 20, 20));
    session->addMask();
    QCOMPARE(session->activeLayer().value().mask.value().asset.size(), QSize(100, 100));
    QCOMPARE(int(session->activeLayer().value().mask.value().asset.image().constScanLine(15)[15]), 0);
    QCOMPARE(int(session->activeLayer().value().mask.value().asset.image().constScanLine(5)[5]), 255);
    // A soft outline softens the mask's edge; hard, none.
    QPainterPath triangle;
    triangle.moveTo(0, 0);
    triangle.lineTo(100, 0);
    triangle.lineTo(0, 100);
    triangle.closeSubpath();
    for (const bool soft : {true, false}) {
        session->undo();
        session->setSelectionAntialiased(soft);
        session->applySelection(triangle, SelectionMode::replace, "Select");
        session->addMask();
        const QImage edge = session->activeLayer().value().mask.value().asset.image();
        int partial = 0;
        for (int x = 0; x < 100; ++x) {
            const int value = edge.constScanLine(99 - x)[x];
            partial += value > 0 && value < 255;
        }
        QCOMPARE(partial > 0, soft);
    }
}

void SelectionEditTests::deletingWithTheMaskTargetedRemovesOnlyTheMask()
{
    const auto session = editSession();
    const QUuid id = session->activeLayerID().value();
    session->addLayerMask(false);
    QVERIFY(session->isMaskSelected());
    session->deleteLayerOrMask();
    QCOMPARE(session->activeLayer().value().id, id);
    QVERIFY(!session->activeLayer().value().mask.has_value());
    QCOMPARE(session->history.undoName(), QString("Delete Layer Mask"));
    QVERIFY(!session->isMaskSelected());
    session->undo();
    QVERIFY(session->activeLayer().value().mask.has_value());
    // The trash button follows the same target.
    session->selectLayerTarget(id, true);
    session->deleteLayerOrMask();
    QCOMPARE(session->document().value().layers.size(), size_t(1));
    QVERIFY(!session->activeLayer().value().mask.has_value());
    // With the layer's pixels targeted, the whole layer goes.
    session->addLayerMask();
    session->selectLayerTarget(id, false);
    session->deleteLayerOrMask();
    QVERIFY(session->document().value().layers.empty());
}

void SelectionEditTests::invertKeepsTransparencyStaysInSelectionAndWorksOnMasks()
{
    const auto session = editSession();
    twoColorLayer(*session);
    // The top half only.
    select(*session, QRectF(0, 0, 100, 20));
    const int revision = session->brushRevision();
    invert(*session);
    QCOMPARE(session->history.undoName(), QString("Invert"));
    QVERIFY(!session->isProjectBusy());
    // The canvas's counter moves as Swift's does.
    QCOMPARE(session->brushRevision(), revision + 1);
    QImage result = render(*session);
    QCOMPARE(pixel(result, 10, 5), (std::vector<int>{0, 255, 255, 255}));
    QCOMPARE(pixel(result, 90, 5), (std::vector<int>{255, 255, 0, 255}));
    QCOMPARE(pixel(result, 10, 30), (std::vector<int>{255, 0, 0, 255}));
    session->deselect();
    // A half-transparent pixel keeps its alpha.
    const auto blank = editSession();
    QImage half = BrushRaster::context(100, 40, false);
    half.fill(QColor(255, 255, 255, 128));
    blank->insert(ImportedImage(half, half, "Half"));
    invert(*blank);
    const std::vector<int> faded = pixel(render(*blank), 50, 20);
    QVERIFY(std::abs(faded[3] - 128) <= 1 && faded[0] == 0);
    // On a mask black and white swap: white hides all.
    session->addLayerMask();
    session->toggleMaskLink(session->activeLayerID().value());
    invert(*session);
    QCOMPARE(session->history.undoName(), QString("Invert Mask"));
    QVERIFY(!session->activeLayer().value().mask.value().isLinked);
    result = render(*session);
    QCOMPARE(pixel(result, 50, 30)[3], 0);
    session->undo();
    QCOMPARE(pixel(render(*session), 50, 30)[3], 255);
    // A mask placed apart takes the selection where it shows.
    const QUuid id = session->activeLayerID().value();
    QImage white = BrushRaster::context(100, 40, true);
    white.fill(Qt::white);
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, id, LayerMask::assetFrom(white));
        record(snapshot, id).maskPlacement = LayerTransform{.origin = {50, 0}, .size = {100, 40}};
        record(snapshot, id).maskLinked = false;
    });
    session->selectLayerTarget(id, true);
    select(*session, QRectF(50, 0, 50, 40));
    invert(*session);
    const QImage placed = session->activeLayer().value().mask.value().asset.image();
    QCOMPARE(int(placed.constScanLine(20)[25]), 0);
    QCOMPARE(int(placed.constScanLine(20)[75]), 255);
}

void SelectionEditTests::invertIsFastOnLargeImagesAndHandlesUniformMasksWithASelection()
{
    const auto session = editSession(4000, 3000);
    QImage image = BrushRaster::context(4000, 3000, false);
    image.fill(Qt::red);
    session->insert(ImportedImage(image, image, "Big"));
    QElapsedTimer clock;
    clock.start();
    invert(*session);
    const qint64 whole = clock.restart();
    select(*session, QRectF(0, 0, 2000, 3000));
    invert(*session);
    const qint64 selected = clock.elapsed();
    QVERIFY2(whole < 1500 && selected < 1500, qPrintable(QStringLiteral("whole %1 ms, selected %2 ms").arg(whole).arg(selected)));
    const QImage result = render(*session);
    // Inverted twice, then once.
    QCOMPARE(pixel(result, 100, 100), (std::vector<int>{255, 0, 0, 255}));
    QCOMPARE(pixel(result, 3000, 100), (std::vector<int>{0, 255, 255, 255}));
    // A 1×1 mask with a selection inverts the selected part.
    session->addLayerMask();
    invert(*session);
    QCOMPARE(session->activeLayer().value().mask.value().asset.size(), QSize(4000, 3000));
    const QImage masked = render(*session);
    QCOMPARE(pixel(masked, 100, 100)[3], 0);
    QCOMPARE(pixel(masked, 3000, 100)[3], 255);
}

void SelectionEditTests::invertWorksInEveryTool()
{
    const auto session = editSession();
    twoColorLayer(*session);
    for (const NavigationTool tool : {NavigationTool::brush, NavigationTool::move, NavigationTool::crop, NavigationTool::hand, NavigationTool::zoom, NavigationTool::lasso}) {
        session->selectTool(tool);
        QVERIFY2(session->canInvert(), qPrintable(label(tool)));
    }
    session->selectTool(NavigationTool::crop);
    invert(*session);
    QCOMPARE(session->history.undoName(), QString("Invert"));
    QCOMPARE(pixel(render(*session), 10, 5), (std::vector<int>{0, 255, 255, 255}));
    // A pending transform is applied first, then inverted.
    session->selectTool(NavigationTool::move);
    session->beginTransform();
    session->previewTransform(LayerTransform{.origin = {10, 0}, .size = {100, 40}});
    QVERIFY(session->transformEdit().has_value() && session->canInvert());
    invert(*session);
    QVERIFY(!session->transformEdit().has_value());
    QCOMPARE(session->history.undoName(), QString("Invert"));
    session->undo();
    QCOMPARE(session->history.undoName(), QString("Transform Layer"));
    QCOMPARE(session->activeLayer().value().transform.origin, QPointF(10, 0));
}

void SelectionEditTests::invertIsRefusedWhereSwiftRefusesIt()
{
    EditorSession session;
    QVERIFY(!session.canInvert());
    bool done = false;
    session.invertPixels([&] { done = true; });
    QTRY_VERIFY(done);
    session.createDocument(100, 40, true);
    // A blank layer has no pixels; its mask inverts.
    QVERIFY(!session.canInvert());
    session.addLayerMask();
    QVERIFY(session.canInvert());
    session.toggleLayerMask();
    QVERIFY(!session.canInvert());
    session.toggleLayerMask();
    const QUuid blank = session.activeLayerID().value();
    twoColorLayer(session);
    const QUuid two = session.activeLayerID().value();
    QVERIFY(session.canInvert());
    session.setIsProjectBusy(true);
    QVERIFY(!session.canInvert());
    session.setIsProjectBusy(false);
    session.setIsImporting(true);
    QVERIFY(!session.canInvert());
    session.setIsImporting(false);
    session.setRenamingLayerID(session.activeLayerID());
    QVERIFY(!session.canInvert());
    session.setRenamingLayerID(std::nullopt);
    session.setShowsNewDocument(true);
    QVERIFY(!session.canInvert());
    session.setShowsNewDocument(false);
    session.setShowsImporter(true);
    QVERIFY(!session.canInvert());
    session.setShowsImporter(false);
    // Hidden, two selected, an empty selection, a folder.
    session.toggleLayerVisibility(session.activeLayerID().value());
    QVERIFY(!session.canInvert());
    session.toggleLayerVisibility(session.activeLayerID().value());
    session.selectLayers({two, blank}, two);
    QCOMPARE(session.activeLayerID(), std::optional(two));
    QVERIFY(!session.canInvert());
    session.selectLayers({two}, two);
    QVERIFY(session.canInvert());
    select(session, QRectF(10, 10, 10, 10));
    session.applySelection(rectPath(QRectF(0, 0, 100, 40)), SelectionMode::subtract, "Subtract");
    QVERIFY(session.selection().value().isEmpty() && !session.canInvert());
    const std::optional<CanvasDocument> before = session.document();
    const int count = session.history.undoCount();
    done = false;
    session.invertPixels([&] { done = true; });
    QTRY_VERIFY(done);
    QVERIFY(session.document() == before && session.history.undoCount() == count);
    session.deselect();
    session.addGroup();
    QVERIFY(!session.canInvert());
    session.addLayerMask();
    QVERIFY(session.canInvert());
}

void SelectionEditTests::anInvertComputedFromOtherPixelsIsDropped()
{
    const auto session = editSession();
    twoColorLayer(*session);
    const QUuid id = session->activeLayerID().value();
    const int revision = session->brushRevision();
    bool done = false;
    session->invertPixels([&] { done = true; });
    QVERIFY(session->isProjectBusy());
    // Meanwhile the layer takes other pixels, as a load would.
    QImage green = BrushRaster::context(100, 40, false);
    green.fill(Qt::green);
    rewrite(*session, [&](ProjectSnapshot &snapshot) { snapshot.images.insert_or_assign(id, ImportedImage(green, green, "Green")); });
    QTRY_VERIFY(done);
    QVERIFY(!session->isProjectBusy());
    QCOMPARE(session->history.undoCount(), 0);
    QCOMPARE(session->brushRevision(), revision);
    QCOMPARE(pixel(render(*session), 10, 5), (std::vector<int>{0, 255, 0, 255}));
    // Gone altogether, it is dropped as well.
    done = false;
    session->invertPixels([&] { done = true; });
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        snapshot.manifest.layers.clear();
        snapshot.images.clear();
        snapshot.manifest.activeLayerID = std::nullopt;
    });
    QTRY_VERIFY(done);
    QVERIFY(session->document().value().layers.empty() && !session->isProjectBusy());
    // The document itself gone: the callback still comes.
    twoColorLayer(*session);
    done = false;
    session->invertPixels([&] { done = true; });
    session->clearProject();
    QTRY_VERIFY(done);
    QVERIFY(!session->document().has_value() && !session->isProjectBusy());
    // Only the mask replaced: the mask's invert is dropped too.
    session->createDocument(100, 40, true);
    twoColorLayer(*session);
    const QUuid two = session->activeLayerID().value();
    session->addLayerMask();
    done = false;
    session->invertPixels([&] { done = true; });
    QImage gray = BrushRaster::context(100, 40, true);
    gray.fill(QColor(100, 100, 100));
    rewrite(*session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, two, LayerMask::assetFrom(gray)); });
    QTRY_VERIFY(done);
    QCOMPARE(int(session->activeLayer().value().mask.value().asset.image().constScanLine(0)[0]), 100);
    QCOMPARE(session->history.undoCount(), 0);
    QVERIFY(!session->isProjectBusy());
}

void SelectionEditTests::aPaintedAssetThatCannotFlattenReportsAndFreesTheProject()
{
    // A painted base of 48 MB, flattened on first use.
    const auto raster = std::make_shared<const RasterSnapshot>(4000, 3000, solid(2, 2, qRgba(255, 0, 0, 255)), QRectF(0, 0, 2, 2), std::vector<BrushPatch>{});
    const auto session = editSession(4000, 3000);
    session->insert(ImportedImage(raster, QImage(), "Painted"));
    QVERIFY(session->canInvert());
    const int count = session->history.undoCount();
    bool done = false;
    {
        const AddressSpaceLimit limit(16 * 1024 * 1024);
        session->invertPixels([&] { done = true; });
    }
    QVERIFY(session->brushError().has_value() && !session->isProjectBusy());
    QTRY_VERIFY(done);
    QCOMPARE(session->history.undoCount(), count);
    QVERIFY(!raster->hasMaterializedPixels());
}

QTEST_GUILESS_MAIN(SelectionEditTests)
#include "SelectionEditTests.moc"
