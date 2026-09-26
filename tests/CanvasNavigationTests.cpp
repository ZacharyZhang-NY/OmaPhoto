#include "CanvasFixtures.h"
#include <QSignalSpy>

// The canvas: Zoom, Hand, Space, the wheel, a pinch.
class CanvasNavigationTests : public QObject {
    Q_OBJECT
private slots:
    void theZoomToolClicksAndDrags();
    void theHandToolAndSpaceDragTheView();
    void theWheelPansAndZoomsWithAModifier();
    void aPinchZoomsAboutThePointer();
};

void CanvasNavigationTests::theZoomToolClicksAndDrags()
{
    Shown shown;
    CanvasView &canvas = *shown.canvas;
    EditorSession &session = shown.session;
    shown.settle();
    session.zoom(1);
    session.selectTool(NavigationTool::zoom);
    const QSizeF size = shown.documentSize();
    const QPointF click(120, 90);
    const QPointF under = session.viewport.documentPoint(click, size);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, click.toPoint());
    QCOMPARE(session.viewport.zoom(), 2.0);
    QCOMPARE(session.viewport.documentPoint(click, size), under);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::AltModifier, click.toPoint());
    QCOMPARE(session.viewport.zoom(), 1.0);
    // Dragging 100 points doubles about the start; release adds nothing.
    const QPointF start(260, 110);
    const QPointF anchor = session.viewport.documentPoint(start, size);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, start.toPoint());
    drag(canvas, QPointF(360, 110));
    QCOMPARE(session.viewport.zoom(), 2.0);
    QCOMPARE(session.viewport.documentPoint(start, size), anchor);
    drag(canvas, QPointF(310, 110));
    QVERIFY(qFuzzyCompare(session.viewport.zoom(), std::sqrt(2.0)));
    QCOMPARE(session.viewport.documentPoint(start, size), anchor);
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(310, 110));
    QVERIFY(qFuzzyCompare(session.viewport.zoom(), std::sqrt(2.0)));
    // Two points of drift still count as a click.
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    drag(canvas, QPointF(102, 100));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(102, 100));
    QVERIFY(qFuzzyCompare(session.viewport.zoom(), 2 * std::sqrt(2.0)));
    // Busy, importing or without a document a press does nothing.
    session.setIsProjectBusy(true);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, click.toPoint());
    QVERIFY(qFuzzyCompare(session.viewport.zoom(), 2 * std::sqrt(2.0)));
    session.setIsProjectBusy(false);
    session.setIsImporting(true);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, click.toPoint());
    QVERIFY(qFuzzyCompare(session.viewport.zoom(), 2 * std::sqrt(2.0)));
    session.setIsImporting(false);
    QTest::mouseClick(&canvas, Qt::RightButton, Qt::NoModifier, click.toPoint());
    QVERIFY(qFuzzyCompare(session.viewport.zoom(), 2 * std::sqrt(2.0)));
    Shown empty(std::nullopt);
    empty.session.selectTool(NavigationTool::zoom);
    QTest::mouseClick(empty.canvas, Qt::LeftButton, Qt::NoModifier, click.toPoint());
    QCOMPARE(empty.session.viewport.zoom(), 1.0);
}

void CanvasNavigationTests::theHandToolAndSpaceDragTheView()
{
    Shown shown;
    CanvasView &canvas = *shown.canvas;
    EditorSession &session = shown.session;
    shown.settle();
    session.zoom(1);
    session.selectTool(NavigationTool::hand);
    canvas.synchronizeDisplay();
    QCOMPARE(canvas.cursor().shape(), Qt::OpenHandCursor);
    QSignalSpy changes(&session, &EditorSession::changed);
    const QSizeF pan = session.viewport.pan;
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    QCOMPARE(canvas.cursor().shape(), Qt::ClosedHandCursor);
    drag(canvas, QPointF(130, 120));
    QCOMPARE(session.viewport.pan, pan + QSizeF(30, 20));
    QVERIFY(!session.viewport.followsFit());
    QCOMPARE(changes.count(), 1);
    drag(canvas, QPointF(135, 120));
    QCOMPARE(session.viewport.pan, pan + QSizeF(35, 20));
    QCOMPARE(changes.count(), 2);
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(135, 120));
    QCOMPARE(canvas.cursor().shape(), Qt::OpenHandCursor);
    drag(canvas, QPointF(150, 120));
    QCOMPARE(session.viewport.pan, pan + QSizeF(35, 20));
    // Space drags the view in any tool.
    session.selectTool(NavigationTool::move);
    canvas.synchronizeDisplay();
    QCOMPARE(canvas.cursor().shape(), Qt::ArrowCursor);
    QTRY_VERIFY(canvas.hasFocus());
    QTest::keyPress(&canvas, Qt::Key_Space);
    QCOMPARE(canvas.cursor().shape(), Qt::OpenHandCursor);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
    drag(canvas, QPointF(60, 70));
    QCOMPARE(session.viewport.pan, pan + QSizeF(45, 40));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(60, 70));
    QTest::keyRelease(&canvas, Qt::Key_Space);
    QCOMPARE(canvas.cursor().shape(), Qt::ArrowCursor);
    // Importing, a press starts no drag.
    session.selectTool(NavigationTool::hand);
    session.setIsImporting(true);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
    drag(canvas, QPointF(90, 90));
    QCOMPARE(session.viewport.pan, pan + QSizeF(45, 40));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(90, 90));
}

void CanvasNavigationTests::theWheelPansAndZoomsWithAModifier()
{
    Shown shown;
    CanvasView &canvas = *shown.canvas;
    EditorSession &session = shown.session;
    shown.settle();
    session.zoom(1);
    const QSizeF size = shown.documentSize();
    const QPointF pointer(120, 90);
    const double lines = QApplication::wheelScrollLines();
    QSizeF pan = session.viewport.pan;
    // Pixels count as they are, and win over notches.
    wheel(canvas, pointer, QPoint(0, -30), QPoint(0, -240), Qt::NoModifier);
    pan += QSizeF(0, -30);
    QCOMPARE(session.viewport.pan, pan);
    QVERIFY(!session.viewport.followsFit());
    // A notch is a few lines, twelve points each.
    wheel(canvas, pointer, QPoint(), QPoint(0, 120), Qt::NoModifier);
    pan += QSizeF(0, 12 * lines);
    QCOMPARE(session.viewport.pan, pan);
    wheel(canvas, pointer, QPoint(), QPoint(-60, 0), Qt::NoModifier);
    pan += QSizeF(-6 * lines, 0);
    QCOMPARE(session.viewport.pan, pan);
    // Ctrl or Alt zooms about the pointer, by Swift's curve.
    const QPointF under = session.viewport.documentPoint(pointer, size);
    wheel(canvas, pointer, QPoint(), QPoint(0, 120), Qt::ControlModifier);
    QVERIFY(qFuzzyCompare(session.viewport.zoom(), std::exp(-lines * 0.015)));
    QCOMPARE(session.viewport.documentPoint(pointer, size), under);
    wheel(canvas, pointer, QPoint(0, 10), QPoint(), Qt::AltModifier);
    QVERIFY(qFuzzyCompare(session.viewport.zoom(), std::exp(-lines * 0.015) * std::exp(-10 * 0.015)));
    QCOMPARE(session.viewport.documentPoint(pointer, size), under);
    // Without a document the wheel does nothing.
    Shown empty(std::nullopt);
    wheel(*empty.canvas, pointer, QPoint(0, -30), QPoint(), Qt::NoModifier);
    QCOMPARE(empty.session.viewport.pan, QSizeF(0, 0));
    wheel(*empty.canvas, pointer, QPoint(), QPoint(0, 120), Qt::ControlModifier);
    QCOMPARE(empty.session.viewport.zoom(), 1.0);
}

void CanvasNavigationTests::aPinchZoomsAboutThePointer()
{
    Shown shown;
    shown.settle();
    shown.session.zoom(1);
    const QSizeF size = shown.documentSize();
    const QPointF pointer(120, 90);
    const QPointF under = shown.session.viewport.documentPoint(pointer, size);
    QNativeGestureEvent pinch(Qt::ZoomNativeGesture, QPointingDevice::primaryPointingDevice(), 2, pointer, pointer,
                              shown.canvas->mapToGlobal(pointer.toPoint()), 0.5, QPointF());
    QVERIFY(QApplication::sendEvent(shown.canvas, &pinch));
    QCOMPARE(shown.session.viewport.zoom(), 1.5);
    QCOMPARE(shown.session.viewport.documentPoint(pointer, size), under);
    // Other gestures pass on.
    QNativeGestureEvent swipe(Qt::SwipeNativeGesture, QPointingDevice::primaryPointingDevice(), 3, pointer, pointer,
                              shown.canvas->mapToGlobal(pointer.toPoint()), 0.5, QPointF());
    QApplication::sendEvent(shown.canvas, &swipe);
    QCOMPARE(shown.session.viewport.zoom(), 1.5);
}

QTEST_MAIN(CanvasNavigationTests)
#include "CanvasNavigationTests.moc"
