#include "SelectionFixtures.h"
#include "UI/LassoControls.h"
#include <QtTest>

// The selection tools' bar: kinds, mode, edges, amounts, deselect.
namespace {
template <typename Widget> Widget &find(QWidget &root, const char *name)
{
    Widget *found = root.findChild<Widget *>(QString::fromLatin1(name));
    if (!found)
        throw std::runtime_error(std::string("no widget named ") + name);
    return *found;
}

struct Bar {
    std::unique_ptr<EditorSession> session = selectionSession(100, 100);
    LassoControls controls{*session};
    Bar()
    {
        controls.show();
        if (!QTest::qWaitForWindowExposed(&controls))
            throw std::runtime_error("the bar never showed");
    }
    QToolButton &button(const char *name) { return find<QToolButton>(controls, name); }
};
}

class LassoControlsTests : public QObject {
    Q_OBJECT
private slots:
    void theBarShowsTheToolsKindsAndSwitchesThem();
    void theModeFollowsHeldKeysAndAnOutlineAndClicksSetTheChoice();
    void expandContractAndTheirAmountsNeedASelection();
    void deselectTheEmptyWordsAndAntialiasFollowTheSession();
    void theWandsControlsWriteItsSettings();
};

void LassoControlsTests::theBarShowsTheToolsKindsAndSwitchesThem()
{
    Bar bar;
    EditorSession &session = *bar.session;
    QCOMPARE(bar.controls.title->text(), QString("Lasso"));
    QVERIFY(bar.button("lassoFreehand").isVisible() && bar.button("lassoPolygonal").isVisible());
    QVERIFY(!bar.button("marqueeRectangle").isVisible() && !bar.button("marqueeEllipse").isVisible());
    QVERIFY(bar.button("lassoFreehand").isChecked() && !bar.button("lassoPolygonal").isChecked());
    QVERIFY(find<QCheckBox>(bar.controls, "selectionAntialiased").isVisible());
    // A choice ends an outline in progress.
    session.beginLasso(QPointF(1, 1), SelectionMode::replace);
    QTest::mouseClick(&bar.button("lassoPolygonal"), Qt::LeftButton);
    QVERIFY(session.lassoKind() == LassoKind::polygonal && !session.lassoDraft().has_value());
    QVERIFY(bar.button("lassoPolygonal").isChecked());
    session.selectTool(NavigationTool::marquee);
    QCOMPARE(bar.controls.title->text(), QString("Marquee"));
    QVERIFY(bar.button("marqueeRectangle").isVisible() && bar.button("marqueeRectangle").isChecked());
    QVERIFY(!bar.button("lassoFreehand").isVisible());
    // Rectangles snap to whole pixels: no smoothing to offer.
    QVERIFY(!find<QCheckBox>(bar.controls, "selectionAntialiased").isVisible());
    QTest::mouseClick(&bar.button("marqueeEllipse"), Qt::LeftButton);
    QVERIFY(session.marqueeKind() == LassoKind::ellipse);
    session.setMarqueeKind(LassoKind::rectangle);
    QVERIFY(bar.button("marqueeRectangle").isChecked() && !bar.button("marqueeEllipse").isChecked());
    session.setMarqueeKind(LassoKind::ellipse);
    QVERIFY(bar.button("marqueeEllipse").isChecked() && !bar.button("marqueeRectangle").isChecked());
    QVERIFY(find<QCheckBox>(bar.controls, "selectionAntialiased").isVisible());
    session.selectTool(NavigationTool::wand);
    QCOMPARE(bar.controls.title->text(), QString("Magic Wand"));
    QVERIFY(!bar.button("marqueeRectangle").isVisible() && !bar.button("lassoFreehand").isVisible());
    QVERIFY(find<QCheckBox>(bar.controls, "selectionAntialiased").isVisible());
    QCOMPARE(bar.controls.height(), 42);
}

void LassoControlsTests::theModeFollowsHeldKeysAndAnOutlineAndClicksSetTheChoice()
{
    Bar bar;
    EditorSession &session = *bar.session;
    QVERIFY(bar.button("selectionModeNew").isChecked());
    session.updateHeldSelectionKeys(true, false);
    QVERIFY(bar.button("selectionModeAdd").isChecked() && !bar.button("selectionModeNew").isChecked());
    session.updateHeldSelectionKeys(false, true);
    QVERIFY(bar.button("selectionModeSubtract").isChecked());
    session.updateHeldSelectionKeys(false, false);
    QVERIFY(bar.button("selectionModeNew").isChecked());
    // An outline keeps its starting mode while it lasts.
    session.beginLasso(QPointF(1, 1), SelectionMode::subtract);
    session.updateHeldSelectionKeys(true, false);
    QVERIFY(bar.button("selectionModeSubtract").isChecked());
    session.cancelLasso();
    session.updateHeldSelectionKeys(false, false);
    QTest::mouseClick(&bar.button("selectionModeAdd"), Qt::LeftButton);
    QVERIFY(session.selectionModeChoice() == SelectionMode::add && bar.button("selectionModeAdd").isChecked());
    QTest::mouseClick(&bar.button("selectionModeNew"), Qt::LeftButton);
    QVERIFY(session.selectionModeChoice() == SelectionMode::replace);
}

void LassoControlsTests::expandContractAndTheirAmountsNeedASelection()
{
    Bar bar;
    EditorSession &session = *bar.session;
    auto &expand = find<QPushButton>(bar.controls, "expandSelection");
    auto &contract = find<QPushButton>(bar.controls, "contractSelection");
    auto &amount = find<QLineEdit>(bar.controls, "selectionExpandAmount");
    auto &shrink = find<QLineEdit>(bar.controls, "selectionContractAmount");
    QVERIFY(!expand.isEnabled() && !contract.isEnabled() && !amount.isEnabled() && !shrink.isEnabled());
    QCOMPARE(amount.text(), QString("1"));
    session.applySelection(rectPath(QRectF(40, 40, 20, 20)), SelectionMode::replace, "Select");
    QVERIFY(expand.isEnabled() && contract.isEnabled() && amount.isEnabled() && shrink.isEnabled());
    // Typed amounts apply at once, 1 to 500; arrows step.
    amount.setFocus();
    QTRY_VERIFY(amount.hasFocus());
    amount.selectAll();
    QTest::keyClicks(&amount, "5");
    QCOMPARE(session.selectionExpandAmount(), 5);
    QTest::keyClick(&amount, Qt::Key_Up);
    QCOMPARE(session.selectionExpandAmount(), 6);
    QCOMPARE(amount.text(), QString("6"));
    QTest::keyClick(&amount, Qt::Key_Up, Qt::ShiftModifier);
    QCOMPARE(session.selectionExpandAmount(), 16);
    amount.selectAll();
    QTest::keyClicks(&amount, "600");
    QCOMPARE(session.selectionExpandAmount(), 500);
    QTest::keyClicks(&amount, "a");
    QCOMPARE(amount.text(), QString("600"));
    // Every digit is taken; the bounds clamp what they name.
    amount.selectAll();
    QTest::keyClicks(&amount, "1000");
    QCOMPARE(amount.text(), QString("1000"));
    QCOMPARE(session.selectionExpandAmount(), 500);
    QTest::keyClick(&amount, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(session.selectionExpandAmount(), 490);
    QCOMPARE(amount.text(), QString("490"));
    // The menu bar borrows the focus: the typing stays.
    amount.selectAll();
    QTest::keyClicks(&amount, "12");
    QVERIFY(amount.isUndoAvailable());
    contract.setFocus(Qt::MenuBarFocusReason);
    QVERIFY(!amount.hasFocus());
    QCOMPARE(amount.text(), QString("12"));
    QVERIFY(amount.isUndoAvailable());
    session.setSelectionExpandAmount(7);
    QCOMPARE(amount.text(), QString("12"));
    amount.setFocus();
    QTRY_VERIFY(amount.hasFocus());
    QTest::keyClicks(&amount, "3");
    QCOMPARE(session.selectionExpandAmount(), 123);
    contract.setFocus(Qt::OtherFocusReason);
    QCOMPARE(amount.text(), QString("123"));
    // A popup borrows it the same way.
    amount.setFocus();
    QTRY_VERIFY(amount.hasFocus());
    amount.selectAll();
    QTest::keyClicks(&amount, "45");
    contract.setFocus(Qt::PopupFocusReason);
    session.setSelectionExpandAmount(9);
    QVERIFY(amount.isUndoAvailable());
    QCOMPARE(amount.text(), QString("45"));
    amount.setFocus();
    QTRY_VERIFY(amount.hasFocus());
    // Return shows the held amount and hands over the keys.
    amount.selectAll();
    QTest::keyClicks(&amount, "0");
    QCOMPARE(session.selectionExpandAmount(), 1);
    const int focus = session.canvasFocusRequest();
    QTest::keyClick(&amount, Qt::Key_Return);
    QVERIFY(!amount.hasFocus());
    QCOMPARE(session.canvasFocusRequest(), focus + 1);
    QCOMPARE(amount.text(), QString("1"));
    session.setSelectionExpandAmount(3);
    QCOMPARE(amount.text(), QString("3"));
    QTest::mouseClick(&expand, Qt::LeftButton);
    QCOMPARE(session.history.undoName(), QString("Expand Selection"));
    QCOMPARE(bounds(session), QRectF(37, 37, 26, 26));
    shrink.setFocus();
    QTRY_VERIFY(shrink.hasFocus());
    QTest::keyClick(&shrink, Qt::Key_Down);
    QCOMPARE(session.selectionContractAmount(), 1);
    shrink.selectAll();
    QTest::keyClicks(&shrink, "2");
    QTest::keyClick(&shrink, Qt::Key_Escape);
    QTest::mouseClick(&contract, Qt::LeftButton);
    QCOMPARE(session.history.undoName(), QString("Contract Selection"));
    QCOMPARE(bounds(session), QRectF(39, 39, 22, 22));
    // A draft in progress rests the edges.
    session.beginLasso(QPointF(1, 1), SelectionMode::replace);
    QVERIFY(!expand.isEnabled() && !contract.isEnabled());
}

void LassoControlsTests::deselectTheEmptyWordsAndAntialiasFollowTheSession()
{
    Bar bar;
    EditorSession &session = *bar.session;
    auto &deselect = find<QPushButton>(bar.controls, "deselect");
    auto &empty = find<QLabel>(bar.controls, "emptySelection");
    auto &antialias = find<QCheckBox>(bar.controls, "selectionAntialiased");
    QVERIFY(!deselect.isVisible() && !empty.isVisible());
    QVERIFY(antialias.isChecked());
    QTest::mouseClick(&antialias, Qt::LeftButton);
    QVERIFY(!session.selectionAntialiased() && !antialias.isChecked());
    session.setSelectionAntialiased(true);
    QVERIFY(antialias.isChecked());
    session.selectAll();
    QVERIFY(deselect.isVisible() && deselect.isEnabled() && !empty.isVisible());
    session.applySelection(rectPath(QRectF(0, 0, 100, 100)), SelectionMode::subtract, "Subtract");
    QVERIFY(empty.isVisible() && deselect.isVisible());
    QTest::mouseClick(&deselect, Qt::LeftButton);
    QVERIFY(!session.selection().has_value() && !deselect.isVisible());
    // Busy, or without a canvas, the whole bar waits.
    session.selectAll();
    session.setIsProjectBusy(true);
    QVERIFY(deselect.isVisible() && !deselect.isEnabled());
    QTRY_VERIFY(!bar.controls.isEnabled());
    session.setIsProjectBusy(false);
    QVERIFY(bar.controls.isEnabled());
    session.clearProject();
    QVERIFY(!bar.controls.isEnabled());
}

void LassoControlsTests::theWandsControlsWriteItsSettings()
{
    Bar bar;
    EditorSession &session = *bar.session;
    auto &tolerance = find<QLineEdit>(bar.controls, "wandTolerance");
    auto &size = find<QComboBox>(bar.controls, "wandSampleSize");
    auto &contiguous = find<QCheckBox>(bar.controls, "wandContiguous");
    // The wand's own controls show with the wand alone.
    QVERIFY(!tolerance.isVisible() && !size.isVisible() && !contiguous.isVisible());
    session.selectTool(NavigationTool::wand);
    QVERIFY(tolerance.isVisible() && size.isVisible() && contiguous.isVisible());
    QVERIFY(bar.button("wandThisLayer").isChecked() && !bar.button("wandAllLayers").isChecked());
    QCOMPARE(tolerance.text(), QString("32"));
    QCOMPARE(size.currentText(), QString("Point Sample"));
    QCOMPARE(size.count(), 3);
    QVERIFY(contiguous.isChecked());
    // Tolerance runs 0 to 255, typed or stepped.
    tolerance.setFocus();
    QTRY_VERIFY(tolerance.hasFocus());
    tolerance.selectAll();
    QTest::keyClicks(&tolerance, "300");
    QCOMPARE(session.wandSettings().tolerance, 255);
    // A pasted million or too many digits: the top.
    tolerance.selectAll();
    tolerance.insert(QStringLiteral("1000000"));
    QCOMPARE(tolerance.text(), QString("1000000"));
    QCOMPARE(session.wandSettings().tolerance, 255);
    WandSettings low = session.wandSettings();
    low.tolerance = 7;
    session.setWandSettings(low);
    QCOMPARE(session.wandSettings().tolerance, 7);
    tolerance.selectAll();
    tolerance.insert(QStringLiteral("99999999999999999999"));
    QCOMPARE(session.wandSettings().tolerance, 255);
    QTest::keyClick(&tolerance, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(session.wandSettings().tolerance, 245);
    tolerance.selectAll();
    QTest::keyClicks(&tolerance, "0");
    QTest::keyClick(&tolerance, Qt::Key_Down);
    QCOMPARE(session.wandSettings().tolerance, 0);
    QTest::keyClick(&tolerance, Qt::Key_Return);
    // The other three write their fields and read them back.
    size.setCurrentIndex(2);
    emit size.activated(2);
    QVERIFY(session.wandSettings().sampleSize == WandSampleSize::fiveByFive);
    QTest::mouseClick(&bar.button("wandAllLayers"), Qt::LeftButton);
    QVERIFY(session.wandSettings().sampleAllLayers);
    QTest::mouseClick(&contiguous, Qt::LeftButton);
    QVERIFY(!session.wandSettings().contiguous);
    QCOMPARE(session.wandSettings().tolerance, 0);
    WandSettings back;
    session.setWandSettings(back);
    QCOMPARE(tolerance.text(), QString("32"));
    QCOMPARE(size.currentText(), QString("Point Sample"));
    QVERIFY(bar.button("wandThisLayer").isChecked() && contiguous.isChecked());
}

QTEST_MAIN(LassoControlsTests)
#include "LassoControlsTests.moc"
