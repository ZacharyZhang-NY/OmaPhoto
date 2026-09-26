#include "BrushFixtures.h"
#include "CanvasFixtures.h"
#include "SelectionCanvasFixtures.h"
#include "SelectionFixtures.h"

// Painting on the canvas: nothing away from the brush moves.
namespace {
struct Shift {
    int start;
    int finish;
    bool committed;
};

// Swift's canvasShift: a big photo scaled down, painted mid.
Shift canvasShift(bool paintingMask, double layerScale = 0.25)
{
    Shown shown(QSize(800, 600), QSize(800, 600));
    EditorSession &session = shown.session;
    const QImage photo = noise(2400, 1800, 3);
    session.insert(ImportedImage(photo, photo, "Photo"));
    const QUuid id = session.activeLayerID().value();
    LayerTransform transform{.origin = {100, 75}, .size = {2400 * layerScale, 1800 * layerScale}};
    transform.sampling = LayerSampling::high;
    rewrite(session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform = transform; });
    session.selectLayer(id);
    if (paintingMask) {
        session.addLayerMask();
        if (!session.isMaskSelected())
            throw std::runtime_error("the mask was not targeted");
    }
    shown.settle();
    session.selectTool(NavigationTool::brush);
    BrushSettings settings = session.brushSettings();
    settings.diameter = 30;
    session.setBrushSettings(settings);
    const auto snapshot = [&] {
        shown.canvas->synchronizeDisplay();
        return shown.canvas->grab().toImage().convertToFormat(QImage::Format_RGBA8888);
    };
    // The brushed area in view pixels, grown generously.
    const QPointF brush = transform.center();
    const QSizeF size = session.document().value().size();
    const QPointF topLeft = session.viewport.viewPoint(QPointF(brush.x() - 60, brush.y() - 40), size);
    const QPointF bottomRight = session.viewport.viewPoint(QPointF(brush.x() + 80, brush.y() + 40), size);
    const auto largestDifference = [&](const QImage &a, const QImage &b) {
        const double perPoint = double(a.width()) / shown.canvas->width();
        const QRectF box(topLeft.x() * perPoint, topLeft.y() * perPoint, (bottomRight.x() - topLeft.x()) * perPoint, (bottomRight.y() - topLeft.y()) * perPoint);
        int largest = 0;
        for (int y = 0; y < a.height(); ++y) {
            const uchar *rowA = a.constScanLine(y), *rowB = b.constScanLine(y);
            for (int x = 0; x < a.width(); ++x) {
                if (box.contains(QPointF(x, y)))
                    continue;
                for (int c = 0; c < 4; ++c)
                    largest = std::max(largest, std::abs(int(rowA[x * 4 + c]) - int(rowB[x * 4 + c])));
            }
        }
        return largest;
    };
    const QImage before = snapshot();
    session.beginBrush(brush);
    session.continueBrush(QPointF(brush.x() + 20, brush.y()));
    const QImage during = snapshot();
    session.finishBrushImmediately();
    const QImage after = snapshot();
    const ImageLayer layer = session.activeLayer().value();
    const bool committed = paintingMask ? layer.mask.value().asset.raster != nullptr : layer.asset.value().raster != nullptr;
    return {largestDifference(before, during), largestDifference(during, after), committed};
}
}

class BrushCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void paintingAScaledDownLayerDoesNotShiftItsPixels();
    void paintingAScaledDownLayersMaskDoesNotShiftItsPixels();
    void theCanvasRepaintsEveryStrokeStepAndDeleteClearsOrDeletes();
    void aPlacedMaskStrokeShowsTheLayerThroughItsPreview();
    void aStrokeStepRepaintsWhereItWentAlone();
    void paintingAFolderMaskClipsItsLayersLive();
    void aStripedFolderMaskKeepsItsShadeWhilePainted();
};

void BrushCanvasTests::paintingAScaledDownLayerDoesNotShiftItsPixels()
{
    const Shift shift = canvasShift(false);
    QVERIFY2(shift.committed, "the stroke was committed");
    QVERIFY2(shift.start <= 3, qPrintable(QStringLiteral("starting to paint changed pixels away from the brush by %1").arg(shift.start)));
    QVERIFY2(shift.finish <= 3, qPrintable(QStringLiteral("finishing the stroke changed pixels away from the brush by %1").arg(shift.finish)));
}

void BrushCanvasTests::paintingAScaledDownLayersMaskDoesNotShiftItsPixels()
{
    const Shift shift = canvasShift(true);
    QVERIFY2(shift.committed, "the mask stroke was committed");
    QVERIFY2(shift.start <= 3, qPrintable(QStringLiteral("starting to paint the mask changed pixels away from the brush by %1").arg(shift.start)));
    QVERIFY2(shift.finish <= 3, qPrintable(QStringLiteral("finishing the mask stroke changed pixels away from the brush by %1").arg(shift.finish)));
}

void BrushCanvasTests::theCanvasRepaintsEveryStrokeStepAndDeleteClearsOrDeletes()
{
    Shown shown(QSize(400, 300), QSize(400, 300));
    EditorSession &session = shown.session;
    QImage blue = BrushRaster::context(400, 300, false);
    blue.fill(Qt::blue);
    session.insert(ImportedImage(blue, blue, "Blue"));
    shown.settle();
    session.zoom(1);
    session.selectTool(NavigationTool::brush);
    session.setBrushSettings(brush(20, 1, 1, 0, 0));
    shown.canvas->synchronizeDisplay();
    // Each step of a stroke is a new display state.
    session.beginBrush(QPointF(50, 50));
    QVERIFY(shown.canvas->synchronizeDisplay());
    QCOMPARE(shown.canvas->grab().toImage().pixelColor(50, 50), QColor(Qt::red));
    QCOMPARE(shown.canvas->grab().toImage().pixelColor(200, 50), QColor(Qt::blue));
    session.continueBrush(QPointF(200, 50));
    QVERIFY(shown.canvas->synchronizeDisplay());
    QVERIFY(!shown.canvas->synchronizeDisplay());
    QCOMPARE(shown.canvas->grab().toImage().pixelColor(200, 50), QColor(Qt::red));
    session.finishBrush();
    QVERIFY(shown.canvas->synchronizeDisplay());
    QCOMPARE(shown.canvas->grab().toImage().pixelColor(200, 50), QColor(Qt::red));
    // Delete clears a selection; without one it deletes the layer.
    session.selectTool(NavigationTool::marquee);
    session.applySelection(rectPath(QRectF(0, 0, 100, 300)), SelectionMode::replace, "Select");
    QTRY_VERIFY(shown.canvas->hasFocus());
    QTest::keyClick(shown.canvas, Qt::Key_Delete);
    QTRY_COMPARE(session.history.undoName(), QString("Clear"));
    session.deselect();
    const int layers = int(session.document().value().layers.size());
    QTest::keyClick(shown.canvas, Qt::Key_Backspace);
    QCOMPARE(int(session.document().value().layers.size()), layers - 1);
    // With Ctrl or Alt the key is not Delete's.
    session.undo();
    QTest::keyClick(shown.canvas, Qt::Key_Delete, Qt::ControlModifier);
    QCOMPARE(int(session.document().value().layers.size()), layers);
}

void BrushCanvasTests::aPlacedMaskStrokeShowsTheLayerThroughItsPreview()
{
    Shown shown(QSize(200, 200), QSize(200, 200));
    EditorSession &session = shown.session;
    QImage blue = BrushRaster::context(64, 64, false);
    blue.fill(Qt::blue);
    session.insert(ImportedImage(blue, blue, "Blue"), QPointF(96, 56));
    const QUuid id = session.activeLayerID().value();
    // A black 16 px mask at 32 from (80,40): hidden.
    rewrite(session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, id, LayerMask::assetFrom(BrushRaster::context(16, 16, true)));
        record(snapshot, id).maskPlacement = LayerTransform{.origin = {80, 40}, .size = {32, 32}};
        record(snapshot, id).maskLinked = false;
    });
    session.selectLayerTarget(id, true);
    shown.settle();
    session.zoom(1);
    session.selectTool(NavigationTool::brush);
    session.setBrushSettings(brush(8, 1, 1, 1, 1));
    session.setMaskPaintWhite(true);
    shown.canvas->synchronizeDisplay();
    QVERIFY(shown.canvas->grab().toImage().pixelColor(88, 48) != QColor(Qt::blue));
    session.beginBrush(QPointF(88, 48));
    QVERIFY(shown.canvas->synchronizeDisplay());
    QCOMPARE(shown.canvas->grab().toImage().pixelColor(88, 48), QColor(Qt::blue));
    QVERIFY(shown.canvas->grab().toImage().pixelColor(70, 70) != QColor(Qt::blue));
    session.finishBrush();
    QCOMPARE(shown.canvas->grab().toImage().pixelColor(88, 48), QColor(Qt::blue));
}

void BrushCanvasTests::aStrokeStepRepaintsWhereItWentAlone()
{
    Shown shown(QSize(400, 300), QSize(400, 300));
    EditorSession &session = shown.session;
    session.addBlankLayer();
    shown.settle();
    session.zoom(1);
    session.selectTool(NavigationTool::brush);
    session.setBrushSettings(brush(20, 1, 1, 0, 0));
    shown.canvas->synchronizeDisplay();
    QTest::qWait(50);
    PaintSpy spy(*shown.canvas);
    // The first tile alone, two points out: not everything.
    session.beginBrush(QPointF(50, 50));
    QVERIFY(shown.canvas->synchronizeDisplay());
    QTRY_VERIFY(spy.count > 0);
    QCOMPARE(spy.painted, QRect(0, 0, 258, 258));
    spy.painted = QRect();
    spy.count = 0;
    // Into the next tile: both, still not everything.
    session.continueBrush(QPointF(300, 50));
    QVERIFY(shown.canvas->synchronizeDisplay());
    QTRY_VERIFY(spy.count > 0);
    QCOMPARE(spy.painted, QRect(0, 0, 400, 258));
    session.cancelBrush();
}

void BrushCanvasTests::paintingAFolderMaskClipsItsLayersLive()
{
    Shown shown(QSize(300, 300), QSize(300, 300));
    EditorSession &session = shown.session;
    QImage red = BrushRaster::context(100, 100, false);
    red.fill(Qt::red);
    session.insert(ImportedImage(red, red, "Red"), QPointF(100, 100));
    session.groupSelectedLayers();
    session.addLayerMask(true);
    QVERIFY(session.isMaskSelected() && session.activeLayer().value().isGroup);
    shown.settle();
    session.selectTool(NavigationTool::brush);
    session.setBrushSettings(brush(20, 1, 0, 0, 0));
    session.setMaskPaintWhite(false);
    for (double zoom : {1.0, 3.0}) {
        session.zoom(zoom);
        shown.canvas->synchronizeDisplay();
        // Both points on screen at 3x: the view spans 100–200.
        const QSizeF size = session.document().value().size();
        const QPoint centre = session.viewport.viewPoint(QPointF(125, 125), size).toPoint();
        const QPoint edge = session.viewport.viewPoint(QPointF(105, 105), size).toPoint();
        QVERIFY(QRect(QPoint(0, 0), shown.canvas->size()).contains(edge));
        QCOMPARE(shown.canvas->grab().toImage().pixelColor(centre), QColor(Qt::red));
        // Black on the mask hides the red at once.
        session.beginBrush(QPointF(125, 125));
        QVERIFY(shown.canvas->synchronizeDisplay());
        QVERIFY(shown.canvas->grab().toImage().pixelColor(centre) != QColor(Qt::red));
        QCOMPARE(shown.canvas->grab().toImage().pixelColor(edge), QColor(Qt::red));
        session.finishBrush();
        shown.canvas->synchronizeDisplay();
        QVERIFY(shown.canvas->grab().toImage().pixelColor(centre) != QColor(Qt::red));
        session.undo();
        QVERIFY(session.isMaskSelected());
    }
}

void BrushCanvasTests::aStripedFolderMaskKeepsItsShadeWhilePainted()
{
    Shown shown(QSize(1200, 1200), QSize(300, 300));
    EditorSession &session = shown.session;
    QImage red = BrushRaster::context(1200, 1200, false);
    red.fill(Qt::red);
    session.insert(ImportedImage(red, red, "Red"));
    session.groupSelectedLayers();
    const QUuid folder = session.activeLayerID().value();
    // One-pixel stripes: halved at a quarter, they read gray.
    QImage stripes = BrushRaster::context(1200, 1200, true);
    for (int y = 0; y < 1200; ++y) {
        for (int x = 0; x < 1200; x += 2)
            stripes.scanLine(y)[x] = 255;
    }
    rewrite(session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, folder, LayerMask::assetFrom(stripes)); });
    session.selectLayerTarget(folder, true);
    QVERIFY(session.isMaskSelected());
    shown.settle();
    session.zoom(0.25);
    session.selectTool(NavigationTool::brush);
    session.setBrushSettings(brush(40, 1, 1, 1, 1));
    session.setMaskPaintWhite(true);
    shown.canvas->synchronizeDisplay();
    const QPoint far = session.viewport.viewPoint(QPointF(1000, 1000), session.document().value().size()).toPoint();
    const auto shade = [&] { return shown.canvas->grab().toImage().pixelColor(far); };
    const auto near = [](const QColor &a, const QColor &b) {
        return std::abs(a.red() - b.red()) <= 3 && std::abs(a.green() - b.green()) <= 3 && std::abs(a.blue() - b.blue()) <= 3;
    };
    const QColor before = shade();
    QVERIFY(before != QColor(Qt::red));
    // Far from each dab, the untouched shade stays put.
    for (const QPointF dab : {QPointF(100, 100), QPointF(100, 300)}) {
        session.beginBrush(dab);
        QVERIFY(shown.canvas->synchronizeDisplay());
        const QColor during = shade();
        QVERIFY2(near(before, during), qPrintable(QStringLiteral("%1 then %2").arg(before.name(), during.name())));
        session.finishBrush();
        shown.canvas->synchronizeDisplay();
        QVERIFY(near(before, shade()));
        // The second dab reads the tiles the first one painted.
        QVERIFY(layerWith(session, folder).mask.value().asset.raster);
    }
}

QTEST_MAIN(BrushCanvasTests)
#include "BrushCanvasTests.moc"
