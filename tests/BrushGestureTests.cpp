#include "BrushCanvasFixtures.h"

// Swift's brush on the canvas: strokes, axes, tip, circle.
namespace {
// The live stroke's alpha at a document pixel.
int liveAlpha(const EditorSession &session, int x, int y)
{
    for (const BrushPatch &patch : session.brushStroke()->patches()) {
        if (patch.rect.contains(QPointF(x + 0.5, y + 0.5)))
            return patch.image.constScanLine(y - int(patch.rect.top()))[(x - int(patch.rect.left())) * 4 + 3];
    }
    return 0;
}

// Twenty pixels: wider than the curve's bend at a corner.
void wide(Canvas &shown)
{
    BrushSettings settings = shown.session.brushSettings();
    settings.diameter = 20;
    shown.session.setBrushSettings(settings);
}

void rightPress(Canvas &shown, QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QTest::mousePress(shown.canvas, Qt::RightButton, modifiers, at.toPoint());
}

void rightMove(Canvas &shown, QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QMouseEvent event(QEvent::MouseMove, to, to, shown.canvas->mapToGlobal(to.toPoint()), Qt::NoButton, Qt::RightButton, modifiers);
    QApplication::sendEvent(shown.canvas, &event);
}
}

class BrushGestureTests : public QObject {
    Q_OBJECT
private slots:
    void aPressDragAndReleasePaintOneStroke();
    void shiftDrawsOnFromTheLastStrokeAndKeepsAnAxis();
    void aShiftLineRunsThroughThePressAndTheAxisFollowsTheLastPixel();
    void theAxisSettlesOnceAndResetsWhenShiftIsLetGo();
    void beforeItSettlesTheAxisHoldsTheAnchor();
    void aBusyProjectHoldsTheStroke();
    void aStrokeHoldsTheViewStill();
    void spotHealingTakesTheBrushGestures();
    void losingFocusCancelsAStroke();
    void aRightDragSizesTheBrushAndShiftSetsHardness();
    void aTipDragSwapsWithShiftRoundsAndStopsAtTheLimit();
    void theCircleFollowsThePointerAndLeavesWithIt();
};

void BrushGestureTests::aPressDragAndReleasePaintOneStroke()
{
    Canvas shown;
    red(shown);
    const int count = shown.session.history.undoCount();
    shown.press(QPointF(100, 100));
    QVERIFY(shown.session.brushStroke());
    shown.move(QPointF(200, 100));
    shown.release(QPointF(250, 100));
    QVERIFY(!shown.session.brushStroke());
    QCOMPARE(shown.session.history.undoCount(), count + 1);
    QCOMPARE(shown.session.history.undoName(), QString("Brush Stroke"));
    // The release reaches its own point.
    for (const int x : {100, 150, 200, 245})
        QCOMPARE(pixel(shown.session, x, 100), (std::vector<int>{255, 0, 0, 255}));
    QCOMPARE(pixel(shown.session, 150, 120)[3], 0);
}

void BrushGestureTests::shiftDrawsOnFromTheLastStrokeAndKeepsAnAxis()
{
    Canvas shown;
    red(shown);
    shown.click(QPointF(50, 50));
    // Shift-press: a line on from where the last stroke ended.
    shown.press(QPointF(150, 150), Qt::ShiftModifier);
    shown.release(QPointF(150, 150), Qt::ShiftModifier);
    QCOMPARE(pixel(shown.session, 100, 100), (std::vector<int>{255, 0, 0, 255}));
    // Held, Shift keeps the axis its first three pixels chose.
    shown.press(QPointF(200, 250), Qt::ShiftModifier);
    shown.move(QPointF(210, 251), Qt::ShiftModifier);
    shown.move(QPointF(300, 270), Qt::ShiftModifier);
    // The release itself reaches its own point, as Swift's.
    shown.release(QPointF(300, 270), Qt::ShiftModifier);
    QCOMPARE(pixel(shown.session, 300, 250), (std::vector<int>{255, 0, 0, 255}));
    QCOMPARE(pixel(shown.session, 280, 268)[3], 0);
}

void BrushGestureTests::aShiftLineRunsThroughThePressAndTheAxisFollowsTheLastPixel()
{
    Canvas shown;
    red(shown);
    wide(shown);
    const std::vector<int> painted{255, 0, 0, 255};
    // The line reaches the press, then turns down the axis.
    shown.click(QPointF(20, 50));
    shown.press(QPointF(100, 50), Qt::ShiftModifier);
    shown.move(QPointF(100, 90), Qt::ShiftModifier);
    shown.release(QPointF(100, 90), Qt::ShiftModifier);
    QCOMPARE(pixel(shown.session, 60, 50), painted);
    QCOMPARE(pixel(shown.session, 60, 70)[3], 0);
    // Shift taken mid-stroke anchors where this stroke began.
    shown.drag(QPointF(300, 250), QPointF(320, 260));
    shown.press(QPointF(50, 150));
    shown.move(QPointF(90, 152), Qt::ShiftModifier);
    shown.release(QPointF(90, 150), Qt::ShiftModifier);
    QCOMPARE(pixel(shown.session, 70, 150), painted);
    // Or at the last pixel the stroke reached.
    shown.press(QPointF(50, 200));
    shown.move(QPointF(70, 220));
    shown.move(QPointF(150, 224), Qt::ShiftModifier);
    shown.release(QPointF(150, 225), Qt::ShiftModifier);
    QCOMPARE(pixel(shown.session, 130, 220), painted);
}

void BrushGestureTests::theAxisSettlesOnceAndResetsWhenShiftIsLetGo()
{
    Canvas shown;
    red(shown);
    wide(shown);
    const std::vector<int> painted{255, 0, 0, 255};
    // Five pixels settle it sideways; a later drop keeps it.
    shown.press(QPointF(50, 200));
    shown.move(QPointF(55, 202), Qt::ShiftModifier);
    shown.move(QPointF(60, 260), Qt::ShiftModifier);
    shown.release(QPointF(60, 200), Qt::ShiftModifier);
    QCOMPARE(pixel(shown.session, 50, 240)[3], 0);
    // A Shift press starts a fresh axis.
    shown.press(QPointF(120, 150), Qt::ShiftModifier);
    shown.move(QPointF(122, 230), Qt::ShiftModifier);
    shown.release(QPointF(160, 230));
    QCOMPARE(pixel(shown.session, 120, 210), painted);
    // Settled upright, it keeps to the anchor's column.
    shown.press(QPointF(300, 50));
    shown.move(QPointF(301, 60), Qt::ShiftModifier);
    shown.move(QPointF(340, 100), Qt::ShiftModifier);
    shown.release(QPointF(300, 100), Qt::ShiftModifier);
    QCOMPARE(pixel(shown.session, 300, 80), painted);
    // Letting Shift go frees both; Shift again settles afresh.
    shown.press(QPointF(150, 30));
    shown.move(QPointF(190, 32), Qt::ShiftModifier);
    shown.move(QPointF(190, 100));
    shown.move(QPointF(192, 180), Qt::ShiftModifier);
    shown.release(QPointF(250, 180));
    QCOMPARE(pixel(shown.session, 190, 140), painted);
}

void BrushGestureTests::beforeItSettlesTheAxisHoldsTheAnchor()
{
    Canvas shown;
    red(shown);
    BrushSettings settings = shown.session.brushSettings();
    settings.diameter = 1;
    shown.session.setBrushSettings(settings);
    shown.session.zoom(8);
    shown.canvas->synchronizeDisplay();
    const QPointF start = shown.session.viewport.documentPoint(QPointF(200, 150), shown.session.document().value().size());
    shown.press(QPointF(200, 150));
    // Under three pixels from the anchor, the stroke waits there.
    shown.move(QPointF(220, 162), Qt::ShiftModifier);
    QVERIFY(shown.session.brushStroke());
    const QPoint beside(int(std::floor(start.x() + 2.5)), int(std::floor(start.y() + 1.5)));
    QCOMPARE(liveAlpha(shown.session, beside.x(), beside.y()), 0);
    QTest::keyClick(shown.canvas, Qt::Key_Escape);
    QVERIFY(!shown.session.brushStroke());
}

void BrushGestureTests::aBusyProjectHoldsTheStroke()
{
    Canvas shown;
    red(shown);
    shown.press(QPointF(100, 100));
    shown.session.setIsProjectBusy(true);
    shown.move(QPointF(200, 100));
    shown.release(QPointF(200, 100));
    // Neither the drag nor the release reached the stroke.
    QVERIFY(shown.session.brushStroke());
    QCOMPARE(liveAlpha(shown.session, 100, 100), 255);
    QCOMPARE(liveAlpha(shown.session, 180, 100), 0);
    // Escape and a lost focus wait for the project too.
    QTest::keyClick(shown.canvas, Qt::Key_Escape);
    QFocusEvent out(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
    QApplication::sendEvent(shown.canvas, &out);
    QVERIFY(shown.session.brushStroke());
    shown.session.setIsProjectBusy(false);
    // A hover paints nothing; only a drag goes on.
    shown.hover(QPointF(250, 100));
    QCOMPARE(liveAlpha(shown.session, 240, 100), 0);
    shown.move(QPointF(250, 100));
    QCOMPARE(liveAlpha(shown.session, 240, 100), 255);
    QTest::keyClick(shown.canvas, Qt::Key_Escape);
    QVERIFY(!shown.session.brushStroke());
}

void BrushGestureTests::aStrokeHoldsTheViewStill()
{
    Canvas shown;
    red(shown);
    const QPointF pointer(200, 150);
    const auto under = [&] { return shown.session.viewport.documentPoint(pointer, shown.session.document().value().size()); };
    const QPointF before = under();
    QNativeGestureEvent pinch(Qt::ZoomNativeGesture, QPointingDevice::primaryPointingDevice(), 2, pointer, pointer,
                              shown.canvas->mapToGlobal(pointer.toPoint()), 0.5, QPointF());
    // Mid-stroke, wheels and pinches neither pan nor zoom.
    shown.press(QPointF(100, 100));
    wheel(*shown.canvas, pointer, QPoint(0, 40), QPoint(0, 120), Qt::NoModifier);
    wheel(*shown.canvas, pointer, QPoint(0, 40), QPoint(0, 120), Qt::ControlModifier);
    QApplication::sendEvent(shown.canvas, &pinch);
    QCOMPARE(shown.session.viewport.zoom(), 1.0);
    QCOMPARE(under(), before);
    shown.release(QPointF(100, 100));
    // With the stroke done, both work again.
    wheel(*shown.canvas, pointer, QPoint(0, 40), QPoint(0, 120), Qt::NoModifier);
    QVERIFY(under() != before);
    QApplication::sendEvent(shown.canvas, &pinch);
    QCOMPARE(shown.session.viewport.zoom(), 1.5);
}

void BrushGestureTests::losingFocusCancelsAStroke()
{
    Canvas shown;
    red(shown);
    shown.hover(QPointF(80, 80));
    QVERIFY(shown.canvas->brushCursor().circle().has_value());
    shown.press(QPointF(100, 100));
    QVERIFY(shown.session.brushStroke());
    QFocusEvent out(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
    QApplication::sendEvent(shown.canvas, &out);
    QVERIFY(!shown.session.brushStroke() && !shown.canvas->brushCursor().circle().has_value());
}

void BrushGestureTests::aRightDragSizesTheBrushAndShiftSetsHardness()
{
    Canvas shown;
    red(shown);
    rightPress(shown, QPointF(100, 100));
    // Each point widens the radius by a point: twice across.
    rightMove(shown, QPointF(150, 120));
    QCOMPARE(shown.session.brushSettings().diameter, 110.0);
    QCOMPARE(shown.canvas->brushCursor().circle().value().center(), QPointF(100, 100));
    QVERIFY(!shown.canvas->brushCursor().hardness().has_value());
    rightMove(shown, QPointF(40, 100));
    QCOMPARE(shown.session.brushSettings().diameter, 1.0);
    QTest::mouseRelease(shown.canvas, Qt::RightButton, Qt::NoModifier, QPoint(60, 70));
    QCOMPARE(shown.canvas->brushCursor().circle().value().center(), QPointF(60, 70));
    // With Shift the drag sets hardness, 200 points the range.
    BrushSettings settings = shown.session.brushSettings();
    settings.diameter = 30;
    settings.hardness = 0.5;
    shown.session.setBrushSettings(settings);
    rightPress(shown, QPointF(100, 100), Qt::ShiftModifier);
    rightMove(shown, QPointF(150, 100), Qt::ShiftModifier);
    QCOMPARE(shown.session.brushSettings().hardness, 0.75);
    QCOMPARE(shown.session.brushSettings().diameter, 30.0);
    QCOMPARE(shown.canvas->brushCursor().hardness(), std::optional(0.75));
    rightMove(shown, QPointF(400, 100), Qt::ShiftModifier);
    QCOMPARE(shown.session.brushSettings().hardness, 1.0);
    QTest::mouseRelease(shown.canvas, Qt::RightButton, Qt::ShiftModifier, QPoint(100, 100));
    QVERIFY(!shown.canvas->brushCursor().hardness().has_value());
    // A stroke under way keeps the right button off.
    shown.press(QPointF(200, 200));
    rightPress(shown, QPointF(200, 200));
    rightMove(shown, QPointF(260, 200));
    QCOMPARE(shown.session.brushSettings().diameter, 30.0);
    shown.release(QPointF(200, 200));
}

void BrushGestureTests::aTipDragSwapsWithShiftRoundsAndStopsAtTheLimit()
{
    Canvas shown;
    red(shown);
    BrushSettings settings = shown.session.brushSettings();
    settings.diameter = 30;
    settings.hardness = 0.5;
    shown.session.setBrushSettings(settings);
    // Shift at the press shows the hardness ring at once.
    rightPress(shown, QPointF(100, 100), Qt::ShiftModifier);
    QCOMPARE(shown.canvas->brushCursor().hardness(), std::optional(0.5));
    rightMove(shown, QPointF(140, 100), Qt::ShiftModifier);
    QCOMPARE(shown.session.brushSettings().hardness, 0.7);
    // Letting Shift go sizes from the press; hardness goes back.
    rightMove(shown, QPointF(140, 100));
    QCOMPARE(shown.session.brushSettings().diameter, 110.0);
    QCOMPARE(shown.session.brushSettings().hardness, 0.5);
    // Shift again puts the size back.
    rightMove(shown, QPointF(160, 100), Qt::ShiftModifier);
    QCOMPARE(shown.session.brushSettings().diameter, 30.0);
    QCOMPARE(shown.session.brushSettings().hardness, 0.8);
    // Far right stops at two thousand pixels.
    rightMove(shown, QPointF(1400, 100));
    QCOMPARE(shown.session.brushSettings().diameter, 2000.0);
    QTest::mouseRelease(shown.canvas, Qt::RightButton, Qt::NoModifier, QPoint(100, 100));
    // Between pixels the size rounds to a whole one.
    settings.diameter = 30;
    shown.session.setBrushSettings(settings);
    shown.session.zoom(0.3);
    rightPress(shown, QPointF(100, 100));
    rightMove(shown, QPointF(110, 100));
    QCOMPARE(shown.session.brushSettings().diameter, 97.0);
    QTest::mouseRelease(shown.canvas, Qt::RightButton, Qt::NoModifier, QPoint(110, 100));
    // Other tools and a held Space leave the right button.
    shown.session.selectTool(NavigationTool::move);
    rightPress(shown, QPointF(100, 100));
    rightMove(shown, QPointF(150, 100));
    QTest::mouseRelease(shown.canvas, Qt::RightButton, Qt::NoModifier, QPoint(150, 100));
    shown.session.selectTool(NavigationTool::brush);
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    QTest::keyPress(shown.canvas, Qt::Key_Space);
    rightPress(shown, QPointF(100, 100));
    rightMove(shown, QPointF(150, 100));
    QTest::mouseRelease(shown.canvas, Qt::RightButton, Qt::NoModifier, QPoint(150, 100));
    QTest::keyRelease(shown.canvas, Qt::Key_Space);
    QCOMPARE(shown.session.brushSettings().diameter, 97.0);
}

void BrushGestureTests::theCircleFollowsThePointerAndLeavesWithIt()
{
    Canvas shown;
    red(shown);
    shown.session.zoom(2);
    shown.canvas->synchronizeDisplay();
    shown.hover(QPointF(120, 80));
    // The brush's size on screen: ten pixels at 200%.
    QCOMPARE(shown.canvas->brushCursor().circle().value(), QRectF(110, 70, 20, 20));
    // The canvas draws it: white halo over the grays.
    const QImage shot = shown.canvas->grab().toImage();
    int light = 0;
    for (int y = 60; y < 100; ++y) {
        for (int x = 100; x < 140; ++x)
            light += qGray(shot.pixel(x, y)) > 180;
    }
    QVERIFY2(light > 10, qPrintable(QString::number(light)));
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    QTest::keyPress(shown.canvas, Qt::Key_Space);
    QVERIFY(!shown.canvas->brushCursor().circle().has_value());
    QTest::keyRelease(shown.canvas, Qt::Key_Space);
    QCOMPARE(shown.canvas->brushCursor().circle().value(), QRectF(110, 70, 20, 20));
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(shown.canvas, &leave);
    QVERIFY(!shown.canvas->brushCursor().circle().has_value());
    shown.hover(QPointF(120, 80));
    shown.session.selectTool(NavigationTool::move);
    shown.canvas->synchronizeDisplay();
    QVERIFY(!shown.canvas->brushCursor().circle().has_value());
    // A pan leaves the circle where the pointer last hovered.
    shown.session.selectTool(NavigationTool::brush);
    shown.canvas->synchronizeDisplay();
    shown.hover(QPointF(100, 100));
    QTest::keyPress(shown.canvas, Qt::Key_Space);
    shown.press(QPointF(100, 100));
    shown.move(QPointF(150, 120));
    shown.release(QPointF(150, 120));
    QTest::keyRelease(shown.canvas, Qt::Key_Space);
    QCOMPARE(shown.canvas->brushCursor().circle().value().center(), QPointF(100, 100));
}

void BrushGestureTests::spotHealingTakesTheBrushGestures()
{
    Canvas shown;
    red(shown);
    shown.session.selectTool(NavigationTool::spotHealing);
    shown.canvas->synchronizeDisplay();
    // The circle, the tip drag, the brackets and the stroke.
    shown.hover(QPointF(100, 100));
    QVERIFY(shown.canvas->brushCursor().circle().has_value());
    rightPress(shown, QPointF(100, 100));
    rightMove(shown, QPointF(110, 100));
    QCOMPARE(shown.session.brushSettings().diameter, 30.0);
    QTest::mouseRelease(shown.canvas, Qt::RightButton, Qt::NoModifier, QPoint(110, 100));
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    QTest::keyClick(shown.canvas, Qt::Key_BracketRight);
    QCOMPARE(shown.session.brushSettings().diameter, 36.0);
    shown.press(QPointF(100, 100));
    QVERIFY(shown.session.brushStroke() && shown.session.brushStroke()->settings.healing);
    shown.move(QPointF(140, 100));
    shown.release(QPointF(140, 100));
    QCOMPARE(shown.session.history.undoName(), QString("Spot Healing"));
}

QTEST_MAIN(BrushGestureTests)
#include "BrushGestureTests.moc"
