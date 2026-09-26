#include "CanvasFixtures.h"

// The Move tool's pointer: move, duplicate, the handles, the grip.
namespace {
QImage image(const QCursor &cursor)
{
    return cursor.pixmap().toImage();
}

// A hover with these modifiers, no button held.
void hover(QWidget &widget, QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QMouseEvent event(QEvent::MouseMove, at, at, widget.mapToGlobal(at.toPoint()), Qt::NoButton, Qt::NoButton, modifiers);
    QApplication::sendEvent(&widget, &event);
}

struct Canvas : Shown {
    Canvas() : Shown(QSize(400, 300), QSize(400, 300))
    {
        settle();
        session.zoom(1);
        session.insert(filled(100, 100, qRgba(255, 0, 0, 255), "Red"));
        session.selectTool(NavigationTool::move);
        canvas->synchronizeDisplay();
    }
    bool shows(const QCursor &cursor) const { return image(canvas->cursor()) == image(cursor); }
};
}

class CursorTests : public QObject {
    Q_OBJECT
private slots:
    void duplicateAndResizeCursors();
    void hiddenTransformControlsLeaveOnlyMoving();
    void aDragKeepsItsCursorAndBusySpaceAndOtherToolsTheirs();
    void ctrlOverTheSelectionShowsTheScissors();
};

void CursorTests::duplicateAndResizeCursors()
{
    Canvas shown;
    CanvasView &canvas = *shown.canvas;
    const double ratio = canvas.devicePixelRatio();
    hover(canvas, QPointF(200, 150));
    QVERIFY(shown.shows(CanvasView::moveCursor(ratio)));
    hover(canvas, QPointF(200, 150), Qt::AltModifier);
    QVERIFY(shown.shows(CanvasView::duplicateCursor(ratio)));
    // A press anywhere drags the active layer: Alt anywhere duplicates.
    hover(canvas, QPointF(20, 20), Qt::AltModifier);
    QVERIFY(shown.shows(CanvasView::duplicateCursor(ratio)));
    hover(canvas, QPointF(20, 20));
    QVERIFY(shown.shows(CanvasView::moveCursor(ratio)));
    // Alt from the keyboard counts too.
    QTest::keyPress(&canvas, Qt::Key_Alt, Qt::AltModifier);
    QVERIFY(shown.shows(CanvasView::duplicateCursor(ratio)));
    QTest::keyRelease(&canvas, Qt::Key_Alt);
    QVERIFY(shown.shows(CanvasView::moveCursor(ratio)));
    // Corners and edges resize with the box; the grip rotates.
    hover(canvas, QPointF(150, 100));
    QCOMPARE(canvas.cursor().shape(), Qt::SizeFDiagCursor);
    hover(canvas, QPointF(250, 100));
    QCOMPARE(canvas.cursor().shape(), Qt::SizeBDiagCursor);
    hover(canvas, QPointF(250, 150));
    QCOMPARE(canvas.cursor().shape(), Qt::SizeHorCursor);
    hover(canvas, QPointF(200, 200));
    QCOMPARE(canvas.cursor().shape(), Qt::SizeVerCursor);
    hover(canvas, QPointF(200, 72));
    QVERIFY(shown.shows(CanvasView::rotationCursor(ratio)));
    // The move pointer carries a badge the plain arrow lacks.
    QVERIFY(image(CanvasView::moveCursor(1)).pixelColor(19, 21).lightness() < 128);
    QVERIFY(image(CanvasView::rotationCursor(1)).pixelColor(12, 5).lightness() < 128);
    QVERIFY(image(CanvasView::rotationCursor(1)).pixelColor(12, 12).alpha() == 0);
    // A hidden active layer moves nothing; Ctrl picks red.
    QVERIFY(canvas.hasMouseTracking());
    const QUuid red = shown.session.activeLayerID().value();
    shown.session.insert(filled(10, 10, qRgba(0, 0, 255, 255), "Blue"), QPointF(50, 250));
    const QUuid blue = shown.session.activeLayerID().value();
    shown.session.toggleLayerVisibility(blue);
    canvas.synchronizeDisplay();
    hover(canvas, QPointF(200, 150));
    QCOMPARE(canvas.cursor().shape(), Qt::ArrowCursor);
    hover(canvas, QPointF(200, 150), Qt::ControlModifier);
    QVERIFY(shown.shows(CanvasView::moveCursor(ratio)));
    hover(canvas, QPointF(200, 150));
    QCOMPARE(canvas.cursor().shape(), Qt::ArrowCursor);
    QTest::keyPress(&canvas, Qt::Key_Control, Qt::ControlModifier);
    QVERIFY(shown.shows(CanvasView::moveCursor(ratio)));
    QTest::keyRelease(&canvas, Qt::Key_Control);
    QCOMPARE(canvas.cursor().shape(), Qt::ArrowCursor);
    // Released while hidden, Ctrl is read again on show.
    QTest::keyPress(shown.window.windowHandle(), Qt::Key_Control, Qt::ControlModifier);
    QVERIFY(shown.shows(CanvasView::moveCursor(ratio)));
    canvas.hide();
    QTest::keyRelease(shown.window.windowHandle(), Qt::Key_Control);
    canvas.show();
    QCOMPARE(canvas.cursor().shape(), Qt::ArrowCursor);
    // With no layer at all, the plain arrow.
    shown.session.selectLayers({red, blue}, red);
    shown.session.deleteSelectedLayers();
    QVERIFY(shown.session.document().value().layers.empty());
    hover(canvas, QPointF(200, 150), Qt::ControlModifier);
    QCOMPARE(canvas.cursor().shape(), Qt::ArrowCursor);
}

void CursorTests::hiddenTransformControlsLeaveOnlyMoving()
{
    Canvas shown;
    CanvasView &canvas = *shown.canvas;
    EditorSession &session = shown.session;
    const double ratio = canvas.devicePixelRatio();
    QVERIFY(session.showsTransformControls());
    hover(canvas, QPointF(150, 100));
    QCOMPARE(canvas.cursor().shape(), Qt::SizeFDiagCursor);
    session.setShowsTransformControls(false);
    canvas.synchronizeDisplay();
    QVERIFY(shown.shows(CanvasView::moveCursor(ratio)));
    hover(canvas, QPointF(150, 100), Qt::ControlModifier);
    QVERIFY(shown.shows(CanvasView::moveCursor(ratio)));
    hover(canvas, QPointF(150, 100));
    // A pending Ctrl+T transform shows its box again.
    session.beginTransform(true);
    canvas.synchronizeDisplay();
    QCOMPARE(canvas.cursor().shape(), Qt::SizeFDiagCursor);
    session.cancelTransform();
    canvas.synchronizeDisplay();
    QVERIFY(shown.shows(CanvasView::moveCursor(ratio)));
}

void CursorTests::aDragKeepsItsCursorAndBusySpaceAndOtherToolsTheirs()
{
    Canvas shown;
    CanvasView &canvas = *shown.canvas;
    EditorSession &session = shown.session;
    const double ratio = canvas.devicePixelRatio();
    // Over a handle mid-drag, the drag's own cursor stays.
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
    QVERIFY(shown.shows(CanvasView::moveCursor(ratio)));
    drag(canvas, QPointF(150, 100));
    QVERIFY(shown.shows(CanvasView::moveCursor(ratio)));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(150, 100));
    QVERIFY(shown.shows(CanvasView::moveCursor(ratio)));
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::AltModifier, QPoint(20, 20));
    QVERIFY(shown.shows(CanvasView::duplicateCursor(ratio)));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::AltModifier, QPoint(20, 20));
    hover(canvas, QPointF(20, 20));
    QVERIFY(shown.shows(CanvasView::moveCursor(ratio)));
    // The grip's drag keeps the rotation pointer off the grip.
    session.undo();
    canvas.synchronizeDisplay();
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(200, 72));
    QVERIFY(shown.shows(CanvasView::rotationCursor(ratio)));
    drag(canvas, QPointF(300, 150));
    QVERIFY(shown.shows(CanvasView::rotationCursor(ratio)));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(300, 150));
    QVERIFY(!shown.shows(CanvasView::rotationCursor(ratio)));
    session.undo();
    // Busy shows the arrow, edit or not; Space the hand.
    session.beginTransform();
    canvas.synchronizeDisplay();
    QVERIFY(shown.shows(CanvasView::moveCursor(ratio)));
    session.setIsProjectBusy(true);
    canvas.synchronizeDisplay();
    QCOMPARE(canvas.cursor().shape(), Qt::ArrowCursor);
    session.setIsProjectBusy(false);
    session.cancelTransform();
    canvas.synchronizeDisplay();
    QTRY_VERIFY(canvas.hasFocus());
    QTest::keyPress(&canvas, Qt::Key_Space);
    QCOMPARE(canvas.cursor().shape(), Qt::OpenHandCursor);
    QTest::keyRelease(&canvas, Qt::Key_Space);
    QVERIFY(shown.shows(CanvasView::moveCursor(ratio)));
    session.selectTool(NavigationTool::brush);
    canvas.synchronizeDisplay();
    QCOMPARE(canvas.cursor().shape(), Qt::CrossCursor);
    // Space mid-resize leaves the drag its cursor.
    session.selectTool(NavigationTool::move);
    canvas.synchronizeDisplay();
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(250, 200));
    QCOMPARE(canvas.cursor().shape(), Qt::SizeFDiagCursor);
    QTest::keyPress(&canvas, Qt::Key_Space);
    QCOMPARE(canvas.cursor().shape(), Qt::SizeFDiagCursor);
    QTest::keyRelease(&canvas, Qt::Key_Space);
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(250, 200));
}

void CursorTests::ctrlOverTheSelectionShowsTheScissors()
{
    Canvas shown;
    QWidget &canvas = *shown.canvas;
    const double ratio = canvas.devicePixelRatioF();
    shown.session.selectTool(NavigationTool::marquee);
    shown.session.selectAll();
    shown.canvas->synchronizeDisplay();
    hover(canvas, QPointF(200, 150), Qt::ControlModifier);
    QVERIFY(shown.shows(CanvasView::movePixelsCursor(ratio)));
    hover(canvas, QPointF(200, 150), Qt::ControlModifier | Qt::AltModifier);
    QVERIFY(shown.shows(CanvasView::duplicateCursor(ratio)));
    hover(canvas, QPointF(200, 150));
    QVERIFY(shown.shows(CanvasView::moveSelectionCursor(ratio)));
    // Scissors hang below the arrow, unlike the move badge.
    QVERIFY(image(CanvasView::movePixelsCursor(1)) != image(CanvasView::moveCursor(1)));
    const QColor scissors = image(CanvasView::movePixelsCursor(1)).pixelColor(13, 23);
    QVERIFY(scissors.alpha() > 128 && scissors.lightness() < 128);
    // Mid-drag the scissors stay, Ctrl held or not.
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::ControlModifier, QPoint(200, 150));
    QVERIFY(shown.session.pixelMove() && !shown.session.pixelMove()->duplicate);
    QMouseEvent drag(QEvent::MouseMove, QPointF(220, 150), QPointF(220, 150), canvas.mapToGlobal(QPoint(220, 150)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &drag);
    QVERIFY(shown.shows(CanvasView::movePixelsCursor(ratio)));
    // Space mid-drag keeps the drag's own cursor.
    QTest::keyPress(&canvas, Qt::Key_Space);
    QVERIFY(shown.shows(CanvasView::movePixelsCursor(ratio)));
    QTest::keyRelease(&canvas, Qt::Key_Space);
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(220, 150));
    QTRY_VERIFY(!shown.session.pixelMove());
    QCOMPARE(shown.session.history.undoName(), QString("Move Pixels"));
    // Released, the plain pointer moves the outline again.
    hover(canvas, QPointF(220, 150));
    QVERIFY(shown.shows(CanvasView::moveSelectionCursor(ratio)));
    // A copy drag keeps its cursor under Space too.
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::ControlModifier | Qt::AltModifier, QPoint(220, 150));
    QVERIFY(shown.session.pixelMove() && shown.session.pixelMove()->duplicate);
    QTest::keyPress(&canvas, Qt::Key_Space);
    QVERIFY(shown.shows(CanvasView::duplicateCursor(ratio)));
    QTest::keyRelease(&canvas, Qt::Key_Space);
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(220, 150));
    QTRY_VERIFY(!shown.session.pixelMove());
}

QTEST_MAIN(CursorTests)
#include "CursorTests.moc"
