#include "SelectionCanvasFixtures.h"

// What the selection tools show: cursors, ants, autoscroll, drawing.
class SelectionOverlayTests : public QObject {
    Q_OBJECT
private slots:
    void cursorsFollowTheToolTheKindTheModeAndTheOutline();
    void theAntsMarchWhileASelectionShowsAndStopWithout();
    void autoscrollPansTheViewWhileADragHoldsAtTheEdge();
    void theOutlineAndTheDraftAreDrawnWhereTheyLie();
    void outlinesAndDraftsAreAntialiased();
    void theAntsDashesKeepFourOnFourOffAtTwoTimes();
    void openDraftsAndCursorBadgesEndWhereTheyEnd();
};

void SelectionOverlayTests::cursorsFollowTheToolTheKindTheModeAndTheOutline()
{
    Canvas shown;
    EditorSession &session = shown.session;
    CanvasView &canvas = *shown.canvas;
    const double ratio = canvas.devicePixelRatio();
    shown.hover(QPointF(200, 150));
    QVERIFY(shown.shows(CanvasView::selectionCursor(SelectionIcon::rectangleMarquee, SelectionMode::replace, ratio)));
    shown.hover(QPointF(200, 150), Qt::ShiftModifier);
    QVERIFY(shown.shows(CanvasView::selectionCursor(SelectionIcon::rectangleMarquee, SelectionMode::add, ratio)));
    QVERIFY(session.displayedSelectionMode() == SelectionMode::add);
    shown.hover(QPointF(200, 150), Qt::AltModifier | Qt::ShiftModifier);
    QVERIFY(shown.shows(CanvasView::selectionCursor(SelectionIcon::rectangleMarquee, SelectionMode::subtract, ratio)));
    // A key from anywhere in the window changes the badge.
    QTest::keyPress(shown.window.windowHandle(), Qt::Key_Shift, Qt::ShiftModifier);
    QVERIFY(shown.shows(CanvasView::selectionCursor(SelectionIcon::rectangleMarquee, SelectionMode::add, ratio)));
    QVERIFY(session.displayedSelectionMode() == SelectionMode::add);
    QTest::keyRelease(shown.window.windowHandle(), Qt::Key_Shift, Qt::NoModifier);
    QVERIFY(shown.shows(CanvasView::selectionCursor(SelectionIcon::rectangleMarquee, SelectionMode::replace, ratio)));
    session.setMarqueeKind(LassoKind::ellipse);
    canvas.synchronizeDisplay();
    QVERIFY(shown.shows(CanvasView::selectionCursor(SelectionIcon::ellipseMarquee, SelectionMode::replace, ratio)));
    session.selectTool(NavigationTool::lasso);
    canvas.synchronizeDisplay();
    QVERIFY(shown.shows(CanvasView::selectionCursor(SelectionIcon::freehandLasso, SelectionMode::replace, ratio)));
    session.setLassoKind(LassoKind::polygonal);
    canvas.synchronizeDisplay();
    QVERIFY(shown.shows(CanvasView::selectionCursor(SelectionIcon::polygonalLasso, SelectionMode::replace, ratio)));
    session.selectTool(NavigationTool::wand);
    canvas.synchronizeDisplay();
    QVERIFY(shown.shows(CanvasView::wandCursor(SelectionMode::replace, ratio)));
    shown.hover(QPointF(200, 150), Qt::AltModifier);
    QVERIFY(shown.shows(CanvasView::wandCursor(SelectionMode::subtract, ratio)));
    // Icons and badges differ; the hot spot is the cross.
    QVERIFY(image(CanvasView::selectionCursor(SelectionIcon::rectangleMarquee, SelectionMode::replace, 1)) != image(CanvasView::selectionCursor(SelectionIcon::ellipseMarquee, SelectionMode::replace, 1)));
    QVERIFY(image(CanvasView::selectionCursor(SelectionIcon::freehandLasso, SelectionMode::replace, 1)) != image(CanvasView::selectionCursor(SelectionIcon::polygonalLasso, SelectionMode::replace, 1)));
    QVERIFY(image(CanvasView::selectionCursor(SelectionIcon::freehandLasso, SelectionMode::add, 1)) != image(CanvasView::selectionCursor(SelectionIcon::freehandLasso, SelectionMode::subtract, 1)));
    QVERIFY(image(CanvasView::selectionCursor(SelectionIcon::freehandLasso, SelectionMode::replace, 1)) != image(CanvasView::selectionCursor(SelectionIcon::freehandLasso, SelectionMode::subtract, 1)));
    QCOMPARE(CanvasView::selectionCursor(SelectionIcon::freehandLasso, SelectionMode::replace, 1).hotSpot(), QPoint(8, 8));
    QVERIFY(image(CanvasView::selectionCursor(SelectionIcon::freehandLasso, SelectionMode::replace, 1)).pixelColor(8, 8).lightness() < 128);
    QCOMPARE(CanvasView::wandCursor(SelectionMode::replace, 1).hotSpot(), QPoint(7, 7));
    QVERIFY(image(CanvasView::wandCursor(SelectionMode::replace, 1)).pixelColor(20, 20).lightness() < 60);
    QCOMPARE(image(CanvasView::wandCursor(SelectionMode::replace, 1)).pixelColor(20, 20).alpha(), 255);
    // Over the selection in New mode, the move pointer.
    session.selectTool(NavigationTool::marquee);
    session.selectAll();
    canvas.synchronizeDisplay();
    shown.hover(QPointF(200, 150));
    QVERIFY(shown.shows(CanvasView::moveSelectionCursor(ratio)));
    shown.hover(QPointF(200, 150), Qt::ShiftModifier);
    QVERIFY(shown.shows(CanvasView::selectionCursor(SelectionIcon::ellipseMarquee, SelectionMode::add, ratio)));
    shown.press(QPointF(200, 150));
    shown.move(QPointF(210, 150), Qt::ShiftModifier);
    QVERIFY(shown.shows(CanvasView::moveSelectionCursor(ratio)));
    // Space mid-drag leaves the outline its pointer.
    QTRY_VERIFY(canvas.hasFocus());
    QTest::keyPress(&canvas, Qt::Key_Space);
    shown.move(QPointF(220, 150));
    QVERIFY(shown.shows(CanvasView::moveSelectionCursor(ratio)));
    QTest::keyRelease(&canvas, Qt::Key_Space);
    shown.release(QPointF(220, 150), Qt::ShiftModifier);
    QCOMPARE(bounds(session).x(), 20.0);
    QVERIFY(image(CanvasView::moveSelectionCursor(1)).pixelColor(17, 16).alpha() > 0);
    // Space shows the hand; busy the arrow.
    QTRY_VERIFY(canvas.hasFocus());
    QTest::keyPress(&canvas, Qt::Key_Space);
    QCOMPARE(canvas.cursor().shape(), Qt::OpenHandCursor);
    QTest::keyRelease(&canvas, Qt::Key_Space);
    QVERIFY(shown.shows(CanvasView::moveSelectionCursor(ratio)));
}

void SelectionOverlayTests::theAntsMarchWhileASelectionShowsAndStopWithout()
{
    Canvas shown;
    EditorSession &session = shown.session;
    CanvasView &canvas = *shown.canvas;
    session.applySelection(rectPath(QRectF(50, 50, 100, 100)), SelectionMode::replace, "Select");
    canvas.synchronizeDisplay();
    // Along the top edge the white dashes move.
    const auto edge = [&] {
        const QImage shot = canvas.grab().toImage();
        QString row;
        for (int x = 50; x < 150; ++x)
            row += shot.pixelColor(x, 50).lightness() > 120 ? 'w' : '.';
        return row;
    };
    const QString first = edge();
    QVERIFY(first.contains('w') && first.contains('.'));
    QTRY_VERIFY(edge() != first);
    // Each tick repaints the outline's rect, nothing more.
    QApplication::processEvents();
    PaintSpy spy(canvas);
    QTRY_VERIFY(spy.count >= 2);
    QVERIFY(canvas.findChild<QTimer *>("antsTimer")->isActive());
    QVERIFY(spy.painted.contains(QRect(50, 50, 100, 100)));
    QVERIFY(QRect(46, 46, 108, 108).contains(spy.painted));
    // Without a selection the ants rest; the outline is gone.
    session.deselect();
    canvas.synchronizeDisplay();
    QApplication::processEvents();
    const QString cleared = edge();
    QVERIFY(!cleared.contains('w'));
    const int painted = spy.count;
    QTest::qWait(300);
    QCOMPARE(spy.count, painted);
    QCOMPARE(edge(), cleared);
    QVERIFY(!canvas.findChild<QTimer *>("antsTimer")->isActive());
    QCOMPARE(canvas.findChild<QTimer *>("antsTimer")->interval(), 120);
    // An empty selection shows no ants either.
    session.applySelection(rectPath(QRectF(50, 50, 100, 100)), SelectionMode::replace, "Select");
    session.applySelection(rectPath(QRectF(0, 0, 400, 300)), SelectionMode::subtract, "Subtract");
    canvas.synchronizeDisplay();
    QCOMPARE(edge(), cleared);
    // Beside the Move box a tick repaints the ants alone.
    session.insert(filled(400, 300, qRgba(0, 0, 255, 255), "Blue"));
    session.selectTool(NavigationTool::move);
    session.applySelection(rectPath(QRectF(50, 50, 20, 20)), SelectionMode::replace, "Select");
    canvas.synchronizeDisplay();
    QApplication::processEvents();
    spy.painted = QRect();
    const int before = spy.count;
    QTRY_VERIFY(spy.count >= before + 2);
    QVERIFY(QRect(46, 46, 28, 28).contains(spy.painted));
}

void SelectionOverlayTests::autoscrollPansTheViewWhileADragHoldsAtTheEdge()
{
    Canvas shown;
    EditorSession &session = shown.session;
    const QSizeF size = shown.documentSize();
    const QPointF before = session.viewport.viewPoint(QPointF(0, 0), size);
    // Held at the right edge, the view slides.
    shown.press(QPointF(100, 100));
    shown.move(QPointF(395, 150));
    QTRY_VERIFY(session.viewport.viewPoint(QPointF(0, 0), size).x() < before.x() - 4);
    // Seven points into the margin: 4.8 a tick.
    const double slid = before.x() - session.viewport.viewPoint(QPointF(0, 0), size).x();
    QVERIFY2(std::abs(std::fmod(slid, 4.8)) < 1e-6 && slid < 4.8 * 6, qPrintable(QString::number(slid)));
    QVERIFY(!session.selection().has_value());
    const double grown = session.lassoDraft().value().points[2].x();
    QVERIFY(grown > 395);
    // Far past the edge, forty a tick at most.
    shown.move(QPointF(200, 150));
    const QPointF held = session.viewport.viewPoint(QPointF(0, 0), size);
    shown.move(QPointF(500, 150));
    QTRY_VERIFY(session.viewport.viewPoint(QPointF(0, 0), size).x() < held.x() - 39);
    const double capped = held.x() - session.viewport.viewPoint(QPointF(0, 0), size).x();
    QVERIFY2(std::abs(std::fmod(capped, 40)) < 1e-6, qPrintable(QString::number(capped)));
    // Back inside, the pan stops where it is.
    shown.move(QPointF(200, 150));
    QVERIFY(!shown.canvas->findChild<QTimer *>("marqueeAutoscroll")->isActive());
    QCOMPARE(shown.canvas->findChild<QTimer *>("marqueeAutoscroll")->interval(), 16);
    const QPointF rest = session.viewport.viewPoint(QPointF(0, 0), size);
    QTest::qWait(100);
    QCOMPARE(session.viewport.viewPoint(QPointF(0, 0), size), rest);
    shown.release(QPointF(200, 150));
    QVERIFY(session.selection().has_value());
    // A dragged outline follows the pan too.
    session.selectAll();
    shown.press(QPointF(200, 150));
    shown.move(QPointF(5, 150));
    QTRY_VERIFY(session.viewport.viewPoint(QPointF(0, 0), size).x() > rest.x() + 20);
    // The outline followed the pointer, then the pan.
    QVERIFY(bounds(session).x() < -195);
    shown.release(QPointF(5, 150));
    QCOMPARE(session.history.undoName(), QString("Move Selection"));
}

void SelectionOverlayTests::theOutlineAndTheDraftAreDrawnWhereTheyLie()
{
    Canvas shown;
    EditorSession &session = shown.session;
    CanvasView &canvas = *shown.canvas;
    // The first paint is still pending: let it pass.
    QTest::qWait(50);
    PaintSpy spy(canvas);
    shown.press(QPointF(50, 50));
    shown.move(QPointF(150, 100));
    QApplication::processEvents();
    // The draft's box, black under white, repainted where it lies.
    QVERIFY(spy.painted.contains(QRect(50, 50, 100, 50)));
    QVERIFY(!spy.painted.contains(QPoint(300, 250)));
    QImage shot = canvas.grab().toImage();
    // Soft-edged lines mix with the checkerboard's 77 and 89.
    const QColor top = shot.pixelColor(100, 50), beside = shot.pixelColor(100, 53);
    QVERIFY(top.lightness() > 120 || top.lightness() < 55);
    QVERIFY(beside.lightness() > 55 && beside.lightness() < 120);
    shown.release(QPointF(150, 100));
    QApplication::processEvents();
    shot = canvas.grab().toImage();
    // Four on, four off: about half the edge is white.
    int light = 0;
    for (int x = 50; x < 150; ++x)
        light += shot.pixelColor(x, 50).lightness() > 120;
    QVERIFY2(light >= 40 && light <= 60, qPrintable(QString::number(light)));
    // A new outline repaints where it lies, before any tick.
    session.deselect();
    canvas.synchronizeDisplay();
    QApplication::processEvents();
    spy.painted = QRect();
    session.applySelection(rectPath(QRectF(200, 200, 50, 50)), SelectionMode::replace, "Select");
    canvas.synchronizeDisplay();
    QApplication::processEvents();
    QVERIFY(spy.painted.contains(QRect(200, 200, 50, 50)));
    QVERIFY(!spy.painted.contains(QPoint(20, 20)));
    // Panned, the outline moves; deselecting repaints where it is.
    session.viewport.translate(QSizeF(-100, 0));
    session.notify();
    canvas.synchronizeDisplay();
    QApplication::processEvents();
    spy.painted = QRect();
    session.deselect();
    canvas.synchronizeDisplay();
    QApplication::processEvents();
    QVERIFY(spy.painted.contains(QRect(100, 200, 50, 50)));
    // A polygonal draft draws its first corner's handle.
    session.selectTool(NavigationTool::lasso);
    session.setLassoKind(LassoKind::polygonal);
    shown.click(QPointF(200, 200));
    shown.hover(QPointF(250, 220));
    QApplication::processEvents();
    shot = canvas.grab().toImage();
    QVERIFY(shot.pixelColor(200, 200).lightness() > 200);
    QVERIFY(shot.pixelColor(196, 200).lightness() < 160);
    QVERIFY(shot.pixelColor(193, 200).lightness() < 120);
    session.cancelLasso();
}

void SelectionOverlayTests::outlinesAndDraftsAreAntialiased()
{
    Canvas shown;
    EditorSession &session = shown.session;
    // Over mid gray a soft edge leaves other values.
    const auto softness = [&] {
        QImage sheet(400, 300, QImage::Format_RGB32);
        sheet.fill(QColor(128, 128, 128));
        QPainter painter(&sheet);
        TransformOverlay(session).draw(painter, shown.canvas->palette());
        painter.end();
        // A white line's partial pixels lie between gray and white.
        int soft = 0;
        for (int y = 0; y < 300; ++y) {
            for (int x = 0; x < 400; ++x) {
                const int value = qRed(sheet.pixel(x, y));
                soft += value > 128 && value < 255;
            }
        }
        return soft;
    };
    QPainterPath oval;
    oval.addEllipse(QRectF(50, 50, 200, 100));
    session.applySelection(oval, SelectionMode::replace, "Select");
    QVERIFY(softness() > 100);
    session.deselect();
    QCOMPARE(softness(), 0);
    session.setMarqueeKind(LassoKind::ellipse);
    session.beginLasso(QPointF(50, 50), SelectionMode::replace);
    session.dragMarquee(QPointF(250, 150), false, false);
    QVERIFY(softness() > 100);
    session.cancelLasso();
}

void SelectionOverlayTests::theAntsDashesKeepFourOnFourOffAtTwoTimes()
{
    Canvas shown;
    EditorSession &session = shown.session;
    session.applySelection(rectPath(QRectF(50, 50, 100, 100)), SelectionMode::replace, "Select");
    // Drawn at 2×: a one-point line fills two device rows.
    QImage sheet(800, 600, QImage::Format_RGB32);
    sheet.fill(QColor(128, 128, 128));
    QPainter painter(&sheet);
    painter.scale(2, 2);
    TransformOverlay(session).draw(painter, shown.canvas->palette());
    painter.end();
    QString row;
    for (int x = 100; x <= 300; ++x) {
        const int value = qRed(sheet.pixel(x, 100));
        row += value < 64 ? 'b' : value > 192 ? 'w' : '.';
    }
    QVERIFY(!row.contains('.'));
    // Butt caps: every whole dash and gap is eight pixels.
    int runs = 0;
    for (int start = 0; start < row.size();) {
        int end = start;
        while (end < row.size() && row[end] == row[start])
            ++end;
        if (start > 0 && end < row.size()) {
            QCOMPARE(end - start, 8);
            ++runs;
        }
        start = end;
    }
    QVERIFY(runs >= 20);
}

void SelectionOverlayTests::openDraftsAndCursorBadgesEndWhereTheyEnd()
{
    Canvas shown;
    EditorSession &session = shown.session;
    session.selectTool(NavigationTool::lasso);
    session.beginLasso(QPointF(50, 100), SelectionMode::replace);
    session.extendLasso(QPointF(150, 100));
    // At 2× over gray the stroke stops at its end.
    QImage sheet(800, 600, QImage::Format_RGB32);
    sheet.fill(QColor(128, 128, 128));
    QPainter painter(&sheet);
    painter.scale(2, 2);
    TransformOverlay(session).draw(painter, shown.canvas->palette());
    painter.end();
    QVERIFY(qRed(sheet.pixel(298, 200)) < 64 || qRed(sheet.pixel(298, 200)) > 192);
    QCOMPARE(sheet.pixel(300, 200), QColor(128, 128, 128).rgb());
    QCOMPARE(sheet.pixel(301, 200), QColor(128, 128, 128).rgb());
    QCOMPARE(sheet.pixel(99, 200), QColor(128, 128, 128).rgb());
    session.cancelLasso();
    // The badges' dashes at 2×: four dark, three light, butt-capped.
    const auto whites = [](const QCursor &cursor, int row, int from, int to) {
        const QImage badge = image(cursor);
        int count = 0;
        for (int x = from; x <= to; ++x)
            count += badge.pixelColor(x, row).lightness() > 192 && badge.pixelColor(x, row).alpha() > 0;
        return count;
    };
    QVERIFY2(whites(CanvasView::moveSelectionCursor(2), 33, 28, 42) >= 5, "the move badge");
    QVERIFY2(whites(CanvasView::loadSelectionCursor(2), 37, 36, 50) >= 5, "the load badge");
}

QTEST_MAIN(SelectionOverlayTests)
#include "SelectionOverlayTests.moc"
