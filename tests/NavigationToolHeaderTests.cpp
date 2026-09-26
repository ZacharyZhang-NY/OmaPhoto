#include "UI/NavigationToolHeader.h"
#include <QtTest>

// The Hand and Zoom tools' bar, and Zoom's percentage field.
namespace {
struct Bar {
    EditorSession session;
    std::unique_ptr<NavigationToolHeader> header;
    QLineEdit *field = nullptr;

    explicit Bar(bool withCanvas = true)
    {
        if (withCanvas)
            session.createDocument(800, 600);
        session.selectTool(NavigationTool::zoom);
        header = std::make_unique<NavigationToolHeader>(session);
        field = header->findChild<QLineEdit *>("zoomPercentage");
        header->show();
        // Focus is real only in an active window.
        header->activateWindow();
        active = QTest::qWaitForWindowActive(header.get());
    }
    bool active = false;

    void type(const QString &text)
    {
        field->setFocus();
        field->selectAll();
        QTest::keyClicks(field, text);
    }
};
}

class NavigationToolHeaderTests : public QObject {
    Q_OBJECT
private slots:
    void theTitleAndTheFieldFollowTheTool();
    void theFieldShowsTheZoomWithoutIdleZeros();
    void returnEscapeAndLeavingAllApplyWhatWasTyped();
    void whatIsNoZoomIsPutBack();
    void arrowsStepOnePercentAndShiftTen();
    void theFieldNeedsACanvasAndAnIdleSession();
    void anOutsideZoomLeavesTypingAlone();
};

void NavigationToolHeaderTests::theTitleAndTheFieldFollowTheTool()
{
    Bar bar;
    QVERIFY(bar.active);
    QCOMPARE(bar.header->title->text(), QString("Zoom"));
    QVERIFY(bar.field->isVisible());
    QCOMPARE(bar.field->width(), 72);
    QCOMPARE(bar.field->alignment(), Qt::AlignRight | Qt::AlignVCenter);
    QCOMPARE(bar.field->accessibleName(), QString("Zoom percentage"));
    QCOMPARE(bar.field->toolTip(), QString("Zoom percentage (0.1–3200%). Press Return to apply."));
    QCOMPARE(bar.header->height(), 42);
    // The unit sits right of its field, two pixels away.
    const QList<QLabel *> labels = bar.header->findChildren<QLabel *>();
    const auto unit = std::find_if(labels.begin(), labels.end(), [](const QLabel *label) { return label->text() == "%"; });
    QVERIFY(unit != labels.end());
    QCoreApplication::processEvents();
    QCOMPARE((*unit)->geometry().left() - bar.field->geometry().right() - 1, 2);
    bar.session.selectTool(NavigationTool::hand);
    QCOMPARE(bar.header->title->text(), QString("Pan"));
    QVERIFY(!bar.field->isVisible() && !(*unit)->isVisible());
    bar.session.selectTool(NavigationTool::zoom);
    QVERIFY(bar.field->isVisible() && (*unit)->isVisible());
}

void NavigationToolHeaderTests::theFieldShowsTheZoomWithoutIdleZeros()
{
    Bar bar;
    // A field in use is left alone; not this one.
    bar.field->clearFocus();
    // A bar made over a zoomed session shows that zoom.
    bar.session.zoom(0.5);
    NavigationToolHeader late(bar.session);
    QCOMPARE(late.findChild<QLineEdit *>("zoomPercentage")->text(), QString("50"));
    const auto shown = [&](double zoom) {
        bar.session.zoom(zoom);
        return bar.field->text();
    };
    QCOMPARE(shown(1), QString("100"));
    QCOMPARE(shown(0.125), QString("12.5"));
    QCOMPARE(shown(1.0 / 3), QString("33.33"));
    QCOMPARE(shown(2.5), QString("250"));
    QCOMPARE(shown(0.001), QString("0.1"));
    QCOMPARE(shown(32), QString("3200"));
    QCOMPARE(shown(0.1), QString("10"));
}

void NavigationToolHeaderTests::returnEscapeAndLeavingAllApplyWhatWasTyped()
{
    Bar bar;
    bar.type("250");
    QTest::keyClick(bar.field, Qt::Key_Return);
    QCOMPARE(bar.session.viewport.zoom(), 2.5);
    QVERIFY(!bar.field->hasFocus());
    QCOMPARE(bar.session.canvasFocusRequest(), 1);
    // Applied once: Return asks for the canvas, zooms no more.
    QSignalSpy changes(&bar.session, &EditorSession::changed);
    bar.field->setFocus();
    QTest::keyClick(bar.field, Qt::Key_Return);
    QCOMPARE(changes.count(), 1);
    QCOMPARE(bar.session.viewport.zoom(), 2.5);
    QCOMPARE(bar.session.canvasFocusRequest(), 2);
    bar.type(" 50% ");
    QTest::keyClick(bar.field, Qt::Key_Escape);
    QCOMPARE(bar.session.viewport.zoom(), 0.5);
    QCOMPARE(bar.field->text(), QString("50"));
    QVERIFY(!bar.field->hasFocus());
    QCOMPARE(bar.session.canvasFocusRequest(), 3);
    bar.type("75");
    QTest::keyClick(bar.field, Qt::Key_Enter);
    QCOMPARE(bar.session.viewport.zoom(), 0.75);
    QCOMPARE(bar.session.canvasFocusRequest(), 4);
    // Leaving the field applies as well, asking for no focus.
    bar.type("125");
    bar.field->clearFocus();
    QCOMPARE(bar.session.viewport.zoom(), 1.25);
    QCOMPARE(bar.session.canvasFocusRequest(), 4);
    // Past the range the viewport decides; the field follows.
    bar.type("99999");
    QTest::keyClick(bar.field, Qt::Key_Return);
    QCOMPARE(bar.session.viewport.zoom(), 32.0);
    QCOMPARE(bar.field->text(), QString("3200"));
    // Behind another window nothing loses focus: Return applies.
    Bar behind;
    QWidget front;
    front.show();
    front.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&front));
    behind.field->setFocus();
    behind.field->selectAll();
    QTest::keyClicks(behind.field, "42");
    QTest::keyClick(behind.field, Qt::Key_Return);
    QCOMPARE(behind.session.viewport.zoom(), 0.42);
}

void NavigationToolHeaderTests::whatIsNoZoomIsPutBack()
{
    Bar bar;
    bar.session.zoom(2);
    QSignalSpy changes(&bar.session, &EditorSession::changed);
    for (const QString &bad : {QString("wide"), QString(""), QString("0"), QString("-40"), QString("nan"), QString("inf"), QString("1e999")}) {
        bar.type(bad);
        QTest::keyClick(bar.field, Qt::Key_Return);
        QCOMPARE(bar.session.viewport.zoom(), 2.0);
        QCOMPARE(bar.field->text(), QString("200"));
    }
    // Each Return asks for the canvas; none zooms.
    QCOMPARE(changes.count(), 7);
    QCOMPARE(bar.session.canvasFocusRequest(), 7);
    // A busy project takes no zoom from the field.
    bar.session.setIsProjectBusy(true);
    bar.type("300");
    QTest::keyClick(bar.field, Qt::Key_Return);
    QCOMPARE(bar.session.viewport.zoom(), 2.0);
    QCOMPARE(bar.field->text(), QString("200"));
    // Untouched text is not applied: no rounding creeps in.
    bar.session.setIsProjectBusy(false);
    bar.session.zoom(1.0 / 3);
    const double third = bar.session.viewport.zoom();
    bar.field->setFocus();
    QTest::keyClick(bar.field, Qt::Key_Return);
    QCOMPARE(bar.session.viewport.zoom(), third);
}

void NavigationToolHeaderTests::arrowsStepOnePercentAndShiftTen()
{
    Bar bar;
    bar.session.zoom(1);
    bar.field->setFocus();
    QTest::keyClick(bar.field, Qt::Key_Up);
    QCOMPARE(bar.session.viewport.zoom(), 1.01);
    QCOMPARE(bar.field->text(), QString("101"));
    QVERIFY(bar.field->hasFocus());
    QTest::keyClick(bar.field, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(bar.session.viewport.zoom(), 0.91);
    QTest::keyClick(bar.field, Qt::Key_Up, Qt::ShiftModifier);
    QTest::keyClick(bar.field, Qt::Key_Down);
    QCOMPARE(bar.field->text(), QString("100"));
    // A step starts from what is typed, percent sign included.
    bar.type("40%");
    QTest::keyClick(bar.field, Qt::Key_Up);
    QCOMPARE(bar.session.viewport.zoom(), 0.41);
    // The ends of the range hold.
    bar.type("3195");
    QTest::keyClick(bar.field, Qt::Key_Up, Qt::ShiftModifier);
    QCOMPARE(bar.field->text(), QString("3200"));
    bar.type("0.5");
    QTest::keyClick(bar.field, Qt::Key_Down);
    QCOMPARE(bar.field->text(), QString("0.1"));
    // Text that is no number steps from the current zoom.
    bar.session.zoom(2);
    bar.type("wide");
    QTest::keyClick(bar.field, Qt::Key_Up);
    QCOMPARE(bar.field->text(), QString("201"));
}

void NavigationToolHeaderTests::theFieldNeedsACanvasAndAnIdleSession()
{
    Bar bare(false);
    QVERIFY(!bare.field->isEnabled());
    bare.session.createDocument(8, 8);
    QVERIFY(bare.field->isEnabled());
    // Busy dims only once it shows, a quarter second in.
    bare.session.setIsProjectBusy(true);
    QVERIFY(bare.field->isEnabled());
    QTRY_VERIFY(!bare.field->isEnabled());
    bare.session.setIsProjectBusy(false);
    QVERIFY(bare.field->isEnabled());
}

void NavigationToolHeaderTests::anOutsideZoomLeavesTypingAlone()
{
    Bar bar;
    bar.type("77");
    // A zoom from elsewhere arrives while the user types.
    bar.session.zoom(3);
    QVERIFY(bar.field->hasFocus());
    QCOMPARE(bar.field->text(), QString("77"));
    QTest::keyClick(bar.field, Qt::Key_Return);
    QCOMPARE(bar.session.viewport.zoom(), 0.77);
    bar.session.zoom(3);
    QCOMPARE(bar.field->text(), QString("300"));
}

QTEST_MAIN(NavigationToolHeaderTests)
#include "NavigationToolHeaderTests.moc"
