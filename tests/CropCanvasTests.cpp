#include "SelectionCanvasFixtures.h"
#include <QNativeGestureEvent>

// The Crop tool on the canvas: drags, keys, cursors, frame.
class CropCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void aDragDrawsMovesAndResizesTheFrame();
    void altAndCtrlShapeTheDragAsTheyChange();
    void snappingReachesEightViewPoints();
    void theKeysAndLostReleasesEndTheDrag();
    void theCursorShowsWhatAPressTakes();
    void theFrameDimsWhatItCutsAway();
    void aFramePastTheCanvasShowsWhatItTakesIn();
    void cropDraggingRedrawsOnlyTheOverlay();
};

namespace {
struct CropCanvas : Canvas {
    CropCanvas()
    {
        // Zoomed about the middle, the pan keeps a float's dust.
        session.viewport.pan = QSizeF(0, 0);
        session.selectTool(NavigationTool::crop);
        canvas->synchronizeDisplay();
    }
    std::optional<QRectF> frame() const { return session.cropRect(); }
    Qt::CursorShape shape() const { return canvas->cursor().shape(); }
};
}

void CropCanvasTests::aDragDrawsMovesAndResizesTheFrame()
{
    CropCanvas shown;
    // On the whole canvas's frame a press draws anew.
    shown.press(QPointF(100, 100));
    QCOMPARE(shown.frame(), std::nullopt);
    shown.move(QPointF(200, 160));
    QCOMPARE(shown.frame(), std::optional(QRectF(100, 100, 100, 60)));
    shown.release(QPointF(200, 160));
    // Inside a drawn frame a press moves it.
    shown.drag(QPointF(150, 130), QPointF(160, 145));
    QCOMPARE(shown.frame(), std::optional(QRectF(110, 115, 100, 60)));
    // Eight points from the canvas's edge, an edge snaps.
    shown.drag(QPointF(150, 130), QPointF(45, 130));
    QCOMPARE(shown.frame(), std::optional(QRectF(0, 115, 100, 60)));
    // Corner grips resize; so do edges far from their middle.
    shown.drag(QPointF(100, 175), QPointF(150, 200));
    QCOMPARE(shown.frame(), std::optional(QRectF(0, 115, 150, 85)));
    shown.drag(QPointF(20, 115), QPointF(20, 95));
    QCOMPARE(shown.frame(), std::optional(QRectF(0, 95, 150, 105)));
    // A fixed ratio shapes a new frame.
    shown.session.setCropRatioChoice("1:1");
    shown.drag(QPointF(250, 20), QPointF(310, 50));
    QCOMPARE(shown.frame(), std::optional(QRectF(250, 20, 60, 60)));
    // Dragged past the bounds, a frame keeps its last shape.
    shown.session.setCropRatioChoice("Free");
    shown.session.zoom(0.01);
    shown.canvas->synchronizeDisplay();
    shown.press(QPointF(1, 1));
    shown.move(QPointF(100, 100));
    const QRectF good = shown.frame().value();
    shown.move(QPointF(399, 299));
    QCOMPARE(shown.frame(), std::optional(good));
    shown.release(QPointF(399, 299));
}

void CropCanvasTests::altAndCtrlShapeTheDragAsTheyChange()
{
    CropCanvas shown;
    // Alt grows the frame out from the press.
    shown.press(QPointF(300, 50), Qt::AltModifier);
    shown.move(QPointF(320, 60), Qt::AltModifier);
    QCOMPARE(shown.frame(), std::optional(QRectF(280, 40, 40, 20)));
    shown.release(QPointF(320, 60), Qt::AltModifier);
    // Ctrl leaves the edges where the pointer puts them.
    shown.drag(QPointF(100, 100), QPointF(395, 200));
    QCOMPARE(shown.frame(), std::optional(QRectF(100, 100, 300, 100)));
    shown.session.cancelCrop();
    shown.drag(QPointF(100, 100), QPointF(395, 200), Qt::ControlModifier);
    QCOMPARE(shown.frame(), std::optional(QRectF(100, 100, 295, 100)));
    // A key pressed or let go mid-drag reshapes at once.
    shown.session.cancelCrop();
    shown.press(QPointF(200, 150));
    shown.move(QPointF(220, 160));
    QCOMPARE(shown.frame(), std::optional(QRectF(200, 150, 20, 10)));
    QTest::keyPress(shown.window.windowHandle(), Qt::Key_Alt, Qt::AltModifier);
    QCOMPARE(shown.frame(), std::optional(QRectF(180, 140, 40, 20)));
    QTest::keyRelease(shown.window.windowHandle(), Qt::Key_Alt, Qt::NoModifier);
    QCOMPARE(shown.frame(), std::optional(QRectF(200, 150, 20, 10)));
    shown.move(QPointF(395, 160));
    QCOMPARE(shown.frame(), std::optional(QRectF(200, 150, 200, 10)));
    QTest::keyPress(shown.window.windowHandle(), Qt::Key_Control, Qt::ControlModifier);
    QCOMPARE(shown.frame(), std::optional(QRectF(200, 150, 195, 10)));
    QTest::keyRelease(shown.window.windowHandle(), Qt::Key_Control, Qt::NoModifier);
    shown.release(QPointF(395, 160));
    // Without a drag a key leaves the frame be.
    QTest::keyPress(shown.window.windowHandle(), Qt::Key_Alt, Qt::AltModifier);
    QTest::keyRelease(shown.window.windowHandle(), Qt::Key_Alt, Qt::NoModifier);
    QCOMPARE(shown.frame(), std::optional(QRectF(200, 150, 200, 10)));
}

void CropCanvasTests::theKeysAndLostReleasesEndTheDrag()
{
    CropCanvas shown;
    EditorSession &session = shown.session;
    // The canvas takes the frame's keys: nothing above sees them.
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(shown.canvas, &escape);
    QVERIFY(escape.isAccepted());
    // Escape drops the frame, mid-drag too, ending the drag.
    shown.move(QPointF(150, 150));
    QTest::keyClick(shown.canvas, Qt::Key_Escape);
    QCOMPARE(shown.frame(), std::nullopt);
    shown.move(QPointF(200, 200));
    QCOMPARE(shown.frame(), std::nullopt);
    shown.release(QPointF(200, 200));
    QCOMPARE(session.visibleCropRect(), std::optional(QRectF(0, 0, 400, 300)));
    // Return without a frame crops nothing.
    QTest::keyClick(shown.canvas, Qt::Key_Return);
    QTest::qWait(20);
    QCOMPARE(session.document().value().width, 400);
    // Losing the keys, or the left button, ends a drag.
    shown.press(QPointF(100, 100));
    shown.move(QPointF(150, 150));
    QFocusEvent out(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
    QApplication::sendEvent(shown.canvas, &out);
    shown.move(QPointF(200, 200));
    QCOMPARE(shown.frame(), std::optional(QRectF(100, 100, 50, 50)));
    shown.canvas->setFocus();
    shown.press(QPointF(10, 250));
    shown.move(QPointF(20, 260));
    shown.hover(QPointF(30, 270));
    shown.move(QPointF(40, 280));
    QCOMPARE(shown.frame(), std::optional(QRectF(10, 250, 10, 10)));
    shown.release(QPointF(40, 280));
    // Mid-drag, wheels and pinches neither pan nor zoom.
    const CanvasViewport before = session.viewport;
    shown.press(QPointF(200, 50));
    wheel(*shown.canvas, QPointF(200, 50), QPoint(0, 40), QPoint(0, 120), Qt::NoModifier);
    QNativeGestureEvent pinch(Qt::ZoomNativeGesture, QPointingDevice::primaryPointingDevice(), 2, QPointF(200, 50), QPointF(200, 50),
                              shown.canvas->mapToGlobal(QPoint(200, 50)), 0.5, QPointF());
    QApplication::sendEvent(shown.canvas, &pinch);
    QVERIFY(session.viewport == before);
    // Busy, or another tool, and a drag changes nothing.
    shown.move(QPointF(250, 80));
    QCOMPARE(shown.frame(), std::optional(QRectF(200, 50, 50, 30)));
    session.setIsProjectBusy(true);
    shown.move(QPointF(260, 90));
    QCOMPARE(shown.frame(), std::optional(QRectF(200, 50, 50, 30)));
    session.setIsProjectBusy(false);
    session.selectTool(NavigationTool::brush);
    shown.move(QPointF(270, 100));
    // The Brush's circle follows the pointer meanwhile.
    QCOMPARE(shown.canvas->brushCursor().circle().value().center(), QPointF(270, 100));
    QTest::keyPress(shown.window.windowHandle(), Qt::Key_Alt, Qt::AltModifier);
    QTest::keyRelease(shown.window.windowHandle(), Qt::Key_Alt, Qt::NoModifier);
    QCOMPARE(shown.frame(), std::nullopt);
    shown.release(QPointF(270, 100));
    // Return crops to the frame, Enter alike; one step each.
    session.selectTool(NavigationTool::crop);
    session.setCropRect(QRectF(10, 20, 200, 100));
    QTest::keyClick(shown.canvas, Qt::Key_Return);
    QTRY_COMPARE(session.document().value().width, 200);
    QCOMPARE(session.document().value().height, 100);
    QCOMPARE(session.history.undoName(), QString("Crop"));
    session.setCropRect(QRectF(0, 0, 50, 40));
    QTest::keyClick(shown.canvas, Qt::Key_Enter, Qt::KeypadModifier);
    QTRY_COMPARE(session.document().value().width, 50);
    // Off the Crop tool, Return goes to a transform.
    session.selectTool(NavigationTool::move);
    session.insert(filled(10, 10, qRgba(0, 0, 255, 255), "Blue"));
    session.beginTransform();
    QVERIFY(session.transformEdit());
    QTest::keyClick(shown.canvas, Qt::Key_Return);
    QVERIFY(!session.transformEdit());
    QCOMPARE(session.document().value().width, 50);
    // A document gone mid-drag leaves the drag nothing to do.
    session.selectTool(NavigationTool::crop);
    shown.press(QPointF(10, 10));
    session.clearProject();
    shown.move(QPointF(30, 30));
    QTest::keyPress(shown.window.windowHandle(), Qt::Key_Alt, Qt::AltModifier);
    QTest::keyRelease(shown.window.windowHandle(), Qt::Key_Alt, Qt::NoModifier);
    QCOMPARE(shown.frame(), std::nullopt);
    shown.release(QPointF(30, 30));
}

void CropCanvasTests::snappingReachesEightViewPoints()
{
    CropCanvas shown;
    // At 100% eight points are eight pixels: nine stay out.
    shown.drag(QPointF(100, 100), QPointF(391, 200));
    QCOMPARE(shown.frame(), std::optional(QRectF(100, 100, 291, 100)));
    // At 50% the same eight points reach sixteen pixels.
    shown.session.zoom(0.5);
    shown.canvas->synchronizeDisplay();
    const QSizeF size = shown.documentSize();
    const QPointF from = shown.session.viewport.viewPoint(QPointF(100, 50), size), to = shown.session.viewport.viewPoint(QPointF(390, 170), size);
    shown.drag(from, to);
    QCOMPARE(shown.frame(), std::optional(QRectF(100, 50, 300, 120)));
}

void CropCanvasTests::theCursorShowsWhatAPressTakes()
{
    CropCanvas shown;
    // Before any hover, the crosshair.
    QCOMPARE(shown.shape(), Qt::CrossCursor);
    // A narrow frame's corners overlap: the first corner wins.
    shown.session.setCropRect(QRectF(100, 100, 10, 100));
    shown.canvas->synchronizeDisplay();
    shown.hover(QPointF(105, 100));
    QCOMPARE(shown.shape(), Qt::SizeFDiagCursor);
    shown.session.setCropRect(QRectF(100, 100, 200, 100));
    shown.canvas->synchronizeDisplay();
    shown.hover(QPointF(200, 150));
    QCOMPARE(shown.shape(), Qt::CrossCursor);
    shown.hover(QPointF(50, 50));
    QCOMPARE(shown.shape(), Qt::CrossCursor);
    const std::pair<QPointF, Qt::CursorShape> grips[] = {
        {{100, 100}, Qt::SizeFDiagCursor}, {{200, 100}, Qt::SizeVerCursor},   {{300, 100}, Qt::SizeBDiagCursor}, {{300, 150}, Qt::SizeHorCursor},
        {{300, 200}, Qt::SizeFDiagCursor}, {{200, 200}, Qt::SizeVerCursor},   {{100, 200}, Qt::SizeBDiagCursor}, {{100, 150}, Qt::SizeHorCursor},
        {{115, 91}, Qt::SizeVerCursor},    {{109, 185}, Qt::SizeHorCursor},   {{109.5, 109.5}, Qt::SizeFDiagCursor}, {{110, 110}, Qt::CrossCursor},
        {{90, 95}, Qt::SizeFDiagCursor},   {{95, 90}, Qt::SizeFDiagCursor}};
    for (const auto &[at, cursor] : grips) {
        shown.hover(at);
        QCOMPARE(shown.shape(), cursor);
    }
    // Space pans.
    QTest::keyPress(shown.canvas, Qt::Key_Space);
    shown.hover(QPointF(300, 200));
    QCOMPARE(shown.shape(), Qt::OpenHandCursor);
    QTest::keyRelease(shown.canvas, Qt::Key_Space);
    QCOMPARE(shown.shape(), Qt::SizeFDiagCursor);
    // A new frame keeps its press's cursor: the old frame's.
    shown.hover(QPointF(5, 150));
    QCOMPARE(shown.shape(), Qt::CrossCursor);
    shown.press(QPointF(5, 150));
    QCOMPARE(shown.frame(), std::nullopt);
    QCOMPARE(shown.shape(), Qt::CrossCursor);
    shown.move(QPointF(30, 170));
    QCOMPARE(shown.shape(), Qt::CrossCursor);
    shown.release(QPointF(30, 170));
    shown.session.setCropRect(QRectF(100, 100, 200, 100));
    shown.canvas->synchronizeDisplay();
    // A drag keeps its press's cursor until the release.
    shown.session.cancelCrop();
    shown.press(QPointF(50, 50));
    shown.move(QPointF(150, 150));
    QCOMPARE(shown.frame(), std::optional(QRectF(50, 50, 100, 100)));
    QCOMPARE(shown.shape(), Qt::CrossCursor);
    shown.release(QPointF(150, 150));
    QCOMPARE(shown.shape(), Qt::SizeFDiagCursor);
    shown.press(QPointF(150, 100));
    QCOMPARE(shown.shape(), Qt::SizeHorCursor);
    shown.move(QPointF(120, 60));
    QCOMPARE(shown.shape(), Qt::SizeHorCursor);
    QTest::keyClick(shown.canvas, Qt::Key_Escape);
    shown.canvas->synchronizeDisplay();
    QCOMPARE(shown.shape(), Qt::CrossCursor);
    shown.release(QPointF(120, 60));
    // Escape without a crop drag leaves another drag's cursor be.
    shown.session.selectTool(NavigationTool::move);
    shown.session.insert(filled(100, 100, qRgba(0, 0, 255, 255), "Blue"));
    shown.press(QPointF(200, 150));
    const QImage dragging = image(shown.canvas->cursor());
    QVERIFY(!dragging.isNull());
    QTest::keyClick(shown.canvas, Qt::Key_C);
    QTest::keyClick(shown.canvas, Qt::Key_Escape);
    QCOMPARE(image(shown.canvas->cursor()), dragging);
    shown.release(QPointF(200, 150));
    // Return frees the drag cursor at once, before the crop.
    shown.press(QPointF(50, 50));
    shown.move(QPointF(150, 150));
    QCOMPARE(shown.shape(), Qt::CrossCursor);
    QTest::keyClick(shown.canvas, Qt::Key_Return);
    QCOMPARE(shown.shape(), Qt::SizeFDiagCursor);
    shown.release(QPointF(150, 150));
    QTRY_COMPARE(shown.session.document().value().width, 100);
}

void CropCanvasTests::theFrameDimsWhatItCutsAway()
{
    CropCanvas shown;
    EditorSession &session = shown.session;
    session.setCropRect(QRectF(100, 100, 200, 100));
    QImage image(400, 300, QImage::Format_RGB32);
    image.fill(QColor(200, 200, 200));
    {
        QPainter painter(&image);
        TransformOverlay(session).draw(painter, shown.canvas->palette());
    }
    // Outside, 60% black; inside, clear but for lines and grips.
    QCOMPARE(qRed(image.pixel(50, 50)), 80);
    QCOMPARE(qRed(image.pixel(150, 150)), 200);
    QCOMPARE(qRed(image.pixel(150, 120)), 200);
    // The frame straddles the edge: half white, half beneath.
    QVERIFY(std::abs(qRed(image.pixel(150, 100)) - 228) <= 1);
    QVERIFY(std::abs(qRed(image.pixel(150, 99)) - 168) <= 1);
    // A third's line: 40% white over its pixels, antialiased.
    QVERIFY(qRed(image.pixel(166, 120)) > 210 && qRed(image.pixel(166, 120)) < 221);
    QCOMPARE(qRed(image.pixel(165, 120)), 200);
    QVERIFY(qRed(image.pixel(150, 133)) > 205);
    // Both thirds each way, end to end, flat-capped.
    for (const QPoint third : {QPoint(233, 120), QPoint(150, 166), QPoint(166, 101), QPoint(166, 198), QPoint(298, 133)})
        QVERIFY2(qRed(image.pixel(third)) > 205, qPrintable(QString("%1 %2").arg(third.x()).arg(third.y())));
    QVERIFY(std::abs(qRed(image.pixel(166, 99)) - 168) <= 2);
    // White grips with black rims, corners and middles alike.
    for (const QPoint grip : {QPoint(102, 102), QPoint(202, 102), QPoint(298, 198), QPoint(102, 152)})
        QCOMPARE(image.pixel(grip), qRgb(255, 255, 255));
    QVERIFY(qRed(image.pixel(96, 101)) < 200);
    // At twice the scale a grip's corner is mitred.
    QImage twice(800, 600, QImage::Format_RGB32);
    twice.fill(QColor(200, 200, 200));
    {
        QPainter painter(&twice);
        painter.scale(2, 2);
        TransformOverlay(session).draw(painter, shown.canvas->palette());
    }
    QCOMPARE(qRed(twice.pixel(191, 191)), 0);
    QCOMPARE(qRed(twice.pixel(204, 204)), 255);
    // It repaints the frames, the grips' five points around.
    QCOMPARE(TransformOverlay(session).drawnRect(QRect(0, 0, 400, 300)), QRect(95, 95, 210, 110));
    session.zoom(2);
    QCOMPARE(TransformOverlay(session).cropViewRect().value().size(), QSizeF(400, 200));
    QCOMPARE(TransformOverlay(session).cropViewRect().value().topLeft(), session.viewport.viewPoint(QPointF(100, 100), QSizeF(400, 300)));
    session.zoom(1);
    session.selectTool(NavigationTool::hand);
    QVERIFY(TransformOverlay(session).drawnRect(QRect(0, 0, 400, 300)).isEmpty());
    QVERIFY(TransformOverlay(session).cropResizeRegions().empty());
}

void CropCanvasTests::aFramePastTheCanvasShowsWhatItTakesIn()
{
    Shown shown;
    shown.settle();
    shown.session.zoom(1);
    shown.canvas->synchronizeDisplay();
    // The document sits at 150–250 × 100–200 in view points.
    QCOMPARE(shown.session.viewport.viewPoint(QPointF(0, 0), shown.documentSize()), QPointF(150, 100));
    const QColor surround = shown.canvas->palette().color(QPalette::Base);
    QCOMPARE(shown.canvas->grab().toImage().pixelColor(120, 150), surround);
    shown.session.selectTool(NavigationTool::crop);
    shown.session.setCropRect(QRectF(-50, 0, 100, 100));
    shown.canvas->synchronizeDisplay();
    const QImage shot = shown.canvas->grab().toImage();
    // The checkerboard's squares: the frame takes this in.
    QCOMPARE(qGray(shot.pixel(120, 150)), 77);
    QCOMPARE(qGray(shot.pixel(106, 106)), 89);
    // The canvas still shows beside it, dimmed.
    QVERIFY(std::abs(qGray(shot.pixel(230, 150)) - 36) <= 1);
    // Beyond the frame the surround shows, dimmed.
    QVERIFY(std::abs(qRed(shot.pixel(20, 20)) - qRound(surround.red() * 0.4)) <= 1);
    shown.session.selectTool(NavigationTool::hand);
    shown.canvas->synchronizeDisplay();
    QCOMPARE(shown.canvas->grab().toImage().pixelColor(120, 150), surround);
    // Coming and going, the dimming repaints the canvas.
    shown.session.viewport.pan = QSizeF(0, 0);
    shown.canvas->synchronizeDisplay();
    QApplication::processEvents();
    PaintSpy spy(*shown.canvas);
    const auto shows = [&](const std::function<void()> &change) {
        spy.painted = QRect();
        change();
        shown.canvas->synchronizeDisplay();
        QApplication::processEvents();
        return spy.painted;
    };
    QCOMPARE(shows([&] { shown.session.selectTool(NavigationTool::crop); }), shown.canvas->rect());
    // A changed frame repaints the old and the new alone.
    QCOMPARE(shows([&] { shown.session.setCropRect(QRectF(10, 0, 20, 20)); }), QRect(145, 95, 110, 110));
    QCOMPARE(shows([&] { shown.session.setCropRect(QRectF(20, 0, 20, 20)); }), QRect(155, 95, 40, 30));
    QCOMPARE(shows([&] { shown.session.selectTool(NavigationTool::hand); }), shown.canvas->rect());
}

// Swift's test: inside the canvas a drag repaints frames alone.
void CropCanvasTests::cropDraggingRedrawsOnlyTheOverlay()
{
    Shown shown(QSize(800, 600), QSize(600, 450));
    shown.settle();
    EditorSession &session = shown.session;
    session.insert(filled(400, 300, qRgba(255, 0, 0, 255), "Red"));
    session.selectTool(NavigationTool::crop);
    shown.canvas->synchronizeDisplay();
    QApplication::processEvents();
    PaintSpy spy(*shown.canvas);
    QTest::mousePress(shown.canvas, Qt::LeftButton, Qt::NoModifier, QPoint(150, 120));
    QApplication::processEvents();
    const auto viewRect = [&] { return TransformOverlay(session).cropViewRect().value(); };
    QRectF shown_ = viewRect();
    for (int step = 0; step < 10; ++step) {
        spy.painted = QRect();
        drag(*shown.canvas, QPointF(170 + step * 10, 140 + step * 8));
        QApplication::processEvents();
        const QRectF next = viewRect();
        // Old and new frames together, grips and all.
        QVERIFY((shown_ | next).adjusted(-5, -5, 5, 5).toAlignedRect().contains(spy.painted));
        QVERIFY(spy.painted.contains(next.toAlignedRect()));
        QVERIFY(step == 0 || spy.painted.width() < 150);
        QVERIFY(!shown.canvas->synchronizeDisplay());
        shown_ = next;
    }
    QTest::mouseRelease(shown.canvas, Qt::LeftButton, Qt::NoModifier, QPoint(260, 212));
    QVERIFY(session.cropRect().has_value());
}

QTEST_MAIN(CropCanvasTests)
#include "CropCanvasTests.moc"
