#include "Rendering/EditorCanvas.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/ProjectWorkspaceView.h"
#include <QLineEdit>
#include <QStandardPaths>
#include <QtTest>

// Swift's stepped keyboard zoom: the stops, the centre, the keys.
namespace {
void expectClose(double actual, double expected)
{
    QVERIFY2(std::abs(actual - expected) < 0.000001, qPrintable(QString::number(actual, 'g', 12)));
}
}

class KeyboardZoomTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void keyboardZoomKeepsPannedViewportCenterFixed();
    void keyboardZoomUsesStableStopsAndClampsAtTheEnds();
    void keyboardZoomRoundTripDoesNotDriftAfterTenSteps();
    void keyboardZoomKeepsSmallCanvasCentered();
    void keyboardZoomFromFitUsesNextStopAndLeavesFitMode();
    void commandZoomUpdatesOnKeyDownAndRepeat();
};

void KeyboardZoomTests::initTestCase()
{
    // Remapped keys live in QSettings: the tests' own folder.
    QStandardPaths::setTestModeEnabled(true);
    ShortcutSettings::shared().save({});
}

void KeyboardZoomTests::keyboardZoomKeepsPannedViewportCenterFixed()
{
    EditorSession session;
    session.viewport.resize(QSizeF(1000, 800), 1, std::nullopt);
    session.createDocument(3000, 2000);
    session.zoom(1);
    session.viewport.translate(QSizeF(-620, 185));
    const QPointF center = session.viewport.center();
    const QSizeF canvas(3000, 2000);
    const QPointF before = session.viewport.documentPoint(center, canvas);
    session.zoomKeyboard(1);
    const QPointF after = session.viewport.documentPoint(center, canvas);
    QCOMPARE(session.viewport.zoom(), 1.25);
    expectClose(before.x(), after.x());
    expectClose(before.y(), after.y());
    expectClose(session.viewport.viewPoint(after, canvas).x(), center.x());
    expectClose(session.viewport.viewPoint(after, canvas).y(), center.y());
}

void KeyboardZoomTests::keyboardZoomUsesStableStopsAndClampsAtTheEnds()
{
    EditorSession session;
    session.viewport.resize(QSizeF(1000, 800), 1, std::nullopt);
    session.createDocument(3000, 2000);
    session.zoom(0.5);
    session.zoomKeyboard(1);
    expectClose(session.viewport.zoom(), 2.0 / 3.0);
    session.zoomKeyboard(1);
    QCOMPARE(session.viewport.zoom(), 1.0);
    session.zoomKeyboard(1);
    QCOMPARE(session.viewport.zoom(), 1.25);
    session.zoom(CanvasViewport::keyboardZoomLevels.front());
    session.zoomKeyboard(-1);
    QCOMPARE(session.viewport.zoom(), CanvasViewport::keyboardZoomLevels.front());
    session.zoom(CanvasViewport::keyboardZoomLevels.back());
    session.zoomKeyboard(1);
    QCOMPARE(session.viewport.zoom(), CanvasViewport::keyboardZoomLevels.back());
    // Every stop, both ways; past the end, nothing.
    const std::array expected{0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 1.25, 1.5, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0, 12.0, 16.0};
    QVERIFY(CanvasViewport::keyboardZoomLevels == expected);
    session.zoom(0.1);
    for (const double stop : expected) {
        session.zoomKeyboard(1);
        QCOMPARE(session.viewport.zoom(), stop);
    }
    QSignalSpy changed(&session, &EditorSession::changed);
    session.zoomKeyboard(1);
    session.zoomKeyboard(0);
    QCOMPARE(changed.count(), 0);
    session.zoom(20);
    for (auto stop = expected.rbegin(); stop != expected.rend(); ++stop) {
        session.zoomKeyboard(-1);
        QCOMPARE(session.viewport.zoom(), *stop);
    }
    // Step 0 stays; between stops, the next each way.
    session.zoom(0.7);
    QCOMPARE(session.viewport.keyboardZoomTarget(0), 0.7);
    QCOMPARE(session.viewport.keyboardZoomTarget(-1), 2.0 / 3.0);
    QCOMPARE(session.viewport.keyboardZoomTarget(1), 1.0);
    session.zoom(1.0000000001);
    QCOMPARE(session.viewport.keyboardZoomTarget(1), 1.25);
    QCOMPARE(session.viewport.keyboardZoomTarget(-1), 2.0 / 3.0);
    // The tolerance from each side: its floor and its share.
    session.zoom(0.9999999995);
    QCOMPARE(session.viewport.keyboardZoomTarget(1), 1.25);
    session.zoom(12.000000006);
    QCOMPARE(session.viewport.keyboardZoomTarget(-1), 8.0);
    session.zoom(0.49999999925);
    QCOMPARE(session.viewport.keyboardZoomTarget(1), 2.0 / 3.0);
    // No document, nothing.
    EditorSession empty;
    empty.zoomKeyboard(1);
    QCOMPARE(empty.viewport.zoom(), 1.0);
}

void KeyboardZoomTests::keyboardZoomRoundTripDoesNotDriftAfterTenSteps()
{
    EditorSession session;
    session.viewport.resize(QSizeF(1200, 900), 2, std::nullopt);
    session.createDocument(4000, 3000);
    session.zoom(0.5);
    session.viewport.translate(QSizeF(-430, 275));
    const QPointF center = session.viewport.center();
    const QSizeF canvas(4000, 3000);
    const QPointF before = session.viewport.documentPoint(center, canvas);
    for (int step = 0; step < 10; ++step)
        session.zoomKeyboard(1);
    for (int step = 0; step < 10; ++step)
        session.zoomKeyboard(-1);
    const QPointF after = session.viewport.documentPoint(center, canvas);
    QCOMPARE(session.viewport.zoom(), 0.5);
    expectClose(before.x(), after.x());
    expectClose(before.y(), after.y());
}

void KeyboardZoomTests::keyboardZoomKeepsSmallCanvasCentered()
{
    EditorSession session;
    session.viewport.resize(QSizeF(1200, 900), 1, std::nullopt);
    session.createDocument(200, 100);
    session.zoom(0.5);
    const QPointF center = session.viewport.center();
    const QSizeF canvas(200, 100);
    const QPointF before = session.viewport.documentPoint(center, canvas);
    session.zoomKeyboard(1);
    const QPointF after = session.viewport.documentPoint(center, canvas);
    expectClose(session.viewport.zoom(), 2.0 / 3.0);
    expectClose(before.x(), 100);
    expectClose(before.y(), 50);
    expectClose(before.x(), after.x());
    expectClose(before.y(), after.y());
}

void KeyboardZoomTests::keyboardZoomFromFitUsesNextStopAndLeavesFitMode()
{
    EditorSession session;
    session.viewport.resize(QSizeF(1000, 800), 1, std::nullopt);
    session.createDocument(1500, 1000);
    session.fit();
    const QPointF center = session.viewport.center();
    const QSizeF canvas(1500, 1000);
    const QPointF before = session.viewport.documentPoint(center, canvas);
    session.zoomKeyboard(1);
    const QPointF after = session.viewport.documentPoint(center, canvas);
    expectClose(session.viewport.zoom(), 2.0 / 3.0);
    QVERIFY(!session.viewport.followsFit());
    expectClose(before.x(), after.x());
    expectClose(before.y(), after.y());
}

void KeyboardZoomTests::commandZoomUpdatesOnKeyDownAndRepeat()
{
    // The app: its menu holds Ctrl+=, Ctrl+- and keypad minus.
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    window.resize(900, 600);
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    EditorSession &session = workspace.current().session;
    session.createDocument(3000, 2000);
    session.zoom(1);
    CanvasView &canvas = *window.findChild<CanvasView *>();
    canvas.setFocus();
    QTRY_VERIFY(canvas.hasFocus());
    QTest::keyClick(&canvas, Qt::Key_Equal, Qt::ControlModifier);
    QCOMPARE(session.viewport.zoom(), 1.25);
    // Ctrl++ and its repeat, and keypad plus: the canvas's filter.
    QTest::keyClick(&canvas, Qt::Key_Plus, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(session.viewport.zoom(), 1.5);
    QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Plus, Qt::ControlModifier | Qt::ShiftModifier, QStringLiteral("+"), true);
    QApplication::sendEvent(&canvas, &repeat);
    QCOMPARE(session.viewport.zoom(), 2.0);
    QTest::keyClick(&canvas, Qt::Key_Plus, Qt::ControlModifier | Qt::KeypadModifier);
    QCOMPARE(session.viewport.zoom(), 3.0);
    QTest::keyClick(&canvas, Qt::Key_Minus, Qt::ControlModifier);
    QCOMPARE(session.viewport.zoom(), 2.0);
    QTest::keyClick(&canvas, Qt::Key_Minus, Qt::ControlModifier | Qt::KeypadModifier);
    QCOMPARE(session.viewport.zoom(), 1.5);
    // Shift-minus, Alt or Meta, or no Ctrl: no zoom.
    QTest::keyClick(&canvas, Qt::Key_Underscore, Qt::ControlModifier | Qt::ShiftModifier);
    QTest::keyClick(&canvas, Qt::Key_Plus, Qt::ControlModifier | Qt::AltModifier);
    QTest::keyClick(&canvas, Qt::Key_Plus, Qt::ControlModifier | Qt::MetaModifier);
    QTest::keyClick(&canvas, Qt::Key_Plus, Qt::ShiftModifier);
    QCOMPARE(session.viewport.zoom(), 1.5);
    // A remapped Zoom In leaves Ctrl++ alone.
    QVERIFY(ShortcutSettings::shared().save({{QStringLiteral("Menus:Zoom In"), ShortcutChord(QStringLiteral("k"), 1)}}));
    QTest::keyClick(&canvas, Qt::Key_Plus, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(session.viewport.zoom(), 1.5);
    ShortcutSettings::shared().save({});
    // A text field keeps its keys.
    auto *field = new QLineEdit(&window);
    field->show();
    field->setFocus();
    QTRY_VERIFY(field->hasFocus());
    QTest::keyClick(field, Qt::Key_Plus, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(session.viewport.zoom(), 1.5);
    // No document: Ctrl++ passes on to whoever holds focus.
    struct Keys : QWidget {
        int count = 0;
        void keyPressEvent(QKeyEvent *event) override { count += event->key() == Qt::Key_Plus; }
    } *keys = new Keys;
    keys->setParent(&window);
    keys->setFocusPolicy(Qt::StrongFocus);
    keys->show();
    keys->setFocus();
    QTRY_VERIFY(keys->hasFocus());
    QTest::keyClick(keys, Qt::Key_Plus, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(session.viewport.zoom(), 2.0);
    QCOMPARE(keys->count, 0);
    session.clearProject();
    QTest::keyClick(keys, Qt::Key_Plus, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(keys->count, 1);
}

QTEST_MAIN(KeyboardZoomTests)
#include "KeyboardZoomTests.moc"
