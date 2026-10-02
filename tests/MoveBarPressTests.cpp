#include "MovePressFixtures.h"
#include "UI/TransformInspector.h"
#include <QLineEdit>

// The Move bar's value and a folder, against a real canvas press.
class MoveBarPressTests : public QObject {
    Q_OBJECT
private slots:
    void aTypedValueThenAPressDragsTheActiveLayer();
    void ctrlKeepsAFolderAsAutoSelectPicksElsewhere();
};

// Swift picks before the field's focus changes: the active layer drags.
void MoveBarPressTests::aTypedValueThenAPressDragsTheActiveLayer()
{
    Canvas shown;
    EditorSession &session = shown.session;
    session.insert(filled(60, 60, qRgba(0, 0, 255, 255), "Blue"), QPointF(320, 20));
    const QUuid blue = session.activeLayerID().value();
    session.selectLayer(shown.red);
    session.setTransformAutoSelect(true);
    shown.window.resize(400, 350);
    TransformInspector bar(session, &shown.window);
    bar.setGeometry(0, 300, 400, 42);
    bar.show();
    QLineEdit *x = bar.findChild<QLineEdit *>(QStringLiteral("transformX"));
    QVERIFY(x);
    x->setFocus();
    QTRY_VERIFY(x->hasFocus());
    x->selectAll();
    QTest::keyClicks(x, "160");
    QVERIFY(session.transformEdit().value().fromFields);
    const int steps = session.history.undoCount();
    // A real press over Blue: focus moves, then the canvas picks.
    QWindow *window = shown.window.windowHandle();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, QPoint(330, 30));
    QTest::mouseMove(window, QPoint(337, 30));
    QTest::mouseMove(window, QPoint(343, 30));
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, QPoint(343, 30));
    QCoreApplication::processEvents();
    QVERIFY(!x->hasFocus());
    QCOMPARE(session.activeLayerID(), std::optional(shown.red));
    QCOMPARE(layerWith(session, blue).transform.origin, QPointF(290, -10));
    QCOMPARE(shown.origin(), QPointF(173, 100));
    // Two steps: the typed value, then the drag.
    QCOMPARE(session.history.undoCount(), steps + 2);
    session.undo();
    QCOMPARE(shown.origin(), QPointF(160, 100));
}

void MoveBarPressTests::ctrlKeepsAFolderAsAutoSelectPicksElsewhere()
{
    Canvas shown;
    EditorSession &session = shown.session;
    session.insert(filled(60, 60, qRgba(0, 0, 255, 255), "Blue"), QPointF(320, 20));
    const QUuid blue = session.activeLayerID().value();
    session.selectLayers({shown.red}, shown.red);
    session.groupSelectedLayers();
    const QUuid folder = session.activeLayerID().value();
    QVERIFY(folder != shown.red && session.transformsAsGroup());
    session.setTransformAutoSelect(true);
    // Over Blue, past the folder's box: Ctrl drags the folder.
    shown.drag(QPointF(330, 30), QPointF(343, 30), Qt::ControlModifier);
    QCOMPARE(session.activeLayerID(), std::optional(folder));
    QCOMPARE(shown.origin(), QPointF(163, 100));
    QCOMPARE(layerWith(session, blue).transform.origin, QPointF(290, -10));
    // Without Ctrl, Auto Select picks Blue.
    shown.drag(QPointF(330, 30), QPointF(343, 30));
    QCOMPARE(session.activeLayerID(), std::optional(blue));
}

QTEST_MAIN(MoveBarPressTests)
#include "MoveBarPressTests.moc"
