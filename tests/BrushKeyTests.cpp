#include "BrushCanvasFixtures.h"
#include "UI/NativeLayerList.h"
#include <QLineEdit>

// Swift's brush keys: Tab, B, E, digits, brackets.
class BrushKeyTests : public QObject {
    Q_OBJECT
private slots:
    void aStrokeTakesEveryKeyAndEscapeCancelsIt();
    void tabCyclesModesAndBAndEChooseThem();
    void digitsSetTheBrushOpacity();
    void shiftBracketsStepHardnessFromTheCanvas();
    void bracketKeysReachTheBrushWhereverFocusIsExceptTextFields();
    void theCanvasTakesItsOwnBracketsAndRereadsAlt();
};

void BrushKeyTests::aStrokeTakesEveryKeyAndEscapeCancelsIt()
{
    Canvas shown;
    red(shown);
    const int count = shown.session.history.undoCount();
    shown.press(QPointF(100, 100));
    QTRY_VERIFY(shown.canvas->hasFocus());
    QTest::keyClick(shown.canvas, Qt::Key_V);
    QTest::keyClick(shown.canvas, Qt::Key_BracketRight);
    QVERIFY(shown.session.tool() == NavigationTool::brush && shown.session.brushSettings().diameter == 10);
    QVERIFY(shown.session.brushStroke());
    QTest::keyClick(shown.canvas, Qt::Key_Escape);
    QVERIFY(!shown.session.brushStroke());
    shown.release(QPointF(100, 100));
    QCOMPARE(shown.session.history.undoCount(), count);
    QCOMPARE(pixel(shown.session, 100, 100)[3], 0);
}

void BrushKeyTests::tabCyclesModesAndBAndEChooseThem()
{
    Canvas shown;
    // Tab stays the canvas's though focus could move on.
    (new QLineEdit(&shown.window))->show();
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    shown.hover(QPointF(200, 150));
    QTest::keyClick(shown.canvas, Qt::Key_Tab);
    QCOMPARE(shown.session.marqueeKind(), LassoKind::ellipse);
    QVERIFY(shown.canvas->hasFocus());
    // The cursor takes the new kind at once.
    QVERIFY(shown.shows(CanvasView::selectionCursor(SelectionIcon::ellipseMarquee, SelectionMode::replace, shown.canvas->devicePixelRatio())));
    QTest::keyClick(shown.canvas, Qt::Key_E);
    QVERIFY(shown.session.tool() == NavigationTool::brush && shown.session.brushMode() == BrushToolMode::erase);
    QTest::keyClick(shown.canvas, Qt::Key_Tab);
    QCOMPARE(shown.session.brushMode(), BrushToolMode::paint);
    QTest::keyClick(shown.canvas, Qt::Key_Tab);
    QCOMPARE(shown.session.brushMode(), BrushToolMode::erase);
    QTest::keyClick(shown.canvas, Qt::Key_B);
    QCOMPARE(shown.session.brushMode(), BrushToolMode::paint);
    // Shift-Tab is the focus chain's, not the mode's.
    QTest::keyClick(shown.canvas, Qt::Key_Backtab, Qt::ShiftModifier);
    QCOMPARE(shown.session.brushMode(), BrushToolMode::paint);
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    QTest::keyClick(shown.canvas, Qt::Key_Tab, Qt::ShiftModifier);
    QCOMPARE(shown.session.brushMode(), BrushToolMode::paint);
    // With Ctrl or Alt the letters are someone else's.
    QTest::keyClick(shown.canvas, Qt::Key_E, Qt::AltModifier);
    QTest::keyClick(shown.canvas, Qt::Key_E, Qt::ControlModifier);
    QCOMPARE(shown.session.brushMode(), BrushToolMode::paint);
}

void BrushKeyTests::digitsSetTheBrushOpacity()
{
    Canvas shown;
    red(shown);
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    QTest::keyClick(shown.canvas, Qt::Key_3);
    QCOMPARE(shown.session.brushSettings().opacity, 0.3);
    QTest::qWait(700);
    QTest::keyClick(shown.canvas, Qt::Key_0);
    QCOMPARE(shown.session.brushSettings().opacity, 1.0);
    // The Move tool's digits set the layer's opacity.
    shown.session.selectTool(NavigationTool::move);
    QTest::qWait(700);
    QTest::keyClick(shown.canvas, Qt::Key_5);
    QCOMPARE(shown.session.activeLayer().value().opacity, 0.5);
    // Where digits set nothing, the key goes to the parent.
    shown.session.selectTool(NavigationTool::hand);
    QKeyEvent digit(QEvent::KeyPress, Qt::Key_5, Qt::NoModifier, QStringLiteral("5"));
    QApplication::sendEvent(shown.canvas, &digit);
    QVERIFY(!digit.isAccepted());
}

void BrushKeyTests::shiftBracketsStepHardnessFromTheCanvas()
{
    Canvas shown;
    red(shown);
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    BrushSettings settings = shown.session.brushSettings();
    settings.hardness = 0.5;
    shown.session.setBrushSettings(settings);
    QTest::keyClick(shown.canvas, Qt::Key_BraceRight, Qt::ShiftModifier);
    QCOMPARE(shown.session.brushSettings().hardness, 0.75);
    QTest::keyClick(shown.canvas, Qt::Key_BraceLeft, Qt::ShiftModifier);
    QTest::keyClick(shown.canvas, Qt::Key_BraceLeft, Qt::ShiftModifier);
    QCOMPARE(shown.session.brushSettings().hardness, 0.25);
    const double diameter = shown.session.brushSettings().diameter;
    QTest::keyClick(shown.canvas, Qt::Key_BracketRight);
    QVERIFY(shown.session.brushSettings().diameter > diameter && shown.session.brushSettings().hardness == 0.25);
}

void BrushKeyTests::bracketKeysReachTheBrushWhereverFocusIsExceptTextFields()
{
    Canvas shown;
    red(shown);
    NativeLayerList list(shown.session, &shown.window);
    list.setGeometry(0, 0, 100, 100);
    auto *field = new QLineEdit(&shown.window);
    field->show();
    list.show();
    // Through the application, as a real key arrives.
    const auto press = [&](QWidget *target, int key, const QString &text, Qt::KeyboardModifiers modifiers) {
        QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
        QApplication::sendEvent(target ? target : static_cast<QWidget *>(&shown.window), &event);
    };
    for (QWidget *focus : {static_cast<QWidget *>(&list), static_cast<QWidget *>(nullptr)}) {
        if (focus)
            focus->setFocus();
        else if (QWidget *focused = QApplication::focusWidget())
            focused->clearFocus();
        BrushSettings settings = shown.session.brushSettings();
        settings.diameter = 20;
        settings.hardness = 0.5;
        shown.session.setBrushSettings(settings);
        press(focus, Qt::Key_BracketRight, QStringLiteral("]"), Qt::NoModifier);
        QVERIFY(shown.session.brushSettings().diameter > 20);
        press(focus, Qt::Key_BracketLeft, QStringLiteral("["), Qt::NoModifier);
        press(focus, Qt::Key_BracketLeft, QStringLiteral("["), Qt::NoModifier);
        QVERIFY(shown.session.brushSettings().diameter < 20);
        press(focus, Qt::Key_BraceRight, QStringLiteral("}"), Qt::ShiftModifier);
        QCOMPARE(shown.session.brushSettings().hardness, 0.75);
        press(focus, Qt::Key_BraceLeft, QStringLiteral("{"), Qt::ShiftModifier);
        press(focus, Qt::Key_BraceLeft, QStringLiteral("{"), Qt::ShiftModifier);
        QCOMPARE(shown.session.brushSettings().hardness, 0.25);
    }
    // Typing in a text field keeps its brackets.
    field->setFocus();
    QTRY_VERIFY(field->hasFocus());
    const double diameter = shown.session.brushSettings().diameter;
    press(field, Qt::Key_BracketRight, QStringLiteral("]"), Qt::NoModifier);
    QCOMPARE(shown.session.brushSettings().diameter, diameter);
    QCOMPARE(field->text(), QString("]"));
    // Other tools leave the brush alone.
    list.setFocus();
    shown.session.selectTool(NavigationTool::move);
    press(&list, Qt::Key_BracketRight, QStringLiteral("]"), Qt::NoModifier);
    QCOMPARE(shown.session.brushSettings().diameter, diameter);
}

void BrushKeyTests::theCanvasTakesItsOwnBracketsAndRereadsAlt()
{
    Canvas shown;
    red(shown);
    // A wide dab gives the Move tool pixels to show.
    BrushSettings settings = shown.session.brushSettings();
    settings.diameter = 100;
    shown.session.setBrushSettings(settings);
    shown.click(QPointF(200, 150));
    settings.diameter = 10;
    shown.session.setBrushSettings(settings);
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    shown.hover(QPointF(200, 150));
    // Alt went down; a drag lost its release.
    QTest::keyPress(shown.canvas, Qt::Key_Alt, Qt::AltModifier);
    QTest::keyClick(shown.canvas, Qt::Key_BracketRight);
    QCOMPARE(shown.session.brushSettings().diameter, 12.0);
    // The bracket reached the canvas, which read Alt as up.
    shown.session.selectTool(NavigationTool::move);
    shown.canvas->synchronizeDisplay();
    QVERIFY(shown.shows(CanvasView::moveCursor(shown.canvas->devicePixelRatio())));
}

QTEST_MAIN(BrushKeyTests)
#include "BrushKeyTests.moc"
