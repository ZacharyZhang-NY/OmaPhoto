#include "ScrubFixtures.h"
#include "UI/BrushControls.h"
#include "UI/GradientControls.h"
#include "UI/LassoControls.h"
#include "UI/ShapeControls.h"
#include "UI/TypeControls.h"
#include <QPushButton>

// Swift's scrubbable labels and units in the tool bars.
class ScrubbableLabelsTests : public QObject {
    Q_OBJECT
private slots:
    void theBrushsLabelsScrubItsTip();
    void theGradientsOpacityScrubs();
    void theSelectionBarsLabelsAndUnitsScrubWholeNumbers();
    void theAmountSheetsTitleScrubsItsInput();
    void theShapesWidthAndRadiusScrub();
    void theTypeBarScrubsWholeSizesTrackingAndLeading();
    void aScrubShowsItsValueInAFocusedField();
    void featherScrubsWithoutASelection();
};

void ScrubbableLabelsTests::theBrushsLabelsScrubItsTip()
{
    EditorSession session;
    session.createDocument(100, 100, true);
    session.selectTool(NavigationTool::brush);
    BrushControls bar(session);
    bar.show();
    drag(scrubbed(bar, "Size"), 25);
    QCOMPARE(session.brushSettings().diameter, 65.0);
    QCOMPARE(find<QLineEdit>(bar, "brushSize").text(), QString("65"));
    drag(scrubbed(bar, "Size"), -5000);
    QCOMPARE(session.brushSettings().diameter, 1.0);
    drag(scrubbed(bar, "Size"), 5000);
    QCOMPARE(session.brushSettings().diameter, 2000.0);
    // Unstepped, a fraction of a point counts.
    drag(scrubbed(bar, "Size"), -1.5);
    QCOMPARE(session.brushSettings().diameter, 1998.5);
    drag(scrubbed(bar, "Hardness"), -30);
    QVERIFY(qAbs(session.brushSettings().hardness - 0.7) < 1e-12);
    drag(scrubbed(bar, "Hardness"), 90);
    QCOMPARE(session.brushSettings().hardness, 1.0);
    drag(scrubbed(bar, "Hardness"), -900);
    QCOMPARE(session.brushSettings().hardness, 0.0);
    drag(scrubbed(bar, "Opacity"), -50);
    QVERIFY(qAbs(session.brushSettings().opacity - 0.5) < 1e-12);
    drag(scrubbed(bar, "Opacity"), -500);
    QCOMPARE(session.brushSettings().opacity, 0.01);
    drag(scrubbed(bar, "Opacity"), 500);
    QCOMPARE(session.brushSettings().opacity, 1.0);
    drag(scrubbed(bar, "Smoothing"), 30);
    QCOMPARE(session.brushSettings().smoothing, 30.0);
    drag(scrubbed(bar, "Smoothing"), 300);
    QCOMPARE(session.brushSettings().smoothing, 100.0);
    drag(scrubbed(bar, "Smoothing"), -300);
    QCOMPARE(session.brushSettings().smoothing, 0.0);
    // The Smear's Strength is the same label, retitled.
    session.selectTool(NavigationTool::blur);
    const double strength = session.brushSettings().opacity;
    drag(scrubbed(bar, "Strength"), -10);
    QVERIFY(qAbs(session.brushSettings().opacity - (strength - 0.1)) < 1e-12);
}

void ScrubbableLabelsTests::theGradientsOpacityScrubs()
{
    EditorSession session;
    session.createDocument(100, 100, true);
    session.selectTool(NavigationTool::gradient);
    GradientControls bar(session);
    bar.show();
    // Unstepped: a point and a half is 1.5%.
    drag(scrubbed(bar, "Opacity"), -1.5);
    QVERIFY(qAbs(session.gradientSettings().opacity - 0.985) < 1e-12);
    drag(scrubbed(bar, "Opacity"), 1.5);
    drag(scrubbed(bar, "Opacity"), -40);
    QVERIFY(qAbs(session.gradientSettings().opacity - 0.6) < 1e-12);
    drag(scrubbed(bar, "Opacity"), -400);
    QCOMPARE(session.gradientSettings().opacity, 0.01);
    drag(scrubbed(bar, "Opacity"), 400);
    QCOMPARE(session.gradientSettings().opacity, 1.0);
}

void ScrubbableLabelsTests::theSelectionBarsLabelsAndUnitsScrubWholeNumbers()
{
    std::unique_ptr<EditorSession> session = selectionSession();
    LassoControls bar(*session);
    bar.resize(1600, bar.height());
    bar.show();
    // Without a selection the amounts rest, units and all.
    QLabel &expand = scrubbed(bar, "px", 0);
    QVERIFY(!expand.isEnabled());
    QVERIFY(!scrubbed(bar, "px", 1).isEnabled());
    drag(scrubbed(bar, "px", 1), 9);
    QCOMPARE(session->selectionContractAmount(), 1);
    lasso(*session, square(10, 10, 20));
    QVERIFY(expand.isEnabled());
    drag(expand, 7);
    QCOMPARE(session->selectionExpandAmount(), 8);
    drag(expand, 900);
    QCOMPARE(session->selectionExpandAmount(), 500);
    drag(expand, -900);
    QCOMPARE(session->selectionExpandAmount(), 1);
    drag(scrubbed(bar, "px", 1), 4);
    QCOMPARE(session->selectionContractAmount(), 5);
    drag(scrubbed(bar, "px", 1), 900);
    QCOMPARE(session->selectionContractAmount(), 500);
    drag(scrubbed(bar, "px", 1), -900);
    QCOMPARE(session->selectionContractAmount(), 1);
    drag(scrubbed(bar, "px", 2), 3);
    QCOMPARE(session->selectionFeatherAmount(), 5);
    drag(scrubbed(bar, "px", 2), 900);
    QCOMPARE(session->selectionFeatherAmount(), 250);
    drag(scrubbed(bar, "px", 2), -900);
    QCOMPARE(session->selectionFeatherAmount(), 1);
    QCOMPARE(find<QLineEdit>(bar, "selectionFeatherAmount").text(), QString("1"));
    session->selectTool(NavigationTool::wand);
    // Whole numbers round, where the setter would truncate.
    drag(scrubbed(bar, "Tolerance"), 1.6);
    QCOMPARE(session->wandSettings().tolerance, 34);
    drag(scrubbed(bar, "Tolerance"), 8);
    QCOMPARE(session->wandSettings().tolerance, 42);
    drag(scrubbed(bar, "Tolerance"), 400);
    QCOMPARE(session->wandSettings().tolerance, 255);
    drag(scrubbed(bar, "Tolerance"), -400);
    QCOMPARE(session->wandSettings().tolerance, 0);
    session->setWandMode(WandMode::object);
    drag(scrubbed(bar, "Edge"), -1.5);
    QCOMPARE(session->objectSelectionSettings().edgeOffset, -2);
    drag(scrubbed(bar, "Edge"), -2);
    QCOMPARE(session->objectSelectionSettings().edgeOffset, -4);
    drag(scrubbed(bar, "Edge"), 50);
    QCOMPARE(session->objectSelectionSettings().edgeOffset, 10);
    drag(scrubbed(bar, "Edge"), -50);
    QCOMPARE(session->objectSelectionSettings().edgeOffset, -10);
}

void ScrubbableLabelsTests::theAmountSheetsTitleScrubsItsInput()
{
    std::unique_ptr<EditorSession> session = selectionSession();
    SelectionAmountSheet sheet(*session, SelectionAmountOperation::feather);
    sheet.show();
    QLineEdit &input = find<QLineEdit>(sheet, "amountField");
    // Rounded to whole pixels, not truncated.
    drag(scrubbed(sheet, "Amount"), 1.6);
    QCOMPARE(input.text(), QString("4"));
    input.setText(QStringLiteral("2"));
    drag(scrubbed(sheet, "Amount"), 10);
    QCOMPARE(input.text(), QString("12"));
    drag(scrubbed(sheet, "Amount"), 900);
    QCOMPARE(input.text(), QString("250"));
    // Unreadable input starts the drag from one.
    input.setText(QStringLiteral("abc"));
    drag(scrubbed(sheet, "Amount"), 4);
    QCOMPARE(input.text(), QString("5"));
    drag(scrubbed(sheet, "Amount"), -40);
    QCOMPARE(input.text(), QString("1"));
    SelectionAmountSheet expand(*session, SelectionAmountOperation::expand);
    drag(scrubbed(expand, "Amount"), 900);
    QCOMPARE(find<QLineEdit>(expand, "amountField").text(), QString("500"));
}

void ScrubbableLabelsTests::theShapesWidthAndRadiusScrub()
{
    EditorSession session;
    session.createDocument(100, 100, true);
    session.selectTool(NavigationTool::shape);
    session.setShapeKind(ShapeKind::line);
    ShapeControls bar(session);
    bar.show();
    drag(scrubbed(bar, "Width"), 6.5);
    QCOMPARE(session.shapeLineWidth(), 10.5);
    drag(scrubbed(bar, "Width"), 9000);
    QCOMPARE(session.shapeLineWidth(), 5000.0);
    drag(scrubbed(bar, "Width"), -9000);
    QCOMPARE(session.shapeLineWidth(), 1.0);
    session.setShapeKind(ShapeKind::rectangle);
    drag(scrubbed(bar, "Radius"), 12.25);
    QCOMPARE(session.shapeCornerRadius(), 12.25);
    drag(scrubbed(bar, "Radius"), 9000);
    QCOMPARE(session.shapeCornerRadius(), 5000.0);
    drag(scrubbed(bar, "Radius"), -9000);
    QCOMPARE(session.shapeCornerRadius(), 0.0);
}

void ScrubbableLabelsTests::theTypeBarScrubsWholeSizesTrackingAndLeading()
{
    EditorSession session;
    session.createDocument(400, 300, true);
    session.selectTool(NavigationTool::type);
    TypeControls bar(session);
    bar.show();
    session.changeTextStyle([](LayerTextStyle &style) { style.fontSize = 24.4; });
    // Dragged sizes land on whole numbers, Swift's step of one.
    drag(scrubbed(bar, "px"), 3);
    QCOMPARE(session.currentTextStyle().fontSize, 27.0);
    drag(scrubbed(bar, "px"), 5000);
    QCOMPARE(session.currentTextStyle().fontSize, 2000.0);
    drag(scrubbed(bar, "px"), -5000);
    QCOMPARE(session.currentTextStyle().fontSize, 1.0);
    session.changeTextStyle([](LayerTextStyle &style) { style.tracking = 2.6; });
    drag(scrubbed(bar, "Tracking"), -5);
    QCOMPARE(session.currentTextStyle().tracking, -2.0);
    drag(scrubbed(bar, "Tracking"), -500);
    QCOMPARE(session.currentTextStyle().tracking, -100.0);
    drag(scrubbed(bar, "Tracking"), 5000);
    QCOMPARE(session.currentTextStyle().tracking, 1000.0);
    // Auto is zero, so a drag starts there.
    drag(scrubbed(bar, "Leading"), 12);
    QCOMPARE(session.currentTextStyle().leading, 12.0);
    drag(scrubbed(bar, "Leading"), -50);
    QCOMPARE(session.currentTextStyle().leading, 0.0);
    drag(scrubbed(bar, "Leading"), 9000);
    QCOMPARE(session.currentTextStyle().leading, 5000.0);
}

void ScrubbableLabelsTests::aScrubShowsItsValueInAFocusedField()
{
    EditorSession session;
    session.createDocument(100, 100, true);
    session.selectTool(NavigationTool::brush);
    {
        BrushControls bar(session);
        showActive(bar);
        QCOMPARE(scrubOverTyping(find<QLineEdit>(bar, "brushSize"), scrubbed(bar, "Size"), 25), QString("32"));
        QCOMPARE(scrubOverTyping(find<QLineEdit>(bar, "brushHardness"), scrubbed(bar, "Hardness"), 30), QString("37"));
        QCOMPARE(scrubOverTyping(find<QLineEdit>(bar, "brushOpacity"), scrubbed(bar, "Opacity"), 40), QString("47"));
        QCOMPARE(scrubOverTyping(find<QLineEdit>(bar, "brushSmoothing"), scrubbed(bar, "Smoothing"), 12), QString("19"));
    }
    session.selectTool(NavigationTool::gradient);
    {
        GradientControls bar(session);
        showActive(bar);
        QCOMPARE(scrubOverTyping(find<QLineEdit>(bar, "gradientOpacity"), scrubbed(bar, "Opacity"), 25), QString("32"));
    }
    session.selectTool(NavigationTool::shape);
    session.setShapeKind(ShapeKind::line);
    {
        ShapeControls bar(session);
        showActive(bar);
        QCOMPARE(scrubOverTyping(find<QLineEdit>(bar, "shapeWidth"), scrubbed(bar, "Width"), 6), QString("13"));
    }
    session.selectTool(NavigationTool::wand);
    {
        LassoControls bar(session);
        bar.resize(1600, bar.height());
        showActive(bar);
        QCOMPARE(scrubOverTyping(find<QLineEdit>(bar, "wandTolerance"), scrubbed(bar, "Tolerance"), 10), QString("17"));
        QCOMPARE(scrubOverTyping(find<QLineEdit>(bar, "selectionFeatherAmount"), scrubbed(bar, "px", 2), 3), QString("10"));
    }
    session.selectTool(NavigationTool::type);
    {
        TypeControls bar(session);
        showActive(bar);
        QCOMPARE(scrubOverTyping(find<QLineEdit>(bar, "typeTracking"), scrubbed(bar, "Tracking"), 4), QString("11"));
        QCOMPARE(scrubOverTyping(find<QLineEdit>(bar, "typeLeading"), scrubbed(bar, "Leading"), 30), QString("37"));
        QCOMPARE(scrubOverTyping(find<QLineEdit>(bar, "typeSize"), scrubbed(bar, "px"), -2), QString("5"));
    }
}

void ScrubbableLabelsTests::featherScrubsWithoutASelection()
{
    std::unique_ptr<EditorSession> session = selectionSession();
    LassoControls bar(*session);
    bar.resize(1600, bar.height());
    bar.show();
    // Swift rests Feather's button alone; its amount stays.
    drag(scrubbed(bar, "px", 2), 7);
    QCOMPARE(session->selectionFeatherAmount(), 9);
    QVERIFY(find<QLineEdit>(bar, "selectionFeatherAmount").isEnabled());
    QVERIFY(!find<QLineEdit>(bar, "selectionExpandAmount").isEnabled());
    QVERIFY(!find<QLineEdit>(bar, "selectionContractAmount").isEnabled());
    for (QPushButton *button : bar.findChildren<QPushButton *>())
        if (button->text() == "Expand" || button->text() == "Contract" || button->text() == "Feather")
            QVERIFY(!button->isEnabled());
    drag(scrubbed(bar, "px", 0), 7);
    QCOMPARE(session->selectionExpandAmount(), 1);
}

QTEST_MAIN(ScrubbableLabelsTests)
#include "ScrubbableLabelsTests.moc"
