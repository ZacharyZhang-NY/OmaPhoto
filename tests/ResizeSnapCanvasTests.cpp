#include "MovePressFixtures.h"

// Swift's resize snapping on the canvas: handles, Ctrl, Shift.
class ResizeSnapCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void resizedEdgesSnapUnlessCtrl();
    void altResizesFromTheCentreAndSnaps();
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

// Swift's "resizing from the center with Option snaps too".
void ResizeSnapCanvasTests::altResizesFromTheCentreAndSnaps()
{
    Canvas shown;
    EditorSession &session = shown.session;
    session.setLocksTransformRatio(false);
    // Right edge to 396, left to 4: 396 snaps.
    shown.press(QPointF(250, 150), Qt::AltModifier);
    shown.move(QPointF(396, 150), Qt::AltModifier);
    QCOMPARE(session.transformEdit().value().draft, (LayerTransform{.origin = {0, 100}, .size = {400, 100}}));
    QCOMPARE(session.snapGuides, (SnapGuides{{400}, {}}));
    shown.release(QPointF(396, 150), Qt::AltModifier);
    QCOMPARE(layerWith(session, shown.red).transform, (LayerTransform{.origin = {0, 100}, .size = {400, 100}}));
    QCOMPARE(int(session.document().value().layers.size()), 1);
}

QTEST_MAIN(ResizeSnapCanvasTests)
#include "ResizeSnapCanvasTests.moc"
