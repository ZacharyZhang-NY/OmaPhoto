#include "UI/NumericScrub.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QtTest>

// Swift's NumericScrub: a label dragged sideways sets its value.
namespace {
// Counts the moves that pass the scrub, which runs first.
struct MoveCounter : QObject {
    int moves = 0;
    bool eventFilter(QObject *, QEvent *event) override
    {
        moves += event->type() == QEvent::MouseMove;
        return false;
    }
};

struct Probe {
    QWidget window;
    QLabel *label = new QLabel(QStringLiteral("Size"), &window);
    QLineEdit *other = new QLineEdit(&window);
    double value = 10;
    QStringList log;
    MoveCounter counter;
    explicit Probe(double sensitivity = 1, double low = 0, double high = 100, std::optional<double> step = std::nullopt)
    {
        auto *row = new QHBoxLayout(&window);
        row->addWidget(label);
        row->addWidget(other);
        window.resize(300, 40);
        label->installEventFilter(&counter);
        new NumericScrub(label, {.sensitivity = sensitivity, .low = low, .high = high, .step = step, .value = [this] { return value; },
                                 .set = [this](double set) { value = set; log << QString::number(set); },
                                 .onStart = [this] { log << "began"; }, .onEnd = [this] { log << "ended"; }});
        window.show();
        if (!QTest::qWaitForWindowExposed(&window))
            throw std::runtime_error("the probe never showed");
    }
};

// A mouse event at a widget's point, sent directly.
bool send(QWidget *widget, QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, at, widget->mapToGlobal(at), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
    return event.isAccepted();
}

void press(QWidget *widget, QPointF at, Qt::MouseButton button = Qt::LeftButton)
{
    send(widget, QEvent::MouseButtonPress, at, button, button);
}

void move(QWidget *widget, QPointF at, Qt::MouseButtons buttons = Qt::LeftButton)
{
    send(widget, QEvent::MouseMove, at, Qt::NoButton, buttons);
}

void release(QWidget *widget, QPointF at, Qt::MouseButton button = Qt::LeftButton)
{
    send(widget, QEvent::MouseButtonRelease, at, button, Qt::NoButton);
}
}

class NumericScrubTests : public QObject {
    Q_OBJECT
private slots:
    void theLabelShowsTheSidewaysCursor();
    void aDragFromThePressSetsTheValueOnceItMoves();
    void stepsSnapAndTheRangeClamps();
    void aClickAloneChangesNothing();
    void onlyTheLeftButtonScrubs();
    void aLostReleaseEndsTheDrag();
    void theDragOutlivesThePointerLeavingTheLabel();
    void aDisabledLabelScrubsNothing();
    void anyMoveOfAPointBeginsTheDrag();
    void aDoubleClicksSecondPressScrubs();
};

void NumericScrubTests::theLabelShowsTheSidewaysCursor()
{
    Probe probe;
    QCOMPARE(probe.label->cursor().shape(), Qt::SizeHorCursor);
    QVERIFY(probe.label->testAttribute(Qt::WA_SetCursor));
    QVERIFY(!probe.window.testAttribute(Qt::WA_SetCursor));
    // Tracked, so a move after a lost release arrives.
    QVERIFY(probe.label->hasMouseTracking());
}

void NumericScrubTests::aDragFromThePressSetsTheValueOnceItMoves()
{
    Probe probe(0.5);
    press(probe.label, QPointF(10, 5));
    // Unmoved, Swift's gesture has not begun.
    move(probe.label, QPointF(10, 5));
    QVERIFY(probe.log.isEmpty());
    move(probe.label, QPointF(14, 5));
    QCOMPARE(probe.log, QStringList({"began", "12"}));
    // Only the sideways part counts; the start stays fixed.
    move(probe.label, QPointF(40, 30));
    QCOMPARE(probe.value, 25.0);
    move(probe.label, QPointF(9, 5));
    QCOMPARE(probe.value, 9.5);
    QCOMPARE(probe.log.count("began"), 1);
    release(probe.label, QPointF(9, 5));
    QCOMPARE(probe.log.last(), QString("ended"));
    QCOMPARE(probe.log.count("ended"), 1);
    // The next drag starts from where the last one left.
    press(probe.label, QPointF(10, 5));
    move(probe.label, QPointF(30, 5));
    QCOMPARE(probe.value, 19.5);
    release(probe.label, QPointF(30, 5));
    QCOMPARE(probe.log.count("began"), 2);
    QCOMPARE(probe.log.count("ended"), 2);
}

void NumericScrubTests::stepsSnapAndTheRangeClamps()
{
    Probe probe(0.3, 5, 12, 1);
    press(probe.label, QPointF(100, 5));
    move(probe.label, QPointF(105, 5));
    // 10 + 1.5 rounds away from zero, as Swift's `rounded()`.
    QCOMPARE(probe.value, 12.0);
    move(probe.label, QPointF(104, 5));
    QCOMPARE(probe.value, 11.0);
    move(probe.label, QPointF(200, 5));
    QCOMPARE(probe.value, 12.0);
    move(probe.label, QPointF(0, 5));
    QCOMPARE(probe.value, 5.0);
    release(probe.label, QPointF(0, 5));
    // A step that is no step leaves the value unsnapped.
    Probe free(0.3, 0, 100, 0);
    press(free.label, QPointF(100, 5));
    move(free.label, QPointF(105, 5));
    QCOMPARE(free.value, 11.5);
    release(free.label, QPointF(105, 5));
    // Halves below zero round away from it, as Swift's.
    Probe negative(0.5, -100, 100, 1);
    press(negative.label, QPointF(100, 5));
    move(negative.label, QPointF(77, 5));
    QCOMPARE(negative.value, -2.0);
    release(negative.label, QPointF(77, 5));
    // A fractional step snaps to its multiples.
    Probe tenths(0.07, 0, 100, 0.25);
    press(tenths.label, QPointF(100, 5));
    move(tenths.label, QPointF(105, 5));
    QCOMPARE(tenths.value, 10.25);
    release(tenths.label, QPointF(105, 5));
}

void NumericScrubTests::aClickAloneChangesNothing()
{
    Probe probe;
    QVERIFY(send(probe.label, QEvent::MouseButtonPress, QPointF(10, 5), Qt::LeftButton, Qt::LeftButton));
    release(probe.label, QPointF(10, 5));
    QVERIFY(probe.log.isEmpty());
    // Moves without a press scrub nothing.
    move(probe.label, QPointF(40, 5));
    QVERIFY(probe.log.isEmpty());
    QCOMPARE(probe.value, 10.0);
}

void NumericScrubTests::onlyTheLeftButtonScrubs()
{
    Probe probe;
    // Unpressed, moves go on to the label untouched.
    send(probe.label, QEvent::MouseMove, QPointF(40, 5), Qt::NoButton, Qt::NoButton);
    QCOMPARE(probe.counter.moves, 1);
    // The label lets other presses go on to its parent.
    QVERIFY(!send(probe.label, QEvent::MouseButtonPress, QPointF(10, 5), Qt::RightButton, Qt::RightButton));
    move(probe.label, QPointF(40, 5), Qt::RightButton);
    release(probe.label, QPointF(40, 5), Qt::RightButton);
    QVERIFY(probe.log.isEmpty());
    // Another button's release leaves a left drag going.
    press(probe.label, QPointF(10, 5));
    move(probe.label, QPointF(20, 5));
    send(probe.label, QEvent::MouseButtonRelease, QPointF(20, 5), Qt::RightButton, Qt::LeftButton);
    send(probe.other, QEvent::MouseButtonRelease, QPointF(5, 5), Qt::RightButton, Qt::LeftButton);
    move(probe.label, QPointF(30, 5));
    QCOMPARE(probe.log, QStringList({"began", "20", "30"}));
    // The drag's own moves stop at the scrub.
    QCOMPARE(probe.counter.moves, 2);
    release(probe.label, QPointF(30, 5));
    QCOMPARE(probe.log.last(), QString("ended"));
}

void NumericScrubTests::aLostReleaseEndsTheDrag()
{
    Probe probe;
    press(probe.label, QPointF(10, 5));
    move(probe.label, QPointF(20, 5));
    // A move without the button: the release went elsewhere.
    move(probe.label, QPointF(60, 5), Qt::NoButton);
    QCOMPARE(probe.log, QStringList({"began", "20", "ended"}));
    move(probe.label, QPointF(70, 5));
    QCOMPARE(probe.value, 20.0);
    // A release a popup takes ends it, seen application-wide.
    press(probe.label, QPointF(10, 5));
    move(probe.label, QPointF(15, 5));
    release(probe.other, QPointF(5, 5));
    QCOMPARE(probe.log.mid(3), QStringList({"began", "25", "ended"}));
    move(probe.label, QPointF(70, 5));
    QCOMPARE(probe.value, 25.0);
    // Another widget's release before any move ends only the press.
    press(probe.label, QPointF(10, 5));
    release(probe.other, QPointF(5, 5));
    move(probe.label, QPointF(70, 5));
    QCOMPARE(probe.log.size(), 6);
    // A move holding another button alone lost the left release.
    press(probe.label, QPointF(10, 5));
    move(probe.label, QPointF(12, 5));
    move(probe.label, QPointF(30, 5), Qt::RightButton);
    QCOMPARE(probe.log.mid(6), QStringList({"began", "27", "ended"}));
}

void NumericScrubTests::theDragOutlivesThePointerLeavingTheLabel()
{
    Probe probe;
    QWindow *window = probe.window.windowHandle();
    const QPoint start = probe.label->mapTo(&probe.window, QPoint(5, probe.label->height() / 2));
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(window, start + QPoint(3, 0));
    // Past the label and the field, out of the window.
    QTest::mouseMove(window, start + QPoint(200, 0));
    QTest::mouseMove(window, start + QPoint(400, 0));
    QCOMPARE(probe.value, 100.0);
    QTest::mouseMove(window, start + QPoint(30, 0));
    QCOMPARE(probe.value, 40.0);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, start + QPoint(30, 0));
    QCOMPARE(probe.log, QStringList({"began", "13", "100", "100", "40", "ended"}));
    QCOMPARE(probe.value, 40.0);
}

void NumericScrubTests::aDisabledLabelScrubsNothing()
{
    Probe probe;
    probe.label->setEnabled(false);
    QWindow *window = probe.window.windowHandle();
    const QPoint start = probe.label->mapTo(&probe.window, QPoint(5, probe.label->height() / 2));
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(window, start + QPoint(30, 0));
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, start + QPoint(30, 0));
    QVERIFY(probe.log.isEmpty());
    QCOMPARE(probe.value, 10.0);
}

void NumericScrubTests::anyMoveOfAPointBeginsTheDrag()
{
    Probe probe;
    press(probe.label, QPointF(10, 5));
    // Under a point, Swift's gesture has not begun.
    move(probe.label, QPointF(10.6, 5));
    QVERIFY(probe.log.isEmpty());
    move(probe.label, QPointF(11, 5));
    QCOMPARE(probe.log, QStringList({"began", "11"}));
    release(probe.label, QPointF(11, 5));
    // Straight down begins too, and sets the start again.
    press(probe.label, QPointF(10, 5));
    move(probe.label, QPointF(10, 8));
    QCOMPARE(probe.log.mid(3), QStringList({"began", "11"}));
    release(probe.label, QPointF(10, 8));
}

void NumericScrubTests::aDoubleClicksSecondPressScrubs()
{
    Probe probe;
    press(probe.label, QPointF(10, 5));
    release(probe.label, QPointF(10, 5));
    send(probe.label, QEvent::MouseButtonDblClick, QPointF(10, 5), Qt::LeftButton, Qt::LeftButton);
    move(probe.label, QPointF(30, 5));
    release(probe.label, QPointF(30, 5));
    QCOMPARE(probe.log, QStringList({"began", "30", "ended"}));
    // A disabled label's double click scrubs nothing either.
    probe.label->setEnabled(false);
    send(probe.label, QEvent::MouseButtonDblClick, QPointF(10, 5), Qt::LeftButton, Qt::LeftButton);
    move(probe.label, QPointF(60, 5));
    QCOMPARE(probe.value, 30.0);
}

QTEST_MAIN(NumericScrubTests)
#include "NumericScrubTests.moc"
