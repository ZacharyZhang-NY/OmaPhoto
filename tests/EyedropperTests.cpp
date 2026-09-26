#include "ContentView.h"
#include "EyedropperFixtures.h"
#include <QCheckBox>

// The Eyedropper: sampling, its ring, its cursor, its bar.
class EyedropperTests : public QObject {
    Q_OBJECT
private slots:
    void theEyedropperSamplesTheCompositeIntoTheForeground();
    void theRingShowsTheSampleOverTheOriginal();
    void pickingShowsTheEyedropperCursor();
    void spaceStillPansWhilePicking();
    void theBrushCircleHidesWhilePicking();
    void aMountedCanvasLeavesThePickerTheKeys();
    void theBarTogglesTheRing();
};

void EyedropperTests::theEyedropperSamplesTheCompositeIntoTheForeground()
{
    Picking shown;
    shown.press(QPointF(100, 150));
    QCOMPARE(shown.foreground(), QString("FF0000"));
    shown.move(QPointF(300, 150));
    QCOMPARE(shown.foreground(), QString("0000FF"));
    shown.release(QPointF(300, 150));
    QCOMPARE(shown.foreground(), QString("0000FF"));
    // Past the document nothing is sampled; the colour stays.
    shown.press(QPointF(100, 150));
    shown.move(QPointF(-20, 150));
    QCOMPARE(shown.foreground(), QString("FF0000"));
    // Busy, the palette rests: the drag samples nothing.
    shown.session.setIsProjectBusy(true);
    shown.move(QPointF(300, 150));
    QCOMPARE(shown.foreground(), QString("FF0000"));
    shown.release(QPointF(300, 150));
    // A busy press samples nothing either.
    shown.click(QPointF(300, 150));
    QCOMPARE(shown.foreground(), QString("FF0000"));
    shown.session.setIsProjectBusy(false);
    // With a mask chosen the foreground takes it, as Swift's.
    shown.session.addMask();
    QVERIFY(shown.session.isMaskSelected());
    shown.click(QPointF(300, 150));
    QCOMPARE(shown.foreground(), QString("0000FF"));
    QVERIFY(!shown.session.maskPaintWhite());
}

void EyedropperTests::theRingShowsTheSampleOverTheOriginal()
{
    Picking shown;
    shown.session.setForegroundColor(PaletteColor{0, 1, 0});
    shown.press(QPointF(100, 150));
    QCOMPARE(shown.ring().frame().value(), QRectF(42, 92, 116, 116));
    QCOMPARE(shown.ring().original(), (PaletteColor{0, 1, 0}));
    QCOMPARE(shown.ring().sampled().hex(), QString("FF0000"));
    // The new colour above, the old below, gray around both.
    const QImage shot = shown.canvas->grab().toImage();
    QCOMPARE(shot.pixelColor(100, 107), QColor(Qt::red));
    QCOMPARE(shot.pixelColor(100, 193), QColor(Qt::green));
    for (const int y : {97, 117}) {
        const QColor rim = shot.pixelColor(100, y);
        QVERIFY2(std::abs(rim.red() - 115) <= 1 && rim.red() == rim.green() && rim.green() == rim.blue(), qPrintable(rim.name()));
    }
    QCOMPARE(shot.pixelColor(100, 150), QColor(Qt::red));
    // Sixteen points of colour, eight each side of the line.
    QCOMPARE(shot.pixelColor(100, 100), QColor(Qt::red));
    QCOMPARE(shot.pixelColor(100, 114), QColor(Qt::red));
    // Antialiased: the gray's outer edge mixes with the red.
    const QColor edge = shot.pixelColor(139, 111);
    QVERIFY2(edge.red() > 115 && edge.red() < 255 && edge.green() > 0 && edge.green() < 115, qPrintable(edge.name()));
    // It follows the drag, both places repainted; the original stays.
    QCoreApplication::processEvents();
    PaintSpy paints(*shown.canvas);
    shown.move(QPointF(300, 150));
    QCOMPARE(shown.ring().frame().value(), QRectF(242, 92, 116, 116));
    QCOMPARE(shown.ring().original(), (PaletteColor{0, 1, 0}));
    QCOMPARE(shown.ring().sampled().hex(), QString("0000FF"));
    QTRY_VERIFY(paints.painted.contains(QRect(42, 92, 316, 116)));
    QCoreApplication::processEvents();
    PaintSpy hidden(*shown.canvas);
    shown.release(QPointF(300, 150));
    QVERIFY(!shown.ring().frame());
    QTRY_VERIFY(hidden.painted.contains(QRect(242, 92, 116, 116)));
    QCOMPARE(shown.canvas->grab().toImage().pixelColor(300, 107), QColor(Qt::blue));
    // Off in the bar, sampling shows no ring.
    shown.session.setShowsSampleRing(false);
    shown.press(QPointF(100, 150));
    QVERIFY(!shown.ring().frame());
    QCOMPARE(shown.foreground(), QString("FF0000"));
    shown.release(QPointF(100, 150));
}

void EyedropperTests::pickingShowsTheEyedropperCursor()
{
    Picking shown;
    QVERIFY(shown.picks());
    // The tip is the hot spot; white rings the glyph.
    const QCursor eyedropper = CanvasView::eyedropperCursor(1);
    QCOMPARE(eyedropper.hotSpot(), QPoint(5, 19));
    const QImage glyph = eyedropper.pixmap().toImage();
    QCOMPARE(glyph.size(), QSize(24, 24));
    QCOMPARE(glyph.pixelColor(5, 18), QColor(Qt::black));
    QCOMPARE(glyph.pixelColor(17, 5), QColor(Qt::black));
    const QColor halo = glyph.pixelColor(5, 20);
    QVERIFY2(halo.red() == 255 && halo.green() == 255 && halo.blue() == 255 && halo.alpha() > 200, qPrintable(halo.name(QColor::HexArgb)));
    QCOMPARE(glyph.pixelColor(2, 18).alpha(), 0);
    // Sixteen offsets smooth the halo: eight leave 67 here.
    QVERIFY2(std::abs(glyph.pixelColor(10, 4).alpha() - 54) <= 4, qPrintable(glyph.pixelColor(10, 4).name(QColor::HexArgb)));
    // Space leaves it, as Swift's cursor rects rank it first.
    QTest::keyPress(shown.canvas, Qt::Key_Space);
    QVERIFY(shown.picks());
    QTest::keyRelease(shown.canvas, Qt::Key_Space);
    // The Brush's crosshair until Alt is held.
    shown.tool(NavigationTool::brush);
    QCOMPARE(shown.canvas->cursor().shape(), Qt::CrossCursor);
    QTest::keyPress(shown.canvas, Qt::Key_Alt, Qt::AltModifier);
    QVERIFY(shown.picks());
    QTest::keyRelease(shown.canvas, Qt::Key_Alt);
    QCOMPARE(shown.canvas->cursor().shape(), Qt::CrossCursor);
    // A hover under Alt, its key unseen, shows it too.
    shown.hover(QPointF(100, 150), Qt::AltModifier);
    QVERIFY(shown.picks());
    shown.hover(QPointF(100, 150));
    // Clone Stamp's Alt picks a source, not a colour.
    shown.tool(NavigationTool::cloneStamp);
    QTest::keyPress(shown.canvas, Qt::Key_Alt, Qt::AltModifier);
    QCOMPARE(shown.canvas->cursor().shape(), Qt::CrossCursor);
    QTest::keyRelease(shown.canvas, Qt::Key_Alt);
}

void EyedropperTests::spaceStillPansWhilePicking()
{
    Picking shown;
    QTest::keyPress(shown.canvas, Qt::Key_Space);
    const QPointF before = shown.session.viewport.documentPoint(QPointF(200, 150), shown.documentSize());
    shown.press(QPointF(100, 150));
    QCOMPARE(shown.canvas->cursor().shape(), Qt::ClosedHandCursor);
    shown.move(QPointF(130, 150));
    QCOMPARE(shown.session.viewport.documentPoint(QPointF(200, 150), shown.documentSize()), before - QPointF(30, 0));
    QCOMPARE(shown.foreground(), QString("000000"));
    QVERIFY(!shown.ring().frame());
    shown.release(QPointF(130, 150));
    QVERIFY(shown.picks());
    QTest::keyRelease(shown.canvas, Qt::Key_Space);
}

void EyedropperTests::theBrushCircleHidesWhilePicking()
{
    Picking shown;
    shown.tool(NavigationTool::brush);
    shown.hover(QPointF(100, 150));
    QVERIFY(shown.canvas->brushCursor().circle());
    QTest::keyPress(shown.canvas, Qt::Key_Alt, Qt::AltModifier);
    QVERIFY(!shown.canvas->brushCursor().circle());
    // Picking's hover moves no circle, as Swift's.
    shown.hover(QPointF(300, 150), Qt::AltModifier);
    QTest::keyRelease(shown.canvas, Qt::Key_Alt);
    QCOMPARE(shown.canvas->brushCursor().circle().value().center(), QPointF(100, 150));
    shown.hover(QPointF(300, 150));
    QCOMPARE(shown.canvas->brushCursor().circle().value().center(), QPointF(300, 150));
}

void EyedropperTests::aMountedCanvasLeavesThePickerTheKeys()
{
    Picking shown;
    shown.session.openColorPicker(false);
    shown.canvas->clearFocus();
    shown.canvas->hide();
    shown.canvas->show();
    QTest::qWait(50);
    QVERIFY(!shown.canvas->hasFocus());
    // Without a picker, a mounted canvas takes them.
    shown.session.closeColorPicker(false);
    shown.canvas->hide();
    shown.canvas->show();
    QTRY_VERIFY(shown.canvas->hasFocus());
}

void EyedropperTests::theBarTogglesTheRing()
{
    EditorSession session;
    session.createDocument(40, 40);
    session.selectTool(NavigationTool::eyedropper);
    ContentView view(session);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto *ring = view.findChild<QCheckBox *>(QStringLiteral("sampleRing"));
    QVERIFY(ring);
    QCOMPARE(ring->text(), QString("Sample Ring"));
    QVERIFY(ring->isChecked());
    QSignalSpy changed(&session, &EditorSession::changed);
    // Title first, the checkbox 16 points on, as Swift's.
    const QLabel *title = ring->parentWidget()->findChild<QLabel *>();
    QCOMPARE(title->text(), QString("Eyedropper"));
    QCOMPARE(ring->geometry().left() - title->geometry().right() - 1, 16);
    ring->click();
    QVERIFY(!session.showsSampleRing());
    QCOMPARE(changed.count(), 1);
    ring->click();
    QVERIFY(session.showsSampleRing());
    // Set elsewhere, the checkbox follows without writing back.
    session.setShowsSampleRing(false);
    QVERIFY(!ring->isChecked());
    QCOMPARE(changed.count(), 3);
}

QTEST_MAIN(EyedropperTests)
#include "EyedropperTests.moc"
