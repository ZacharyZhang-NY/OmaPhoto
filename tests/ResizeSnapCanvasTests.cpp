#include "CanvasFixtures.h"

// Swift's resize snapping on the canvas: handles, Ctrl, Shift.
namespace {
// The document fills the canvas: view points are pixels.
struct Canvas : Shown {
    QUuid red;
    Canvas() : Shown(QSize(400, 300), QSize(400, 300))
    {
        settle();
        session.zoom(1);
        session.insert(filled(100, 100, qRgba(255, 0, 0, 255), "Red"));
        red = session.activeLayerID().value();
        session.selectTool(NavigationTool::move);
        canvas->synchronizeDisplay();
        if (session.viewport.viewPoint(QPointF(0, 0), documentSize()) != QPointF(0, 0))
            throw std::runtime_error("the document does not fill the canvas");
    }
    QPointF origin() const { return layerWith(session, red).transform.origin; }
    void press(QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier) { QTest::mousePress(canvas, Qt::LeftButton, modifiers, at.toPoint()); }
    void move(QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QMouseEvent event(QEvent::MouseMove, to, to, canvas->mapToGlobal(to.toPoint()), Qt::NoButton, Qt::LeftButton, modifiers);
        QApplication::sendEvent(canvas, &event);
    }
    void release(QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier) { QTest::mouseRelease(canvas, Qt::LeftButton, modifiers, at.toPoint()); }
    void drag(QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        press(from, modifiers);
        move((from + to) / 2, modifiers);
        move(to, modifiers);
        release(to, modifiers);
    }
    void click(QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        press(at, modifiers);
        release(at, modifiers);
    }
};
}

class ResizeSnapCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void resizedEdgesSnapUnlessCtrl();
};

void ResizeSnapCanvasTests::resizedEdgesSnapUnlessCtrl()
{
    Canvas shown;
    EditorSession &session = shown.session;
    session.setLocksTransformRatio(false);
    // The right edge, from 250 to four short of 400.
    shown.press(QPointF(250, 150));
    shown.move(QPointF(396, 150));
    QCOMPARE(session.transformEdit().value().draft, (LayerTransform{.origin = {150, 100}, .size = {250, 100}}));
    QCOMPARE(session.snapGuides, (SnapGuides{{400}, {}}));
    // Ctrl mid-drag frees it.
    shown.move(QPointF(396, 150), Qt::ControlModifier);
    QCOMPARE(session.transformEdit().value().draft.size, QSizeF(246, 100));
    shown.release(QPointF(396, 150), Qt::ControlModifier);
    QCOMPARE(layerWith(session, shown.red).transform.size, QSizeF(246, 100));
    // Snapping off, nothing snaps.
    session.undo();
    session.setSnappingEnabled(false);
    shown.drag(QPointF(250, 150), QPointF(396, 150));
    QCOMPARE(layerWith(session, shown.red).transform.size, QSizeF(246, 100));
    QCOMPARE(session.snapGuides, SnapGuides{});
    // Locked, Shift frees the lock: a corner snaps both edges.
    session.undo();
    session.setSnappingEnabled(true);
    session.setLocksTransformRatio(true);
    shown.press(QPointF(250, 200));
    shown.move(QPointF(396, 296), Qt::ShiftModifier);
    QCOMPARE(session.transformEdit().value().draft, (LayerTransform{.origin = {150, 100}, .size = {250, 200}}));
    QCOMPARE(session.snapGuides, (SnapGuides{{400}, {300}}));
    // Kept proportional, only the nearer edge snaps.
    shown.move(QPointF(397, 349));
    QCOMPARE(session.transformEdit().value().draft.size, QSizeF(250, 250));
    QCOMPARE(session.snapGuides, (SnapGuides{{400}, {}}));
    shown.release(QPointF(397, 349));
}

QTEST_MAIN(ResizeSnapCanvasTests)
#include "ResizeSnapCanvasTests.moc"
