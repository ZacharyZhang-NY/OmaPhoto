#include "Rendering/EditorCanvas.h"
#include "UI/NativeLayerList.h"
#include <QLineEdit>
#include <QtTest>

// Shift-+ and Shift-− step the blend mode wherever focus sits.
class BlendShortcutTests : public QObject {
    Q_OBJECT
private slots:
    void shiftPlusAndMinusStepTheActiveLayersBlendModeWhereverFocusIsExceptTextFields();
};

void BlendShortcutTests::shiftPlusAndMinusStepTheActiveLayersBlendModeWhereverFocusIsExceptTextFields()
{
    EditorSession session;
    session.createDocument(40, 20);
    session.addBlankLayer();
    QWidget window;
    auto *canvas = new CanvasView(session, &window);
    canvas->setGeometry(0, 0, 300, 200);
    auto *list = new NativeLayerList(session, &window);
    list->setGeometry(300, 0, 200, 200);
    auto *field = new QLineEdit(&window);
    field->setGeometry(0, 210, 100, 22);
    window.resize(500, 240);
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    const auto mode = [&] { return session.activeLayer().value().blendMode; };
    session.selectTool(NavigationTool::brush);
    canvas->setFocus();
    QTRY_VERIFY(canvas->hasFocus());
    QTest::keyClick(canvas, Qt::Key_Plus, Qt::ShiftModifier);
    QCOMPARE(mode(), LayerBlendMode::multiply);
    // From the list, back past Normal to the last mode.
    session.selectTool(NavigationTool::lasso);
    list->setFocus();
    QTRY_VERIFY(list->hasFocus());
    QTest::keyClick(list, Qt::Key_Underscore, Qt::ShiftModifier);
    QTest::keyClick(list, Qt::Key_Underscore, Qt::ShiftModifier);
    QCOMPARE(mode(), LayerBlendMode::luminosity);
    session.undo();
    QCOMPARE(mode(), LayerBlendMode::normal);
    // Typing in a text field keeps its characters.
    field->setFocus();
    QTRY_VERIFY(field->hasFocus());
    QTest::keyClick(field, Qt::Key_Plus, Qt::ShiftModifier);
    QCOMPARE(mode(), LayerBlendMode::normal);
    QCOMPARE(field->text(), QString("+"));
}

QTEST_MAIN(BlendShortcutTests)
#include "BlendShortcutTests.moc"
