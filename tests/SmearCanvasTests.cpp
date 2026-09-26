#include "BrushCanvasFixtures.h"

// The Smear on the canvas: copy, holds, Escape.
namespace {
// Red left of x 200, blue right; Liquify chosen.
QUuid scene(Canvas &shown)
{
    QImage image = BrushRaster::context(400, 300, false);
    QPainter painter(&image);
    painter.fillRect(QRect(0, 0, 200, 300), QColor(255, 0, 0));
    painter.fillRect(QRect(200, 0, 200, 300), QColor(0, 0, 255));
    painter.end();
    shown.session.insert(ImportedImage(image, image, QStringLiteral("Scene")));
    shown.session.selectTool(NavigationTool::blur);
    shown.session.setBrushSettings(BrushSettings{.diameter = 30, .hardness = 0.5});
    shown.canvas->synchronizeDisplay();
    return shown.session.activeLayerID().value();
}

QColor shownAt(Canvas &shown, int x, int y)
{
    return shown.canvas->grab().toImage().pixelColor(x, y);
}
}

class SmearCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void aLiquifyDragShowsTheCopyThenCommits();
    void escapeCancelsAWarp();
    void aWarpHoldsTheViewAndTheTip();
    void theCopyShowsThroughTheLayersMask();
};

void SmearCanvasTests::aLiquifyDragShowsTheCopyThenCommits()
{
    Canvas shown;
    scene(shown);
    shown.press(QPointF(180, 150));
    QVERIFY(shown.session.warpStroke());
    shown.move(QPointF(230, 150));
    shown.canvas->synchronizeDisplay();
    // The canvas shows red pushed right; nothing is committed yet.
    QCOMPARE(shownAt(shown, 210, 150), QColor(255, 0, 0));
    QCOMPARE(pixel(shown.session, 210, 150), (std::vector<int>{0, 0, 255, 255}));
    shown.release(QPointF(230, 150));
    QVERIFY(!shown.session.warpStroke());
    QCOMPARE(pixel(shown.session, 210, 150), (std::vector<int>{255, 0, 0, 255}));
    QCOMPARE(shown.session.history.undoName(), QString("Liquify"));
    QCOMPARE(shownAt(shown, 210, 150), QColor(255, 0, 0));
    QCOMPARE(shownAt(shown, 210, 40), QColor(0, 0, 255));
}

void SmearCanvasTests::escapeCancelsAWarp()
{
    Canvas shown;
    scene(shown);
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    const int steps = shown.session.history.undoCount();
    shown.press(QPointF(180, 150));
    shown.move(QPointF(230, 150));
    QTest::keyClick(shown.canvas, Qt::Key_Escape);
    QVERIFY(!shown.session.warpStroke());
    shown.release(QPointF(230, 150));
    QCOMPARE(shown.session.history.undoCount(), steps);
    shown.canvas->synchronizeDisplay();
    QCOMPARE(shownAt(shown, 210, 150), QColor(0, 0, 255));
    // Other keys wait while a warp lasts.
    shown.press(QPointF(180, 150));
    QTest::keyClick(shown.canvas, Qt::Key_B);
    QCOMPARE(shown.session.tool(), NavigationTool::blur);
    shown.release(QPointF(180, 150));
}

void SmearCanvasTests::aWarpHoldsTheViewAndTheTip()
{
    Canvas shown;
    scene(shown);
    shown.press(QPointF(180, 150));
    const CanvasViewport before = shown.session.viewport;
    wheel(*shown.canvas, QPointF(180, 150), QPoint(0, 40), QPoint(0, 120), Qt::NoModifier);
    QCOMPARE(shown.session.viewport.zoom(), before.zoom());
    QCOMPARE(shown.session.viewport.documentPoint(QPointF(0, 0), shown.documentSize()), before.documentPoint(QPointF(0, 0), shown.documentSize()));
    QNativeGestureEvent pinch(Qt::ZoomNativeGesture, QPointingDevice::primaryPointingDevice(), 2, QPointF(180, 150), QPointF(180, 150),
                              shown.canvas->mapToGlobal(QPointF(180, 150)), 0.5, QPointF(), 0);
    QApplication::sendEvent(shown.canvas, &pinch);
    QCOMPARE(shown.session.viewport.zoom(), before.zoom());
    // A right press sizes no tip mid-warp.
    QTest::mousePress(shown.canvas, Qt::RightButton, Qt::NoModifier, QPoint(180, 150));
    QMouseEvent drag(QEvent::MouseMove, QPointF(260, 150), QPointF(260, 150), shown.canvas->mapToGlobal(QPoint(260, 150)), Qt::NoButton,
                     Qt::LeftButton | Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(shown.canvas, &drag);
    QCOMPARE(shown.session.brushSettings().diameter, 30.0);
    QTest::mouseRelease(shown.canvas, Qt::RightButton, Qt::NoModifier, QPoint(260, 150));
    shown.release(QPointF(260, 150));
    QVERIFY(!shown.session.warpStroke());
}

void SmearCanvasTests::theCopyShowsThroughTheLayersMask()
{
    Canvas shown;
    const QUuid id = scene(shown);
    // The mask hides the lower half, during the warp too.
    QImage mask(400, 300, QImage::Format_Grayscale8);
    mask.fill(255);
    for (int y = 200; y < 300; ++y)
        std::fill_n(mask.scanLine(y), 400, uchar(0));
    rewrite(shown.session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, id, LayerMask::assetFrom(mask)); });
    // Installing fits the view; back to one pixel a point.
    shown.session.zoom(1);
    shown.session.selectTool(NavigationTool::blur);
    shown.canvas->synchronizeDisplay();
    QCOMPARE(shown.session.viewport.viewPoint(QPointF(300, 40), shown.documentSize()), QPointF(300, 40));
    const QColor ground = shownAt(shown, 210, 250);
    shown.press(QPointF(180, 150));
    shown.move(QPointF(230, 150));
    shown.move(QPointF(230, 250));
    shown.canvas->synchronizeDisplay();
    QCOMPARE(shownAt(shown, 210, 150), QColor(255, 0, 0));
    QCOMPARE(shownAt(shown, 210, 250), ground);
    shown.release(QPointF(230, 250));
    // At half opacity the copy stays half, where nothing moved.
    shown.session.selectTool(NavigationTool::blur);
    shown.session.setLayerOpacity(0.5);
    shown.canvas->synchronizeDisplay();
    const QColor faint = shownAt(shown, 300, 40);
    QVERIFY(faint != QColor(0, 0, 255));
    shown.press(QPointF(180, 150));
    QVERIFY(shown.session.warpStroke());
    shown.move(QPointF(230, 150));
    shown.canvas->synchronizeDisplay();
    QCOMPARE(shownAt(shown, 300, 40), faint);
    shown.release(QPointF(230, 150));
}

QTEST_MAIN(SmearCanvasTests)
#include "SmearCanvasTests.moc"
