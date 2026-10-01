#include "SelectionCanvasFixtures.h"

// Marquee and Lasso gestures on the canvas: drags, keys, focus.
class SelectionCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void marqueeDragsSelectShiftAddsAltSubtractsAndAFreshShiftSquares();
    void theFreehandLassoDragsAnOutlineAndClicksMoveOrDeselect();
    void thePolygonalLassoTakesCornersAndClosesByClickReturnOrDoubleClick();
    void arrowsNudgeTheOutlineAndTheKeysMAndLChooseTools();
    void focusLossEndsAMarqueeDraftAndAnOutlineMove();
    void shiftDeleteBeginsAContentAwareFill();
    void aCtrlDragMovesSelectedPixelsAndCtrlArrowsNudgeThem();
};

void SelectionCanvasTests::marqueeDragsSelectShiftAddsAltSubtractsAndAFreshShiftSquares()
{
    Canvas shown;
    EditorSession &session = shown.session;
    session.applySelection(rectPath(QRectF(5, 5, 10, 10)), SelectionMode::replace, "Select");
    // Shift held throughout: an add, 40–70 × 40–50, not squared.
    shown.drag(QPointF(40, 40), QPointF(70, 50), Qt::ShiftModifier);
    QCOMPARE(session.history.undoName(), QString("Rectangular Marquee"));
    QCOMPARE(shown.coverage(10, 10), 255);
    QCOMPARE(shown.coverage(65, 45), 255);
    QCOMPARE(shown.coverage(65, 60), 0);
    // Shift pressed afresh mid-drag: squared to 20–40 × 60–80.
    shown.press(QPointF(20, 60), Qt::ShiftModifier);
    shown.move(QPointF(30, 65), Qt::ShiftModifier);
    shown.move(QPointF(35, 68));
    shown.move(QPointF(40, 70), Qt::ShiftModifier);
    shown.release(QPointF(40, 70), Qt::ShiftModifier);
    QCOMPARE(shown.coverage(30, 75), 255);
    QCOMPARE(shown.coverage(65, 45), 255);
    QCOMPARE(shown.coverage(10, 10), 255);
    // Alt subtracts and never draws from the centre.
    session.selectAll();
    shown.drag(QPointF(40, 40), QPointF(60, 60), Qt::AltModifier);
    QCOMPARE(shown.coverage(50, 50), 0);
    QCOMPARE(shown.coverage(30, 30), 255);
    // A key change reshapes the box before the pointer moves.
    session.deselect();
    shown.press(QPointF(100, 100));
    shown.move(QPointF(130, 110));
    QCOMPARE(session.lassoDraft().value().points[2], QPointF(130, 110));
    QTest::keyPress(shown.window.windowHandle(), Qt::Key_Shift, Qt::ShiftModifier);
    QCOMPARE(session.lassoDraft().value().points[2], QPointF(130, 130));
    QTest::keyRelease(shown.window.windowHandle(), Qt::Key_Shift, Qt::NoModifier);
    QCOMPARE(session.lassoDraft().value().points[2], QPointF(130, 110));
    shown.release(QPointF(130, 110));
    QCOMPARE(bounds(session), QRectF(100, 100, 30, 10));
    // A click outside deselects; busy, a press draws nothing.
    shown.click(QPointF(200, 200));
    QVERIFY(!session.selection().has_value());
    session.setIsProjectBusy(true);
    shown.press(QPointF(10, 10));
    QVERIFY(!session.lassoDraft().has_value());
    session.setIsProjectBusy(false);
    shown.release(QPointF(10, 10));
}

void SelectionCanvasTests::theFreehandLassoDragsAnOutlineAndClicksMoveOrDeselect()
{
    Canvas shown;
    EditorSession &session = shown.session;
    session.selectTool(NavigationTool::lasso);
    shown.press(QPointF(10, 10));
    for (const QPointF point : {QPointF(90, 10), QPointF(90, 90), QPointF(10, 90)})
        shown.move(point);
    QCOMPARE(session.lassoDraft().value().points.size(), size_t(4));
    shown.release(QPointF(10, 90));
    QCOMPARE(session.history.undoName(), QString("Lasso"));
    QCOMPARE(shown.coverage(50, 50), 255);
    QCOMPARE(shown.coverage(95, 95), 0);
    // Inside, a drag moves the outline; Shift keeps one axis.
    shown.drag(QPointF(50, 50), QPointF(60, 55));
    QCOMPARE(session.history.undoName(), QString("Move Selection"));
    QCOMPARE(bounds(session), QRectF(20, 15, 80, 80));
    shown.press(QPointF(50, 50));
    shown.move(QPointF(70, 58), Qt::ShiftModifier);
    shown.release(QPointF(70, 58), Qt::ShiftModifier);
    QCOMPARE(bounds(session), QRectF(40, 15, 80, 80));
    // Shift at the press adds an outline instead of moving.
    shown.press(QPointF(50, 50), Qt::ShiftModifier);
    shown.move(QPointF(200, 200), Qt::ShiftModifier);
    shown.move(QPointF(200, 50), Qt::ShiftModifier);
    shown.release(QPointF(200, 50), Qt::ShiftModifier);
    QCOMPARE(session.history.undoName(), QString("Lasso"));
    QCOMPARE(bounds(session), QRectF(40, 15, 160, 185));
    // A click inside without a drag deselects, as outside.
    shown.click(QPointF(50, 50));
    QVERIFY(!session.selection().has_value());
    QCOMPARE(session.history.undoName(), QString("Deselect"));
    shown.click(QPointF(200, 200));
    QVERIFY(!session.selection().has_value());
}

void SelectionCanvasTests::thePolygonalLassoTakesCornersAndClosesByClickReturnOrDoubleClick()
{
    Canvas shown;
    EditorSession &session = shown.session;
    session.selectTool(NavigationTool::lasso);
    session.setLassoKind(LassoKind::polygonal);
    shown.click(QPointF(10, 10));
    shown.click(QPointF(90, 10));
    shown.click(QPointF(90, 90));
    QCOMPARE(session.lassoDraft().value().points.size(), size_t(3));
    // The rubber band follows the pointer; Backspace takes a corner.
    shown.hover(QPointF(50, 50));
    QCOMPARE(session.lassoDraft().value().cursor, std::optional(QPointF(50, 50)));
    QTRY_VERIFY(shown.canvas->hasFocus());
    QTest::keyClick(shown.canvas, Qt::Key_Backspace);
    QCOMPARE(session.lassoDraft().value().points.size(), size_t(2));
    shown.click(QPointF(90, 90));
    // Near the first corner, with three, a click closes.
    shown.click(QPointF(13, 12));
    QVERIFY(!session.lassoDraft().has_value());
    QCOMPARE(session.history.undoName(), QString("Polygonal Lasso"));
    QCOMPARE(shown.coverage(80, 40), 255);
    QCOMPARE(shown.coverage(20, 80), 0);
    // Return closes; Escape cancels; a double click closes.
    shown.click(QPointF(100, 100));
    QVERIFY(session.lassoDraft().has_value());
    shown.click(QPointF(200, 100));
    shown.click(QPointF(150, 200));
    QTest::keyClick(shown.canvas, Qt::Key_Return);
    QCOMPARE(shown.coverage(150, 130), 255);
    QCOMPARE(shown.coverage(50, 50), 0);
    shown.click(QPointF(10, 200));
    QTest::keyClick(shown.canvas, Qt::Key_Escape);
    QVERIFY(!session.lassoDraft().has_value());
    QCOMPARE(shown.coverage(150, 130), 255);
    shown.click(QPointF(10, 250));
    shown.click(QPointF(100, 250));
    // A double click's first press adds the closing corner.
    shown.click(QPointF(100, 290));
    QTest::mouseDClick(shown.canvas, Qt::LeftButton, Qt::NoModifier, QPoint(100, 290));
    QVERIFY(!session.lassoDraft().has_value());
    QCOMPARE(shown.coverage(70, 265), 255);
    // With two corners a click by the first adds one.
    shown.click(QPointF(300, 10));
    shown.click(QPointF(350, 10));
    shown.click(QPointF(302, 12));
    QCOMPARE(session.lassoDraft().value().points.size(), size_t(3));
    // A double click while Space pans closes nothing.
    QTest::keyPress(shown.canvas, Qt::Key_Space);
    QTest::mouseDClick(shown.canvas, Qt::LeftButton, Qt::NoModifier, QPoint(320, 40));
    QTest::mouseRelease(shown.canvas, Qt::LeftButton, Qt::NoModifier, QPoint(320, 40));
    QTest::keyRelease(shown.canvas, Qt::Key_Space);
    QCOMPARE(session.lassoDraft().value().points.size(), size_t(3));
    session.cancelLasso();
}

void SelectionCanvasTests::arrowsNudgeTheOutlineAndTheKeysMAndLChooseTools()
{
    Canvas shown;
    EditorSession &session = shown.session;
    session.applySelection(rectPath(QRectF(10, 10, 20, 20)), SelectionMode::replace, "Select");
    QTRY_VERIFY(shown.canvas->hasFocus());
    QTest::keyClick(shown.canvas, Qt::Key_Right);
    QTest::keyClick(shown.canvas, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(bounds(session), QRectF(11, 20, 20, 20));
    QCOMPARE(session.history.undoName(), QString("Move Selection"));
    // Alt-arrows are no nudge; a draft takes the arrows away.
    QTest::keyClick(shown.canvas, Qt::Key_Left, Qt::AltModifier);
    QCOMPARE(bounds(session), QRectF(11, 20, 20, 20));
    session.beginLasso(QPointF(1, 1), SelectionMode::replace);
    QTest::keyClick(shown.canvas, Qt::Key_Left);
    QCOMPARE(bounds(session), QRectF(11, 20, 20, 20));
    session.cancelLasso();
    // M and L choose their tools; repeats and Shift too.
    session.selectTool(NavigationTool::brush);
    QTest::keyClick(shown.canvas, Qt::Key_M);
    QVERIFY(session.tool() == NavigationTool::marquee && session.marqueeKind() == LassoKind::rectangle);
    QTest::keyClick(shown.canvas, Qt::Key_M, Qt::ShiftModifier);
    QVERIFY(session.tool() == NavigationTool::marquee && session.marqueeKind() == LassoKind::rectangle);
    session.setMarqueeKind(LassoKind::ellipse);
    QTest::keyClick(shown.canvas, Qt::Key_L);
    QVERIFY(session.tool() == NavigationTool::lasso && session.lassoKind() == LassoKind::freehand);
    QTest::keyClick(shown.canvas, Qt::Key_M);
    QVERIFY(session.tool() == NavigationTool::marquee && session.marqueeKind() == LassoKind::ellipse);
    // A held key's repeats choose nothing and end no draft.
    session.selectTool(NavigationTool::lasso);
    session.beginLasso(QPointF(1, 1), SelectionMode::replace);
    QKeyEvent repeat(QEvent::KeyPress, Qt::Key_M, Qt::NoModifier, QStringLiteral("m"), true);
    QApplication::sendEvent(shown.canvas, &repeat);
    QVERIFY(session.tool() == NavigationTool::lasso && session.lassoDraft().has_value());
    session.cancelLasso();
    // In the Move tool the arrows nudge the layer.
    session.insert(filled(10, 10, qRgba(255, 0, 0, 255), "Red"));
    session.selectTool(NavigationTool::move);
    QTest::keyClick(shown.canvas, Qt::Key_Right);
    QCOMPARE(bounds(session), QRectF(11, 20, 20, 20));
    QCOMPARE(session.history.undoName(), QString("Transform Layer"));
}

void SelectionCanvasTests::focusLossEndsAMarqueeDraftAndAnOutlineMove()
{
    Canvas shown;
    EditorSession &session = shown.session;
    shown.press(QPointF(50, 50));
    shown.move(QPointF(80, 80));
    shown.canvas->clearFocus();
    QVERIFY(!session.lassoDraft().has_value());
    shown.release(QPointF(80, 80));
    QVERIFY(!session.selection().has_value());
    // A polygonal draft survives; a move ends as a step.
    session.selectTool(NavigationTool::lasso);
    session.setLassoKind(LassoKind::polygonal);
    shown.click(QPointF(10, 10));
    shown.canvas->clearFocus();
    QVERIFY(session.lassoDraft().has_value());
    session.cancelLasso();
    session.selectAll();
    // Twenty across: out of the canvas edges' snapping reach.
    shown.press(QPointF(200, 150));
    shown.move(QPointF(220, 150));
    shown.canvas->clearFocus();
    QCOMPARE(session.history.undoName(), QString("Move Selection"));
    QVERIFY(session.canUndo());
    shown.release(QPointF(220, 150));
    QCOMPARE(bounds(session), QRectF(20, 0, 400, 300));
}

void SelectionCanvasTests::shiftDeleteBeginsAContentAwareFill()
{
    Canvas shown;
    EditorSession &session = shown.session;
    QImage field = BrushRaster::context(400, 300, false);
    field.fill(QColor(51, 153, 204));
    QPainter red(&field);
    red.fillRect(QRect(30, 30, 40, 40), Qt::red);
    red.end();
    session.insert(ImportedImage(field, field, "Field"));
    QTRY_VERIFY(shown.canvas->hasFocus());
    // Without a selection the keys do nothing.
    QTest::keyClick(shown.canvas, Qt::Key_Backspace, Qt::ShiftModifier);
    QVERIFY(!session.filterEdit().has_value());
    shown.drag(QPointF(20, 20), QPointF(80, 80));
    QTest::keyClick(shown.canvas, Qt::Key_Backspace, Qt::ShiftModifier);
    QVERIFY(session.filterEdit().has_value());
    QCOMPARE(session.filterEdit().value().kind, FilterKind::contentAwareFill);
    session.cancelFilter();
    QTest::keyClick(shown.canvas, Qt::Key_Delete, Qt::ShiftModifier);
    QVERIFY(session.filterEdit().has_value());
    // The canvas repaints once the preview lands.
    QTRY_VERIFY(!session.filterEdit().value().preparing);
    QVERIFY(shown.canvas->synchronizeDisplay());
    QVERIFY(!shown.canvas->synchronizeDisplay());
    // The preview stands in: the red block is gone.
    const QColor shown_pixel = shown.canvas->grab().toImage().pixelColor(50, 50);
    QVERIFY(std::abs(shown_pixel.red() - 51) <= 2 && std::abs(shown_pixel.blue() - 204) <= 2);
    session.cancelFilter();
    QCOMPARE(shown.canvas->grab().toImage().pixelColor(50, 50), QColor(Qt::red));
    // Ctrl with it is no fill.
    QTest::keyClick(shown.canvas, Qt::Key_Backspace, Qt::ShiftModifier | Qt::ControlModifier);
    QVERIFY(!session.filterEdit().has_value());
}

void SelectionCanvasTests::aCtrlDragMovesSelectedPixelsAndCtrlArrowsNudgeThem()
{
    Canvas shown;
    EditorSession &session = shown.session;
    QImage halves = BrushRaster::context(400, 300, false);
    QPainter painter(&halves);
    painter.fillRect(QRect(0, 0, 200, 300), Qt::red);
    painter.fillRect(QRect(200, 0, 200, 300), Qt::blue);
    painter.end();
    session.insert(ImportedImage(halves, halves, "Halves"));
    session.selectTool(NavigationTool::marquee);
    session.applySelection(rectPath(QRectF(20, 20, 40, 40)), SelectionMode::replace, "Select");
    shown.canvas->synchronizeDisplay();
    const int count = session.history.undoCount();
    // Ctrl inside: the pixels lift and follow the pointer.
    shown.press(QPointF(40, 40), Qt::ControlModifier);
    QVERIFY(session.pixelMove());
    shown.move(QPointF(260, 40), Qt::ControlModifier);
    const QImage moving = shown.canvas->grab().toImage();
    QCOMPARE(moving.pixelColor(260, 40), QColor(Qt::red));
    QVERIFY(moving.pixelColor(40, 40) != QColor(Qt::red));
    QCOMPARE(session.displayedSelection().value().path.boundingRect(), QRectF(240, 20, 40, 40));
    shown.release(QPointF(260, 40), Qt::ControlModifier);
    QTRY_VERIFY(!session.pixelMove());
    QCOMPARE(session.history.undoCount(), count + 1);
    QCOMPARE(session.history.undoName(), QString("Move Pixels"));
    // Ctrl-arrow in any tool, ten with Shift.
    session.selectTool(NavigationTool::brush);
    QTRY_VERIFY(shown.canvas->hasFocus());
    QTest::keyClick(shown.canvas, Qt::Key_Right, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_COMPARE(session.history.undoCount(), count + 2);
    QCOMPARE(session.selection().value().path.boundingRect(), QRectF(250, 20, 40, 40));
    // With Alt the arrow is not the nudge's.
    QTest::keyClick(shown.canvas, Qt::Key_Right, Qt::ControlModifier | Qt::AltModifier);
    QCOMPARE(session.history.undoCount(), count + 2);
    // Ctrl-Alt copies; losing the keys mid-drag cancels.
    session.selectTool(NavigationTool::marquee);
    // The nudge's move ends a turn after its step lands.
    QTRY_VERIFY(!session.pixelMove());
    shown.press(QPointF(270, 40), Qt::ControlModifier | Qt::AltModifier);
    QVERIFY(session.pixelMove() && session.pixelMove()->duplicate);
    shown.move(QPointF(300, 40), Qt::ControlModifier | Qt::AltModifier);
    QFocusEvent out(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
    QApplication::sendEvent(shown.canvas, &out);
    QVERIFY(!session.pixelMove());
    shown.release(QPointF(300, 40));
    QCOMPARE(session.history.undoCount(), count + 2);
    // Ctrl outside the selection lifts nothing.
    shown.press(QPointF(380, 280), Qt::ControlModifier);
    QVERIFY(!session.pixelMove());
    shown.release(QPointF(380, 280), Qt::ControlModifier);
    // An open outline keeps the pixels still under Ctrl-arrows.
    session.applySelection(rectPath(QRectF(250, 20, 40, 40)), SelectionMode::replace, "Select");
    QVERIFY(session.selection().has_value());
    session.selectTool(NavigationTool::lasso);
    session.setLassoKind(LassoKind::polygonal);
    shown.click(QPointF(300, 200));
    QVERIFY(session.lassoDraft().has_value());
    const int drafting = session.history.undoCount();
    QTest::keyClick(shown.canvas, Qt::Key_Right, Qt::ControlModifier);
    QTest::qWait(50);
    QCOMPARE(session.history.undoCount(), drafting);
    QVERIFY(session.lassoDraft().has_value() && !session.pixelMove());
}

QTEST_MAIN(SelectionCanvasTests)
#include "SelectionCanvasTests.moc"
