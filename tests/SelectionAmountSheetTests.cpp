#include "ContentView.h"
#include "SelectionFixtures.h"
#include "UI/LassoControls.h"
#include <QApplication>
#include <QLabel>
#include <QSlider>

// Select's amount sheet, its panel, and the bar's Feather.
namespace {
template <typename Widget> Widget &find(QWidget &root, const char *name)
{
    Widget *found = root.findChild<Widget *>(QString::fromLatin1(name));
    if (!found)
        throw std::runtime_error(std::string("no widget named ") + name);
    return *found;
}

QWidget *amountPanel()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == QStringLiteral("selectionAmountPanel") && widget->isVisible())
            return widget;
    }
    return nullptr;
}
}

class SelectionAmountSheetTests : public QObject {
    Q_OBJECT
private slots:
    void theSheetTakesAWholeNumberInRange();
    void cancelAndThePanelsCloseForgetTheQuestion();
    void theBarFeathersByItsAmount();
};

void SelectionAmountSheetTests::theSheetTakesAWholeNumberInRange()
{
    const auto session = selectionSession();
    lasso(*session, square(20, 20, 40));
    session->setSelectionFeatherAmount(9);
    session->promptSelectionAmount(SelectionAmountOperation::feather);
    SelectionAmountSheet sheet(*session, SelectionAmountOperation::feather);
    sheet.show();
    QVERIFY(QTest::qWaitForWindowExposed(&sheet));
    QCOMPARE(sheet.width(), 380);
    auto &field = find<QLineEdit>(sheet, "amountField");
    auto &slider = find<QSlider>(sheet, "amountSlider");
    auto &note = find<QLabel>(sheet, "amountNote");
    auto &ok = find<QPushButton>(sheet, "amountOK");
    QTRY_VERIFY(field.hasFocus());
    QCOMPARE(field.text(), QString("9"));
    QCOMPARE(slider.maximum(), 250);
    QCOMPARE(slider.value(), 9);
    QVERIFY(!note.isVisible() && ok.isEnabled() && ok.isDefault());
    QCOMPARE(note.text(), QString("Enter a whole number from 1 to 250 px."));
    const int height = sheet.height();
    // Out of range: the note shows, OK rests.
    for (const char *typed : {"251", "0", "2.5", "x", ""}) {
        field.setText(QString::fromLatin1(typed));
        QVERIFY2(note.isVisible() && !ok.isEnabled(), typed);
        QCOMPARE(sheet.height(), height);
    }
    slider.setValue(40);
    QCOMPARE(field.text(), QString("40"));
    QVERIFY(ok.isEnabled());
    field.setText(QStringLiteral(" 12 "));
    QCOMPARE(slider.value(), 12);
    QCOMPARE(note.font().pixelSize(), 12);
    QTest::mouseClick(&ok, Qt::LeftButton);
    QVERIFY(!session->selectionAmountOperation());
    QCOMPARE(session->selectionFeatherAmount(), 12);
    QCOMPARE(session->selection().value().feather, 12.0);
    // Expand and Contract reach 500.
    SelectionAmountSheet expand(*session, SelectionAmountOperation::expand);
    QCOMPARE(find<QSlider>(expand, "amountSlider").maximum(), 500);
    QCOMPARE(find<QLineEdit>(expand, "amountField").text(), QString("1"));
    QCOMPARE(find<QLabel>(expand, "amountNote").text(), QString("Enter a whole number from 1 to 500 px."));
}

void SelectionAmountSheetTests::cancelAndThePanelsCloseForgetTheQuestion()
{
    EditorSession session;
    ContentView view(session);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    session.createDocument(100, 100, true);
    session.selectAll();
    session.promptSelectionAmount(SelectionAmountOperation::contract);
    QWidget *panel = nullptr;
    QTRY_VERIFY((panel = amountPanel()));
    QCOMPARE(panel->windowTitle(), QString("Contract Selection"));
    QTest::mouseClick(&find<QPushButton>(*panel, "amountCancel"), Qt::LeftButton);
    QVERIFY(!session.selectionAmountOperation());
    QTRY_VERIFY(!amountPanel());
    QCOMPARE(session.history.undoName(), QString("Select All"));
    // Escape closes the panel, forgetting the question too.
    session.promptSelectionAmount(SelectionAmountOperation::expand);
    QTRY_VERIFY((panel = amountPanel()));
    QCOMPARE(panel->windowTitle(), QString("Expand Selection"));
    QTRY_COMPARE(QApplication::activeWindow(), panel);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    QVERIFY(!session.selectionAmountOperation());
    QTRY_VERIFY(!amountPanel());
    // Return in the field is OK: it applies and closes.
    session.promptSelectionAmount(SelectionAmountOperation::feather);
    QTRY_VERIFY((panel = amountPanel()));
    QCOMPARE(panel->windowTitle(), QString("Feather Selection"));
    QTRY_COMPARE(QApplication::focusWidget(), &find<QLineEdit>(*panel, "amountField"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
    QCOMPARE(session.history.undoName(), QString("Feather Selection"));
    QTRY_VERIFY(!amountPanel());
}

void SelectionAmountSheetTests::theBarFeathersByItsAmount()
{
    const auto session = selectionSession();
    LassoControls controls(*session);
    controls.show();
    QVERIFY(QTest::qWaitForWindowExposed(&controls));
    auto &feather = find<QPushButton>(controls, "featherSelection");
    auto &amount = find<QLineEdit>(controls, "selectionFeatherAmount");
    // Swift rests the button alone; the amount stays editable.
    QVERIFY(!feather.isEnabled() && amount.isEnabled());
    QCOMPARE(amount.text(), QString("2"));
    lasso(*session, square(20, 20, 40));
    QVERIFY(feather.isEnabled() && amount.isEnabled());
    amount.setFocus();
    QTRY_VERIFY(amount.hasFocus());
    amount.selectAll();
    QTest::keyClicks(&amount, "300");
    QCOMPARE(session->selectionFeatherAmount(), 250);
    amount.selectAll();
    QTest::keyClicks(&amount, "3");
    QTest::mouseClick(&feather, Qt::LeftButton);
    QCOMPARE(session->selection().value().feather, 3.0);
    QCOMPARE(session->history.undoName(), QString("Feather Selection"));
}

QTEST_MAIN(SelectionAmountSheetTests)
#include "SelectionAmountSheetTests.moc"
