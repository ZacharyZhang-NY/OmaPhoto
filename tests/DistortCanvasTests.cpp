#include "CanvasFixtures.h"
#include "Rendering/TransformOverlay.h"
#include "UI/TransformInspector.h"

// A distortion on the canvas: box, drags, cursor, preview.
namespace {
QImage image(const QCursor &cursor)
{
    return cursor.pixmap().toImage();
}

void hover(QWidget &widget, QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QMouseEvent event(QEvent::MouseMove, at, at, widget.mapToGlobal(at.toPoint()), Qt::NoButton, Qt::NoButton, modifiers);
    QApplication::sendEvent(&widget, &event);
}

// The document fills the canvas: view points are pixels.
struct Canvas : Shown {
    QUuid red;
    Canvas() : Shown(QSize(400, 300), QSize(400, 300))
    {
        settle();
        session.zoom(1);
        session.insert(filled(100, 100, qRgba(255, 0, 0, 255), "Red"));
        red = session.activeLayerID().value();
        session.selectTool(NavigationTool::move);
        canvas->synchronizeDisplay();
    }
    void press(QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier) { QTest::mousePress(canvas, Qt::LeftButton, modifiers, at.toPoint()); }
    void move(QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QMouseEvent event(QEvent::MouseMove, to, to, canvas->mapToGlobal(to.toPoint()), Qt::NoButton, Qt::LeftButton, modifiers);
        QApplication::sendEvent(canvas, &event);
    }
    void release(QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier) { QTest::mouseRelease(canvas, Qt::LeftButton, modifiers, at.toPoint()); }
    bool shows(const QCursor &cursor) const { return image(canvas->cursor()) == image(cursor); }
};
}

class DistortCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void aDistortionsGeometryHasNoGripAndHitsItsCornersAndEdges();
    void ctrlDraggingAHandleDistortsAndKeepsDistorting();
    void theDistortCursorShowsWithCtrlOrOnceDistorted();
    void thePreviewIsDrawnAndTheNumbersRest();
    void aDistortedLayerShowsThroughItsMask();
};

void DistortCanvasTests::aDistortionsGeometryHasNoGripAndHitsItsCornersAndEdges()
{
    CanvasViewport viewport;
    const QSizeF size(400, 300);
    viewport.resize(QSizeF(400, 300), 1, size);
    viewport.setZoom(1, QPointF(200, 150), size);
    QCOMPARE(viewport.viewPoint(QPointF(0, 0), size), QPointF(0, 0));
    const Corners corners = {QPointF(100, 100), QPointF(200, 100), QPointF(180, 160), QPointF(100, 160)};
    const TransformOverlayGeometry geometry(corners, viewport, size);
    QVERIFY(!geometry.showsRotation);
    QCOMPARE(geometry.handles[0], QPointF(100, 100));
    QCOMPARE(geometry.handles[1], QPointF(150, 100));
    QCOMPARE(geometry.handles[3], QPointF(190, 130));
    QCOMPARE(geometry.handles[6], QPointF(100, 160));
    QCOMPARE(geometry.handles[7], QPointF(100, 130));
    QCOMPARE(geometry.rotationHandle, geometry.handles[1]);
    // The top's middle is a handle, not the grip.
    const TransformDrag::Mode top = geometry.hit(QPointF(150, 100)).value();
    QVERIFY(top.kind == TransformDrag::Kind::resize);
    QCOMPARE(top.index, 1);
    QCOMPARE(geometry.hit(QPointF(185, 145)).value().index, 3);
    QVERIFY(!geometry.hit(QPointF(150, 130)).has_value());
    // A box's geometry keeps its grip; the two differ.
    const TransformOverlayGeometry box({.origin = {100, 100}, .size = {100, 60}}, viewport, size);
    QVERIFY(box.showsRotation);
    QVERIFY(!(box == geometry));
}

void DistortCanvasTests::ctrlDraggingAHandleDistortsAndKeepsDistorting()
{
    Canvas shown;
    EditorSession &session = shown.session;
    const double ratio = shown.canvas->devicePixelRatio();
    // Ctrl on the bottom right corner: that corner alone follows.
    shown.press(QPointF(250, 200), Qt::ControlModifier);
    QVERIFY(shown.shows(CanvasView::distortCursor(ratio)));
    shown.move(QPointF(260, 210), Qt::ControlModifier);
    shown.move(QPointF(270, 230), Qt::ControlModifier);
    const TransformEdit edit = session.transformEdit().value();
    QVERIFY(edit.persistent);
    QCOMPARE(edit.corners.value(), (Corners{QPointF(150, 100), QPointF(250, 100), QPointF(270, 230), QPointF(150, 200)}));
    QCOMPARE(edit.draft, layerWith(session, shown.red).transform);
    shown.release(QPointF(270, 230), Qt::ControlModifier);
    // The edit waits for Apply; its box has no grip.
    QVERIFY(session.transformEdit().has_value());
    const TransformOverlay overlay(session);
    QVERIFY(!overlay.geometry().value().showsRotation);
    QCOMPARE(overlay.geometry().value().handles[4], QPointF(270, 230));
    // Once distorted, plain drags distort too; Shift keeps an axis.
    shown.press(QPointF(150, 100));
    QVERIFY(shown.shows(CanvasView::distortCursor(ratio)));
    shown.move(QPointF(140, 105), Qt::ShiftModifier);
    shown.release(QPointF(140, 105), Qt::ShiftModifier);
    QCOMPARE(session.transformEdit().value().corners.value()[0], QPointF(140, 100));
    // An edge handle moves both its corners.
    shown.press(QPointF(198, 100));
    shown.move(QPointF(198, 90));
    shown.release(QPointF(198, 90));
    QCOMPARE(session.transformEdit().value().corners.value()[0], QPointF(140, 90));
    QCOMPARE(session.transformEdit().value().corners.value()[1], QPointF(250, 90));
    // Return applies: the pixels are resampled into the shape's bounds.
    QTRY_VERIFY(shown.canvas->hasFocus());
    QTest::keyClick(shown.canvas, Qt::Key_Return);
    QVERIFY(!session.transformEdit().has_value());
    QCOMPARE(session.history.undoName(), QString("Distort"));
    QCOMPARE(layerWith(session, shown.red).transform, (LayerTransform{.origin = {140, 90}, .size = {130, 140}}));
    QCOMPARE(layerWith(session, shown.red).asset.value().size(), QSize(130, 140));
    session.undo();
    QCOMPARE(layerWith(session, shown.red).transform.size, QSizeF(100, 100));
}

void DistortCanvasTests::theDistortCursorShowsWithCtrlOrOnceDistorted()
{
    Canvas shown;
    CanvasView &canvas = *shown.canvas;
    const double ratio = canvas.devicePixelRatio();
    hover(canvas, QPointF(150, 100));
    QCOMPARE(canvas.cursor().shape(), Qt::SizeFDiagCursor);
    hover(canvas, QPointF(150, 100), Qt::ControlModifier);
    QVERIFY(shown.shows(CanvasView::distortCursor(ratio)));
    // Ctrl off a handle picks: the move pointer stays.
    hover(canvas, QPointF(200, 150), Qt::ControlModifier);
    QVERIFY(shown.shows(CanvasView::moveCursor(ratio)));
    hover(canvas, QPointF(150, 100));
    QCOMPARE(canvas.cursor().shape(), Qt::SizeFDiagCursor);
    shown.session.beginTransform();
    shown.session.beginDistort();
    canvas.synchronizeDisplay();
    QVERIFY(shown.shows(CanvasView::distortCursor(ratio)));
    // The white arrow: light where the duplicate pointer is dark.
    QVERIFY(image(CanvasView::distortCursor(1)).pixelColor(8, 12).lightness() > 128);
    QVERIFY(image(CanvasView::duplicateCursor(1)).pixelColor(8, 12).lightness() < 128);
    shown.session.cancelTransform();
    canvas.synchronizeDisplay();
    QCOMPARE(canvas.cursor().shape(), Qt::SizeFDiagCursor);
}

void DistortCanvasTests::thePreviewIsDrawnAndTheNumbersRest()
{
    Shown shown;
    CanvasView &canvas = *shown.canvas;
    EditorSession &session = shown.session;
    shown.settle();
    session.zoom(1);
    session.insert(filled(20, 20, qRgba(255, 0, 0, 255), "Red"), QPointF(50, 50));
    QCOMPARE(session.viewport.viewPoint(QPointF(0, 0), shown.documentSize()), QPointF(150, 100));
    QVERIFY(canvas.synchronizeDisplay());
    TransformInspector inspector(session);
    QWidget &numbers = *inspector.findChild<QWidget *>("transformFields");
    QVERIFY(numbers.isEnabled());
    session.beginTransform();
    session.beginDistort();
    QVERIFY(!numbers.isEnabled());
    // The top right corner pulled out: the preview follows.
    session.previewCorners({QPointF(40, 40), QPointF(80, 40), QPointF(60, 60), QPointF(40, 60)});
    QVERIFY(canvas.synchronizeDisplay());
    QImage shot = canvas.grab().toImage();
    QCOMPARE(shot.pixelColor(215, 145), QColor(Qt::red));
    QVERIFY(shot.pixelColor(225, 155) != QColor(Qt::red));
    QCOMPARE(shot.pixelColor(197, 147), QColor(Qt::red));
    // The same corners again change nothing.
    QVERIFY(!canvas.synchronizeDisplay());
    session.cancelTransform();
    QVERIFY(canvas.synchronizeDisplay());
    QVERIFY(numbers.isEnabled());
    shot = canvas.grab().toImage();
    QVERIFY(shot.pixelColor(215, 145) != QColor(Qt::red));
    // An unlinked mask distorted alone shows in the layer's grid.
    const QUuid red = session.activeLayerID().value();
    QImage block(20, 20, QImage::Format_Grayscale8);
    block.fill(0);
    for (int y = 2; y < 18; ++y)
        std::fill_n(block.scanLine(y) + 2, 8, uchar(255));
    rewrite(session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, red, LayerMask::assetFrom(block));
        record(snapshot, red).maskLinked = false;
    });
    // Installing the snapshot fits the view: back to 100%.
    session.zoom(1);
    QCOMPARE(session.viewport.viewPoint(QPointF(0, 0), shown.documentSize()), QPointF(150, 100));
    session.selectTool(NavigationTool::move);
    session.selectLayerTarget(red, true);
    session.beginTransform();
    session.beginDistort();
    // The bottom left corner pulled out: the block leans left.
    session.previewCorners({QPointF(40, 40), QPointF(60, 40), QPointF(60, 60), QPointF(30, 60)});
    QVERIFY(session.transformEdit().value().mask);
    const std::optional<QImage> preview = session.maskDistortPreview(layerWith(session, red));
    QCOMPARE(int(preview.value().constScanLine(5)[5]), 255);
    QCOMPARE(int(preview.value().constScanLine(16)[16]), 0);
    canvas.synchronizeDisplay();
    shot = canvas.grab().toImage();
    QCOMPARE(shot.pixelColor(195, 145), QColor(Qt::red));
    QVERIFY(shot.pixelColor(199, 154) != QColor(Qt::red));
}

void DistortCanvasTests::aDistortedLayerShowsThroughItsMask()
{
    Shown shown;
    CanvasView &canvas = *shown.canvas;
    EditorSession &session = shown.session;
    shown.settle();
    session.insert(filled(20, 20, qRgba(255, 0, 0, 255), "Red"), QPointF(50, 50));
    const QUuid red = session.activeLayerID().value();
    // A linked mask hides the left half.
    QImage half(20, 20, QImage::Format_Grayscale8);
    half.fill(255);
    for (int y = 0; y < 20; ++y)
        std::fill_n(half.scanLine(y), 10, uchar(0));
    rewrite(session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, red, LayerMask::assetFrom(half)); });
    session.zoom(1);
    session.selectTool(NavigationTool::move);
    session.selectLayer(red);
    session.beginTransform();
    session.beginDistort();
    session.previewCorners({QPointF(40, 40), QPointF(62, 40), QPointF(60, 60), QPointF(40, 60)});
    canvas.synchronizeDisplay();
    // The document sits at (150, 100); probes miss the handles.
    const QImage shot = canvas.grab().toImage();
    QVERIFY(shot.pixelColor(196, 154) != QColor(Qt::red));
    QCOMPARE(shot.pixelColor(205, 146), QColor(Qt::red));
}

QTEST_MAIN(DistortCanvasTests)
#include "DistortCanvasTests.moc"
