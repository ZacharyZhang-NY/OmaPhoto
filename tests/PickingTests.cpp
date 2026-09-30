#include "EyedropperFixtures.h"
#include <QLineEdit>

// Picking beside tools, the open picker and lost releases.
class PickingTests : public QObject {
    Q_OBJECT
private slots:
    void altPicksUnderTheBrushSpotHealingAndTheGradient();
    void altLeavesAStrokeOrAGradientDragAlone();
    void theOpenPickerTakesSamplesAndTheKeysBack();
    void pickingEndingEndsTheSampling();
    void altFirstSeenInThePressSamplesOn();
    void aRightDragSizesTheTipWhilePicking();
    void aLostReleaseEndsTheSampling();
    void theInlineEditorStandsAsideForThePicker();
    void aPolygonalLassoWaitsForThePicker();
    void openTextTakesTheKeysBackFromThePicker();
};

void PickingTests::altPicksUnderTheBrushSpotHealingAndTheGradient()
{
    Picking shown;
    for (const NavigationTool tool : {NavigationTool::brush, NavigationTool::spotHealing, NavigationTool::gradient}) {
        shown.tool(tool);
        shown.session.resetPaletteColors();
        shown.press(QPointF(100, 150), Qt::AltModifier);
        QVERIFY(shown.ring().frame());
        QVERIFY(!shown.session.brushStroke() && !shown.session.gradientEdit());
        shown.release(QPointF(100, 150), Qt::AltModifier);
        QCOMPARE(shown.foreground(), QString("FF0000"));
    }
    // Clone Stamp's Alt sets its source instead.
    shown.tool(NavigationTool::cloneStamp);
    shown.session.resetPaletteColors();
    shown.click(QPointF(300, 150), Qt::AltModifier);
    QVERIFY(shown.session.cloneSource());
    QCOMPARE(shown.foreground(), QString("000000"));
    // The other tools take Alt for their own.
    shown.tool(NavigationTool::shape);
    shown.click(QPointF(300, 150), Qt::AltModifier);
    QCOMPARE(shown.foreground(), QString("000000"));
}

void PickingTests::altLeavesAStrokeOrAGradientDragAlone()
{
    Picking shown;
    // A stroke begun paints on under Alt.
    shown.tool(NavigationTool::brush);
    shown.press(QPointF(100, 150));
    QVERIFY(shown.session.brushStroke());
    QTest::keyPress(shown.canvas, Qt::Key_Alt, Qt::AltModifier);
    QVERIFY(!shown.picks());
    shown.move(QPointF(300, 150), Qt::AltModifier);
    QVERIFY(shown.session.brushStroke() && !shown.ring().frame());
    shown.session.cancelBrush();
    shown.release(QPointF(300, 150), Qt::AltModifier);
    QTest::keyRelease(shown.canvas, Qt::Key_Alt);
    QCOMPARE(shown.foreground(), QString("000000"));
    // So does a gradient's end being dragged.
    shown.tool(NavigationTool::gradient);
    shown.press(QPointF(100, 150));
    QTest::keyPress(shown.canvas, Qt::Key_Alt, Qt::AltModifier);
    QVERIFY(!shown.picks());
    shown.move(QPointF(300, 150), Qt::AltModifier);
    QCOMPARE(shown.session.gradientEdit().value().end, QPointF(300, 150));
    QVERIFY(!shown.ring().frame());
    shown.release(QPointF(300, 150), Qt::AltModifier);
    QTest::keyRelease(shown.canvas, Qt::Key_Alt);
    QCOMPARE(shown.foreground(), QString("000000"));
    // A refused gradient press drags nothing: Alt picks there.
    shown.session.cancelGradient();
    shown.session.toggleLayerVisibility(shown.session.activeLayerID().value());
    shown.press(QPointF(100, 150));
    QVERIFY(!shown.session.gradientEdit());
    QTest::keyPress(shown.canvas, Qt::Key_Alt, Qt::AltModifier);
    QVERIFY(shown.picks());
    shown.release(QPointF(100, 150), Qt::AltModifier);
    QTest::keyRelease(shown.canvas, Qt::Key_Alt);
}

void PickingTests::theOpenPickerTakesSamplesAndTheKeysBack()
{
    Picking shown;
    shown.tool(NavigationTool::brush);
    ColorPickerPanelController controller(shown.window);
    shown.session.openColorPicker(false);
    controller.show(shown.session.colorPicker().value(), shown.session);
    QWidget *panel = pickerPanel();
    QVERIFY(panel);
    QTRY_COMPARE(QApplication::activeWindow(), panel);
    // Any tool picks while it is open.
    shown.canvas->synchronizeDisplay();
    QVERIFY(shown.picks());
    shown.window.activateWindow();
    QTRY_COMPARE(QApplication::activeWindow(), &shown.window);
    // The ring's original is the picker's colour, not the swatch's.
    shown.session.setColorPickerHSB(PickerHSB(120, 1, 1));
    shown.press(QPointF(300, 150));
    QCOMPARE(shown.session.colorPicker().value().color().hex(), QString("0000FF"));
    QCOMPARE(shown.foreground(), QString("000000"));
    QCOMPARE(shown.ring().original(), (PaletteColor{0, 1, 0}));
    QCOMPARE(shown.ring().sampled().hex(), QString("0000FF"));
    QVERIFY(!shown.session.brushStroke());
    // The release hands the picker the keys back.
    shown.release(QPointF(300, 150));
    QTRY_COMPARE(QApplication::activeWindow(), panel);
    // A pan's release, sampling nothing, leaves them.
    shown.window.activateWindow();
    QTRY_COMPARE(QApplication::activeWindow(), &shown.window);
    QTest::keyPress(shown.canvas, Qt::Key_Space);
    shown.drag(QPointF(300, 150), QPointF(310, 150));
    QTest::keyRelease(shown.canvas, Qt::Key_Space);
    QTest::qWait(50);
    QCOMPARE(QApplication::activeWindow(), &shown.window);
    // Closed, the Brush paints again.
    shown.session.closeColorPicker(false);
    controller.close();
    shown.canvas->synchronizeDisplay();
    QVERIFY(!shown.picks());
}

void PickingTests::pickingEndingEndsTheSampling()
{
    Picking shown;
    shown.tool(NavigationTool::brush);
    QTest::keyPress(shown.canvas, Qt::Key_Alt, Qt::AltModifier);
    shown.press(QPointF(100, 150), Qt::AltModifier);
    QVERIFY(shown.ring().frame());
    // Alt let go mid-drag: the ring goes, sampling stops.
    QTest::keyRelease(shown.canvas, Qt::Key_Alt);
    QVERIFY(!shown.ring().frame());
    shown.move(QPointF(300, 150));
    QCOMPARE(shown.foreground(), QString("FF0000"));
    QVERIFY(!shown.session.brushStroke());
    shown.release(QPointF(300, 150));
    // Another tool ends it too.
    shown.tool(NavigationTool::eyedropper);
    shown.press(QPointF(300, 150));
    shown.tool(NavigationTool::brush);
    QVERIFY(!shown.ring().frame());
    shown.move(QPointF(100, 150));
    QCOMPARE(shown.foreground(), QString("0000FF"));
    shown.release(QPointF(100, 150));
}

void PickingTests::altFirstSeenInThePressSamplesOn()
{
    // As ContentView makes it: every change syncs the canvas.
    Picking shown;
    QObject::connect(&shown.session, &EditorSession::changed, shown.canvas, [&shown] { shown.canvas->synchronizeDisplay(); });
    shown.tool(NavigationTool::brush);
    shown.press(QPointF(100, 150), Qt::AltModifier);
    QVERIFY(shown.ring().frame());
    shown.move(QPointF(300, 150), Qt::AltModifier);
    QCOMPARE(shown.foreground(), QString("0000FF"));
    QCOMPARE(shown.ring().frame().value(), QRectF(242, 92, 116, 116));
    shown.release(QPointF(300, 150), Qt::AltModifier);
    QVERIFY(!shown.ring().frame());
    // Alt let go unseen mid-drag: the move ends the sampling.
    shown.press(QPointF(100, 150), Qt::AltModifier);
    shown.move(QPointF(300, 150));
    QVERIFY(!shown.ring().frame());
    QCOMPARE(shown.foreground(), QString("FF0000"));
    shown.release(QPointF(300, 150));
}

void PickingTests::aRightDragSizesTheTipWhilePicking()
{
    Picking shown;
    shown.tool(NavigationTool::brush);
    const auto rightDrag = [&shown](Qt::KeyboardModifiers modifiers) {
        QTest::mousePress(shown.canvas, Qt::RightButton, modifiers, QPoint(100, 150));
        QMouseEvent move(QEvent::MouseMove, QPointF(120, 150), QPointF(120, 150), shown.canvas->mapToGlobal(QPoint(120, 150)), Qt::NoButton,
                         Qt::RightButton, modifiers);
        QApplication::sendEvent(shown.canvas, &move);
        QTest::mouseRelease(shown.canvas, Qt::RightButton, modifiers, QPoint(120, 150));
    };
    // Alt held: the right-drag still sizes the tip, as Swift's.
    const double diameter = shown.session.brushSettings().diameter;
    QTest::keyPress(shown.canvas, Qt::Key_Alt, Qt::AltModifier);
    rightDrag(Qt::AltModifier);
    QCOMPARE(shown.session.brushSettings().diameter, diameter + 40);
    QTest::keyRelease(shown.canvas, Qt::Key_Alt);
    // So with the picker open.
    shown.session.openColorPicker(false);
    shown.canvas->synchronizeDisplay();
    rightDrag(Qt::NoModifier);
    QCOMPARE(shown.session.brushSettings().diameter, diameter + 80);
    shown.session.closeColorPicker(false);
}

void PickingTests::aLostReleaseEndsTheSampling()
{
    Picking shown;
    // A move without the button: its release was lost.
    shown.press(QPointF(100, 150));
    shown.hover(QPointF(300, 150));
    QVERIFY(!shown.ring().frame());
    QCOMPARE(shown.foreground(), QString("FF0000"));
    shown.hover(QPointF(310, 150));
    QCOMPARE(shown.foreground(), QString("FF0000"));
    // A lost focus ends it too.
    shown.press(QPointF(300, 150));
    QVERIFY(shown.ring().frame());
    shown.canvas->clearFocus();
    QVERIFY(!shown.ring().frame());
    shown.move(QPointF(100, 150));
    QCOMPARE(shown.foreground(), QString("0000FF"));
    // So does the next press: with Space it pans instead.
    shown.canvas->setFocus();
    shown.press(QPointF(100, 150));
    QTest::keyPress(shown.canvas, Qt::Key_Space);
    const QPointF before = shown.session.viewport.documentPoint(QPointF(200, 150), shown.documentSize());
    shown.press(QPointF(300, 150));
    QVERIFY(!shown.ring().frame());
    shown.move(QPointF(320, 150));
    QCOMPARE(shown.foreground(), QString("FF0000"));
    QCOMPARE(shown.session.viewport.documentPoint(QPointF(200, 150), shown.documentSize()), before - QPointF(20, 0));
    shown.release(QPointF(320, 150));
    QTest::keyRelease(shown.canvas, Qt::Key_Space);
}

void PickingTests::theInlineEditorStandsAsideForThePicker()
{
    Picking shown;
    QObject::connect(&shown.session, &EditorSession::changed, shown.canvas, [&shown] { shown.canvas->synchronizeDisplay(); });
    shown.tool(NavigationTool::type);
    beginTextAt(shown.session, QPointF(20, 30));
    QTest::keyClicks(shown.canvas, QStringLiteral("Hello there"));
    QTest::keyClick(shown.canvas, Qt::Key_Home);
    const InlineTextEditor &editor = *shown.canvas->inlineTextEditor();
    const QPointF word = editor.textTransform().map(QPointF(LayerTextStyle::padding + 4, LayerTextStyle::padding + 4));
    const QImage before = shown.canvas->grab(QRect(0, 0, 200, 140)).toImage();
    shown.session.openTextColorPicker();
    // A sampling ring elsewhere leaves the editor drawn whole.
    shown.press(QPointF(300, 200));
    QVERIFY(shown.ring().frame());
    QCOMPARE(shown.canvas->grab(QRect(0, 0, 200, 140)).toImage(), before);
    shown.release(QPointF(300, 200));
    // Over the text the eyedropper shows; clicks sample.
    shown.hover(word);
    QVERIFY(shown.picks());
    shown.click(word);
    QCOMPARE(shown.session.colorPicker().value().color().hex(), QString("FF0000"));
    QVERIFY(editor.anchor() == 0 && editor.caretPosition() == 0);
    // A double click samples too, selecting no word.
    shown.session.setColorPickerHSB(PickerHSB(240, 1, 1));
    QTest::mouseDClick(shown.canvas, Qt::LeftButton, Qt::NoModifier, word.toPoint());
    QCOMPARE(shown.session.colorPicker().value().color().hex(), QString("FF0000"));
    QVERIFY(editor.anchor() == 0 && editor.caretPosition() == 0);
    // Closed, the text view takes clicks again.
    shown.session.closeColorPicker(false);
    QTest::mouseDClick(shown.canvas, Qt::LeftButton, Qt::NoModifier, word.toPoint());
    QVERIFY(editor.anchor() == 0 && editor.caretPosition() == 5);
}

void PickingTests::aPolygonalLassoWaitsForThePicker()
{
    Picking shown;
    shown.tool(NavigationTool::lasso);
    shown.session.setLassoKind(LassoKind::polygonal);
    shown.click(QPointF(50, 50));
    shown.hover(QPointF(80, 60));
    const LassoDraft draft = shown.session.lassoDraft().value();
    shown.session.openColorPicker(false);
    shown.canvas->synchronizeDisplay();
    // A sampling drag adds no corner, moves no rubber band.
    shown.press(QPointF(300, 150));
    shown.move(QPointF(310, 160));
    QCOMPARE(shown.session.colorPicker().value().color().hex(), QString("0000FF"));
    QCOMPARE(shown.session.lassoDraft().value(), draft);
    shown.release(QPointF(310, 160));
    // A double click samples, the outline still open.
    QTest::mouseDClick(shown.canvas, Qt::LeftButton, Qt::NoModifier, QPoint(100, 150));
    QCOMPARE(shown.session.colorPicker().value().color().hex(), QString("FF0000"));
    QCOMPARE(shown.session.lassoDraft().value(), draft);
    shown.session.closeColorPicker(false);
}

void PickingTests::openTextTakesTheKeysBackFromThePicker()
{
    // Swift's 810c88a: closing either picker returns the keys to text.
    Picking shown;
    QObject::connect(&shown.session, &EditorSession::changed, shown.canvas, [&shown] {
        shown.canvas->consumeFocusRequest(shown.session.canvasFocusRequest());
        shown.canvas->synchronizeDisplay();
    });
    auto *field = new QLineEdit(&shown.window);
    field->setGeometry(0, 0, 10, 10);
    field->show();
    shown.tool(NavigationTool::type);
    QString typed;
    for (const bool existing : {false, true}) {
        if (existing) {
            QVERIFY(shown.session.finishText());
            shown.session.editActiveText();
            QVERIFY(shown.session.textDraft().value().layerID.has_value());
        } else {
            beginTextAt(shown.session, QPointF(20, 30));
        }
        // The foreground, the background, the Type bar's.
        for (const int picker : {0, 1, 2}) {
            for (const bool commit : {false, true}) {
                picker == 2 ? shown.session.openTextColorPicker() : shown.session.openColorPicker(picker == 1);
                QVERIFY(shown.session.colorPicker().has_value());
                field->setFocus();
                QTRY_VERIFY(field->hasFocus());
                const int requests = shown.session.canvasFocusRequest();
                shown.session.closeColorPicker(commit);
                QCOMPARE(shown.session.canvasFocusRequest(), requests + 1);
                QTRY_VERIFY(shown.canvas->hasFocus());
                QTest::keyClicks(shown.canvas, QStringLiteral("a"));
                typed += u'a';
            }
        }
        QCOMPARE(shown.session.textDraft().value().style.content, typed);
    }
    // Ctrl+Return reaches the text: it applies.
    QTest::keyClick(shown.canvas, Qt::Key_Return, Qt::ControlModifier);
    QVERIFY(!shown.session.textDraft());
    QCOMPARE(shown.session.activeLayer().value().liveText().value().style.content, QString(12, u'a'));
    // Any focus request hands open text the keys.
    shown.session.editActiveText();
    field->setFocus();
    QTRY_VERIFY(field->hasFocus());
    shown.session.requestCanvasFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    // No open text: a closed picker asks nothing, field kept.
    QVERIFY(shown.session.finishText());
    QTRY_VERIFY(shown.canvas->hasFocus());
    for (const int picker : {0, 1, 2}) {
        for (const bool commit : {false, true}) {
            field->setFocus();
            QTRY_VERIFY(field->hasFocus());
            const int requests = shown.session.canvasFocusRequest();
            picker == 2 ? shown.session.openTextColorPicker() : shown.session.openColorPicker(picker == 1);
            QVERIFY(shown.session.colorPicker().has_value());
            shown.session.closeColorPicker(commit);
            QCOMPARE(shown.session.canvasFocusRequest(), requests);
            QTest::qWait(20);
            QVERIFY(field->hasFocus());
        }
    }
}

QTEST_MAIN(PickingTests)
#include "PickingTests.moc"
