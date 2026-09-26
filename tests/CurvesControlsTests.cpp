#include "UI/CurvesControls.h"
#include <QAccessible>
#include <QApplication>
#include <QDialog>
#include <QPainter>
#include <QtTest>

// Swift's CurvesControls: add, drag, remove, reset, channel.
namespace {
// Controls over their own settings, a pixel a unit.
struct Curves {
    CurvesSettings settings;
    CurvesControls controls{[this] { return settings; }, [this](const CurvesSettings &changed) { settings = changed; }};
    Curves()
    {
        controls.resize(255, 420);
        controls.show();
        if (!QTest::qWaitForWindowExposed(&controls))
            throw std::runtime_error("the controls never showed");
    }
    QWidget &graph() const { return *controls.findChild<QWidget *>(QStringLiteral("curvesGraph")); }
    QPushButton &button(const QString &text) const
    {
        for (QPushButton *button : controls.findChildren<QPushButton *>()) {
            if (button->text() == text)
                return *button;
        }
        throw std::runtime_error(text.toStdString());
    }
    QLabel &readout() const { return *controls.findChild<QLabel *>(QStringLiteral("curvesReadout")); }
    const std::vector<CurvePoint> &points() const { return settings.channels.at(size_t(settings.channel)); }
    void press(QPoint at, Qt::MouseButton button = Qt::LeftButton) { QTest::mousePress(&graph(), button, Qt::NoModifier, at); }
    void move(QPoint to, Qt::MouseButtons buttons = Qt::LeftButton)
    {
        QMouseEvent event(QEvent::MouseMove, to, graph().mapToGlobal(to), Qt::NoButton, buttons, Qt::NoModifier);
        QApplication::sendEvent(&graph(), &event);
    }
    void release(QPoint at, Qt::MouseButton button = Qt::LeftButton) { QTest::mouseRelease(&graph(), button, Qt::NoModifier, at); }
    void click(QPoint at)
    {
        press(at);
        release(at);
    }
};

// A curve with points at even x, 32 in all.
std::vector<CurvePoint> full()
{
    std::vector<CurvePoint> points{{0, 0}};
    for (int index = 1; index < 31; ++index)
        points.push_back({double(index * 8), double(index * 8)});
    points.push_back({255, 255});
    return points;
}
}

class CurvesControlsTests : public QObject {
    Q_OBJECT
private slots:
    void theControlsShowSwiftsParts();
    void aClickAddsAPointThatDragsBetweenItsNeighbours();
    void aPressNearAPointTakesItAndNewOnesNeedRoom();
    void removeAndResetTheChannelsCurve();
    void anotherChannelDropsTheSelection();
    void theGraphDrawsGridCurveAndPoints();
};

void CurvesControlsTests::theControlsShowSwiftsParts()
{
    Curves shown;
    QComboBox &channel = *shown.controls.findChild<QComboBox *>();
    QCOMPARE(channel.count(), 4);
    QCOMPARE(channel.itemText(1), QString("Red"));
    const auto labels = QAccessible::queryAccessibleInterface(&channel)->relations(QAccessible::Label);
    QCOMPARE(labels.size(), 1);
    QCOMPARE(labels.first().first->text(QAccessible::Name), QString("Channel"));
    // Rows twelve apart: picker, graph, hint, readout, reset.
    QWidget &graph = shown.graph();
    QCOMPARE(graph.geometry(), QRect(0, channel.geometry().bottom() + 13, 255, 260));
    QLabel *hint = nullptr;
    for (QLabel *label : shown.controls.findChildren<QLabel *>()) {
        if (label->text() == QStringLiteral("Click to add a point. Drag to adjust."))
            hint = label;
    }
    QVERIFY(hint && hint->y() == graph.geometry().bottom() + 13);
    QCOMPARE(hint->font().pixelSize(), 10);
    QCOMPARE(hint->foregroundRole(), QPalette::PlaceholderText);
    QPushButton &remove = shown.button("Remove point"), &reset = shown.button("Reset curve");
    QVERIFY(shown.readout().isHidden() && !remove.isEnabled());
    QCOMPARE(remove.geometry().right(), 254);
    QCOMPARE(remove.width(), remove.sizeHint().width());
    QCOMPARE(reset.x(), 0);
    QCOMPARE(reset.width(), reset.sizeHint().width());
    QCOMPARE(reset.y(), remove.geometry().bottom() + 13);
    // In a panel, Return still reaches the panel's default.
    QDialog panel;
    CurvesSettings settings;
    CurvesControls inside([&] { return settings; }, [&](const CurvesSettings &changed) { settings = changed; }, &panel);
    for (QPushButton *button : inside.findChildren<QPushButton *>())
        QVERIFY(!button->autoDefault());
}

void CurvesControlsTests::aClickAddsAPointThatDragsBetweenItsNeighbours()
{
    Curves shown;
    shown.press(QPoint(100, 104));
    QCOMPARE(shown.points(), (std::vector<CurvePoint>{{0, 0}, {100, 153}, {255, 255}}));
    QVERIFY(!shown.readout().isHidden());
    QCOMPARE(shown.readout().text(), QString("Input 100 · Output 153"));
    QVERIFY(shown.button("Remove point").isEnabled());
    // Dragged, it keeps a unit from each neighbour.
    shown.move(QPoint(400, -20));
    QCOMPARE(shown.points()[1], (CurvePoint{254, 255}));
    shown.move(QPoint(-50, 300));
    QCOMPARE(shown.points()[1], (CurvePoint{1, 0}));
    QCOMPARE(shown.readout().text(), QString("Input 1 · Output 0"));
    // Other buttons move nothing.
    shown.move(QPoint(100, 104), Qt::RightButton);
    QCOMPARE(shown.points()[1], (CurvePoint{1, 0}));
    shown.release(QPoint(-50, 300));
    // Released: the next press away adds another.
    shown.click(QPoint(200, 51));
    QCOMPARE(shown.points().size(), size_t(4));
    QVERIFY(shown.points()[2].x == 200 && shown.points()[2].y > 204.9);
    // The readout cuts fractions, as Swift's Int().
    QCOMPARE(shown.readout().text(), QString("Input 200 · Output 204"));
    // Only the left button presses.
    shown.press(QPoint(150, 104), Qt::MiddleButton);
    shown.release(QPoint(150, 104), Qt::MiddleButton);
    QCOMPARE(shown.points().size(), size_t(4));
}

void CurvesControlsTests::aPressNearAPointTakesItAndNewOnesNeedRoom()
{
    Curves shown;
    // Within fourteen units the end moves, up and down only.
    shown.press(QPoint(5, 255));
    shown.move(QPoint(9, 208));
    shown.release(QPoint(9, 208));
    QCOMPARE(shown.points(), (std::vector<CurvePoint>{{0, 51}, {255, 255}}));
    QCOMPARE(shown.readout().text(), QString("Input 0 · Output 51"));
    QVERIFY(!shown.button("Remove point").isEnabled());
    // A press past an edge reads as the edge.
    shown.press(QPoint(300, 3));
    shown.move(QPoint(300, 26));
    shown.release(QPoint(300, 26));
    QCOMPARE(shown.points().back(), (CurvePoint{255, 229.5}));
    shown.press(QPoint(-30, 195));
    shown.release(QPoint(-30, 195));
    QCOMPARE(shown.points().front(), (CurvePoint{0, 63.75}));
    // Fourteen units off is too far to take.
    shown.settings.channels[0] = {{0, 51}, {255, 255}};
    shown.click(QPoint(14, 208));
    QCOMPARE(shown.points().size(), size_t(3));
    shown.settings.channels[0] = {{0, 51}, {255, 255}};
    shown.controls.synchronize();
    // Farther, a point is added; none at the edges.
    shown.click(QPoint(20, 260));
    QCOMPARE(shown.points().size(), size_t(3));
    for (const QPoint edge : {QPoint(1, 104), QPoint(254, 104), QPoint(21, 104)})
        shown.click(edge);
    QCOMPARE(shown.points(), (std::vector<CurvePoint>{{0, 51}, {20, 0}, {255, 255}}));
    shown.click(QPoint(22, 104));
    QCOMPARE(shown.points().size(), size_t(4));
    // Thirty-two is the most; the right button adds none.
    shown.settings.channels[0] = full();
    shown.controls.synchronize();
    shown.click(QPoint(100, 52));
    QCOMPARE(shown.points().size(), size_t(32));
    shown.settings.channels[0] = {{0, 0}, {255, 255}};
    shown.press(QPoint(100, 104), Qt::RightButton);
    shown.release(QPoint(100, 104), Qt::RightButton);
    QCOMPARE(shown.points().size(), size_t(2));
}

void CurvesControlsTests::removeAndResetTheChannelsCurve()
{
    Curves shown;
    shown.settings.channels[2] = {{0, 20}, {255, 90}};
    shown.click(QPoint(100, 104));
    shown.click(QPoint(200, 52));
    shown.click(QPoint(100, 104));
    QCOMPARE(shown.readout().text(), QString("Input 100 · Output 153"));
    shown.button("Remove point").click();
    QCOMPARE(shown.points(), (std::vector<CurvePoint>{{0, 0}, {200, 204}, {255, 255}}));
    QVERIFY(shown.readout().isHidden() && !shown.button("Remove point").isEnabled());
    // The last point stays: Remove rests there.
    shown.click(QPoint(250, 0));
    QCOMPARE(shown.readout().text(), QString("Input 255 · Output 255"));
    QVERIFY(!shown.button("Remove point").isEnabled());
    shown.click(QPoint(200, 52));
    QCOMPARE(shown.readout().text(), QString("Input 200 · Output 204"));
    shown.click(QPoint(100, 104));
    QCOMPARE(shown.readout().text(), QString("Input 100 · Output 153"));
    // Reset drops the selection, which would read the new end.
    shown.button("Reset curve").click();
    QCOMPARE(shown.points(), (std::vector<CurvePoint>{{0, 0}, {255, 255}}));
    QVERIFY(shown.readout().isHidden());
    QCOMPARE(shown.settings.channels[2], (std::vector<CurvePoint>{{0, 20}, {255, 90}}));
    // A selection gone from elsewhere removes nothing.
    shown.click(QPoint(100, 104));
    shown.click(QPoint(200, 52));
    QCOMPARE(shown.readout().text(), QString("Input 200 · Output 204"));
    shown.settings.channels[0] = {{0, 0}, {255, 255}};
    shown.controls.synchronize();
    QVERIFY(shown.readout().isHidden() && shown.button("Remove point").isEnabled());
    shown.button("Remove point").click();
    QCOMPARE(shown.points(), (std::vector<CurvePoint>{{0, 0}, {255, 255}}));
}

void CurvesControlsTests::anotherChannelDropsTheSelection()
{
    Curves shown;
    shown.click(QPoint(100, 104));
    QVERIFY(shown.button("Remove point").isEnabled());
    QComboBox &channel = *shown.controls.findChild<QComboBox *>();
    channel.setFocus();
    QTest::keyClick(&channel, Qt::Key_Down);
    QCOMPARE(shown.settings.channel, LevelsChannel::red);
    QVERIFY(shown.readout().isHidden() && !shown.button("Remove point").isEnabled());
    // Each channel keeps its own points.
    shown.click(QPoint(50, 156));
    QCOMPARE(shown.settings.channels[1].size(), size_t(3));
    QCOMPARE(shown.settings.channels[0].size(), size_t(3));
    // A change from elsewhere drops it too, mid-drag included.
    shown.press(QPoint(50, 156));
    shown.settings.channel = LevelsChannel::blue;
    shown.controls.synchronize();
    QCOMPARE(channel.currentText(), QString("Blue"));
    QVERIFY(shown.readout().isHidden());
    // The drag starts over: far from both ends, it adds.
    shown.move(QPoint(3, 3));
    QCOMPARE(shown.points().size(), size_t(3));
    QCOMPARE(shown.points().back(), (CurvePoint{255, 255}));
    shown.release(QPoint(3, 3));
    // A point taken, then gone from elsewhere, drags nothing.
    shown.press(QPoint(128, 130));
    QCOMPARE(shown.points().size(), size_t(4));
    shown.settings.channels[3] = {{0, 0}, {255, 255}};
    shown.controls.synchronize();
    shown.move(QPoint(200, 0));
    QCOMPARE(shown.points(), (std::vector<CurvePoint>{{0, 0}, {255, 255}}));
    shown.release(QPoint(200, 0));
}

void CurvesControlsTests::theGraphDrawsGridCurveAndPoints()
{
    Curves shown;
    const QColor window = shown.controls.palette().color(QPalette::Window);
    QImage blended(1, 1, QImage::Format_ARGB32_Premultiplied);
    blended.fill(window);
    QPainter(&blended).fillRect(0, 0, 1, 1, QColor(0, 0, 0, 89));
    const QColor background = blended.pixelColor(0, 0);
    QImage drawn = shown.graph().grab().toImage();
    QCOMPARE(drawn.pixelColor(30, 20), background);
    // Quarter lines a pixel wide at 12%, the edges' too.
    QCOMPARE(drawn.pixelColor(63, 20), QColor(0xa5, 0xa5, 0xa5));
    QCOMPARE(drawn.pixelColor(64, 20), QColor(0x9f, 0x9f, 0x9f));
    for (const QPoint half : {QPoint(0, 20), QPoint(254, 20), QPoint(30, 0), QPoint(30, 64), QPoint(30, 259)})
        QCOMPARE(drawn.pixelColor(half), QColor(0xa2, 0xa2, 0xa2));
    // The whole curve, two wide, antialiased.
    QCOMPARE(drawn.pixelColor(128, 127), QColor(0xca, 0xca, 0xca));
    QCOMPARE(drawn.pixelColor(128, 130), QColor(0xc9, 0xc9, 0xc9));
    QCOMPARE(drawn.pixelColor(200, 55), QColor(Qt::white));
    QCOMPARE(drawn.pixelColor(200, 57), QColor(0xa9, 0xa9, 0xa9));
    // Each channel draws its own curve.
    shown.settings.channel = LevelsChannel::red;
    shown.settings.channels[1] = {{0, 128}, {255, 128}};
    shown.controls.synchronize();
    drawn = shown.graph().grab().toImage();
    QVERIFY(drawn.pixelColor(30, 129).red() == 255 && drawn.pixelColor(30, 229) == background);
    shown.settings.channel = LevelsChannel::rgb;
    // The quarter lines, a twelfth white, over it.
    QVERIFY(drawn.pixelColor(64, 20).red() > background.red() && drawn.pixelColor(64, 20).red() < background.red() + 40);
    QVERIFY(drawn.pixelColor(30, 65).red() > background.red());
    // The curve, white and two wide; points are white dots.
    QVERIFY(drawn.pixelColor(128, 129).red() >= 250);
    shown.settings.channels[0] = {{0, 0}, {100, 204}, {255, 255}};
    shown.controls.synchronize();
    drawn = shown.graph().grab().toImage();
    QCOMPARE(drawn.pixelColor(97, 50), QColor(Qt::white));
    QCOMPARE(drawn.pixelColor(95, 50), background);
    // A change repaints the graph whole.
    QRegion painted;
    struct Spy : QObject {
        QRegion &region;
        explicit Spy(QRegion &region) : region(region) {}
        bool eventFilter(QObject *, QEvent *event) override
        {
            if (event->type() == QEvent::Paint)
                region += static_cast<QPaintEvent *>(event)->region();
            return false;
        }
    } spy(painted);
    shown.graph().installEventFilter(&spy);
    shown.settings.channels[0] = {{0, 0}, {100, 200}, {255, 255}};
    shown.controls.synchronize();
    QTRY_VERIFY(painted.contains(shown.graph().rect()));
    shown.graph().removeEventFilter(&spy);
    // The chosen one takes the accent.
    shown.click(QPoint(100, 52));
    QCOMPARE(shown.graph().grab().toImage().pixelColor(97, 50), shown.controls.palette().color(QPalette::Highlight));
}

QTEST_MAIN(CurvesControlsTests)
#include "CurvesControlsTests.moc"
