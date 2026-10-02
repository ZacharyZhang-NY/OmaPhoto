#include "SessionFixtures.h"
#include <QtTest>

// Swift's ResizeSnapTests: a resized layer's edges snap, as moves do.
namespace {
struct Resized {
    EditorSession session;
    QUuid layer;

    // 300 by 200: the layer at 10; another at 150.
    Resized()
    {
        session.createDocument(300, 200);
        place(QPointF(150, 150), QSizeF(40, 40));
        place(QPointF(10, 10), QSizeF(100, 100));
        layer = session.activeLayerID().value();
    }
    void place(QPointF origin, QSizeF size)
    {
        session.insert(ImportedImage(QImage(20, 20, QImage::Format_RGBA8888_Premultiplied), QImage(), "Layer"));
        session.beginTransform();
        session.previewTransform(LayerTransform{.origin = origin, .size = size});
        session.commitTransform();
    }
    LayerTransform resize(int handle, QPointF start, QPointF point, bool lockRatio)
    {
        const TransformDrag drag{layerWith(session, layer).transform, start, {TransformDrag::Kind::resize, handle}, std::nullopt};
        const QPointF snapped = session.snappedResizePoint(point, drag, lockRatio, {layer}, 5, [&](QPointF to) {
            return drag.updated(to, lockRatio, false);
        });
        return drag.updated(snapped, lockRatio, false).rounded();
    }
};
}

class ResizeSnapTests : public QObject {
    Q_OBJECT
private slots:
    void aSideHandleSnapsItsEdge();
    void aProportionalCornerSnapsItsNearerEdgeAndKeepsTheRatio();
    void aTurnedLayerDoesntSnap();
    void eachEdgeSnapsOnItsOwn();
    void theDraggedSideAloneSnaps();
    void aLongDragSnapsTheEdgeItCarries();
    void aTieTakesTheLowEdge();
    void snappingOffOrAnotherDragLeavesThePoint();
};

void ResizeSnapTests::aSideHandleSnapsItsEdge()
{
    Resized resized;
    QCOMPARE(LayerTransform::handles[3], QPointF(1, 0.5));
    // The right edge, three short of the other layer's left.
    const LayerTransform result = resized.resize(3, QPointF(110, 60), QPointF(147, 60), false);
    QCOMPARE(result.origin.x() + result.size.width(), 150.0);
    QCOMPARE(result.size.height(), 100.0);
    QCOMPARE(resized.session.snapGuides, (SnapGuides{{150}, {}}));
    // Out of reach, it does not.
    const LayerTransform free = resized.resize(3, QPointF(110, 60), QPointF(130, 60), false);
    QCOMPARE(free.origin.x() + free.size.width(), 130.0);
    QCOMPARE(resized.session.snapGuides, SnapGuides{});
    // The left handle snaps the left edge, to the canvas.
    const LayerTransform left = resized.resize(7, QPointF(10, 60), QPointF(3, 60), false);
    QCOMPARE(left.origin.x(), 0.0);
    QCOMPARE(left.origin.x() + left.size.width(), 110.0);
}

void ResizeSnapTests::aProportionalCornerSnapsItsNearerEdgeAndKeepsTheRatio()
{
    Resized resized;
    // Bottom right toward 146, 148: the bottom edge snaps.
    const LayerTransform result = resized.resize(4, QPointF(110, 110), QPointF(146, 148), true);
    QCOMPARE(result.origin.y() + result.size.height(), 150.0);
    QVERIFY(std::abs(result.size.width() - result.size.height()) <= 1);
    // A tie goes to the right edge, the first.
    QCOMPARE(resized.session.snapGuides, (SnapGuides{{150}, {}}));
    // A top at 148 lies nearer the bottom: it snaps.
    resized.place(QPointF(260, 148), QSizeF(10, 10));
    resized.session.selectLayer(resized.layer);
    const LayerTransform bottom = resized.resize(4, QPointF(110, 110), QPointF(147, 147), true);
    QCOMPARE(bottom, (LayerTransform{.origin = {10, 10}, .size = {138, 138}}));
    QCOMPARE(resized.session.snapGuides, (SnapGuides{{}, {148}}));
    // Free, both edges snap, each to its nearest.
    const LayerTransform free = resized.resize(4, QPointF(110, 110), QPointF(147, 147), false);
    QCOMPARE(free, (LayerTransform{.origin = {10, 10}, .size = {140, 138}}));
}

void ResizeSnapTests::aTurnedLayerDoesntSnap()
{
    Resized resized;
    LayerTransform turned = layerWith(resized.session, resized.layer).transform;
    turned.rotation = 20;
    const TransformDrag drag{turned, QPointF(110, 60), {TransformDrag::Kind::resize, 3}, std::nullopt};
    // Wherever the handle goes, the pointer stays put.
    for (double x = 100; x <= 200; x += 0.5) {
        resized.session.snapGuides = SnapGuides{{1}, {2}};
        const QPointF point(x, 60);
        QCOMPARE(resized.session.snappedResizePoint(point, drag, false, {resized.layer}, 5, [&](QPointF to) { return drag.updated(to, false, false); }),
                 point);
        QCOMPARE(resized.session.snapGuides, SnapGuides{});
    }
}

void ResizeSnapTests::eachEdgeSnapsOnItsOwn()
{
    Resized resized;
    // Free, a corner snaps both edges, each to its line.
    const LayerTransform result = resized.resize(4, QPointF(110, 110), QPointF(146, 147), false);
    QCOMPARE(result, (LayerTransform{.origin = {10, 10}, .size = {140, 140}}));
    QCOMPARE(resized.session.snapGuides, (SnapGuides{{150}, {150}}));
    // The top left corner moves the top and left edges.
    const LayerTransform corner = resized.resize(0, QPointF(10, 10), QPointF(2, 4), false);
    QCOMPARE(corner, (LayerTransform{.origin = {0, 0}, .size = {110, 110}}));
    // An edge no step moves keeps the pointer; guide shown.
    const TransformDrag drag{layerWith(resized.session, resized.layer).transform, QPointF(110, 60), {TransformDrag::Kind::resize, 3}, std::nullopt};
    const QPointF still = resized.session.snappedResizePoint(QPointF(147, 60), drag, false, {resized.layer}, 5, [&](QPointF) {
        return LayerTransform{.origin = {10, 10}, .size = {138, 100}};
    });
    QCOMPARE(still, QPointF(147, 60));
    QCOMPARE(resized.session.snapGuides, (SnapGuides{{150}, {}}));
}

// The dragged side tells its edge, wherever the press began.
void ResizeSnapTests::theDraggedSideAloneSnaps()
{
    Resized resized;
    // Pressed left of the layer, the right edge still moves.
    const LayerTransform far = resized.resize(3, QPointF(-50, 60), QPointF(-13, 60), false);
    QCOMPARE(far.origin.x() + far.size.width(), 150.0);
    QCOMPARE(far.origin.x(), 10.0);
    // A top on a line: side drags never snap it.
    resized.session.beginTransform();
    resized.session.previewTransform(LayerTransform{.origin = {10, 98}, .size = {100, 60}});
    resized.session.commitTransform();
    resized.resize(3, QPointF(110, 128), QPointF(147, 128), false);
    QCOMPARE(resized.session.snapGuides, (SnapGuides{{150}, {}}));
    // Nor a bottom handle the left edge, near the canvas's.
    resized.session.beginTransform();
    resized.session.previewTransform(LayerTransform{.origin = {2, 10}, .size = {100, 100}});
    resized.session.commitTransform();
    const LayerTransform down = resized.resize(5, QPointF(52, 110), QPointF(52, 148), false);
    QCOMPARE(down, (LayerTransform{.origin = {2, 10}, .size = {100, 140}}));
    QCOMPARE(resized.session.snapGuides, (SnapGuides{{}, {150}}));
}

// Dragged past the anchor, the moved edge is the box's left.
void ResizeSnapTests::aLongDragSnapsTheEdgeItCarries()
{
    Resized resized;
    const LayerTransform flipped = resized.resize(3, QPointF(110, 60), QPointF(-3, 60), false);
    QCOMPARE(flipped.origin.x(), 0.0);
    QCOMPARE(flipped.size.width(), 10.0);
    QVERIFY(flipped.flipX);
    QCOMPARE(resized.session.snapGuides, (SnapGuides{{0}, {}}));
}

// Half a pixel from the anchor: a one-pixel box, the pointer mid-way.
void ResizeSnapTests::aTieTakesTheLowEdge()
{
    Resized resized;
    resized.place(QPointF(13, 150), QSizeF(20, 20));
    resized.session.selectLayer(resized.layer);
    const TransformDrag drag{layerWith(resized.session, resized.layer).transform, QPointF(110, 60), {TransformDrag::Kind::resize, 3}, std::nullopt};
    QCOMPARE(drag.updated(QPointF(10.5, 60), false, false).size.width(), 1.0);
    // The anchor's edge, which no step moves: the pointer stays.
    QCOMPARE(resized.session.snappedResizePoint(QPointF(10.5, 60), drag, false, {resized.layer}, 5, [&](QPointF to) { return drag.updated(to, false, false); }),
             QPointF(10.5, 60));
    QCOMPARE(resized.session.snapGuides, (SnapGuides{{13}, {}}));
}

void ResizeSnapTests::snappingOffOrAnotherDragLeavesThePoint()
{
    Resized resized;
    resized.session.setSnappingEnabled(false);
    const LayerTransform result = resized.resize(3, QPointF(110, 60), QPointF(147, 60), false);
    QCOMPARE(result.origin.x() + result.size.width(), 147.0);
    resized.session.setSnappingEnabled(true);
    const TransformDrag move{layerWith(resized.session, resized.layer).transform, QPointF(110, 60), {TransformDrag::Kind::move, 3}, std::nullopt};
    QCOMPARE(resized.session.snappedResizePoint(QPointF(147, 60), move, false, {resized.layer}, 5, [&](QPointF to) { return move.updated(to, false, false); }),
             QPointF(147, 60));
}

QTEST_GUILESS_MAIN(ResizeSnapTests)
#include "ResizeSnapTests.moc"
