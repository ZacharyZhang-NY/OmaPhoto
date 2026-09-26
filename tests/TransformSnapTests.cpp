#include "SessionFixtures.h"
#include <QtTest>

// What a move snaps to; the edits drags leave behind.
namespace {
QUuid insertImage(EditorSession &session, int width, int height, QPointF center)
{
    session.insert(ImportedImage(QImage(width, height, QImage::Format_RGBA8888_Premultiplied), QImage(), "Image"), center);
    return session.activeLayerID().value();
}
}

class TransformSnapTests : public QObject {
    Q_OBJECT
private slots:
    void targetsAreTheCanvasAndTheOtherVisibleLayersEdgesAndMiddles();
    void snappedMoveNudgesWithinToleranceAndKeepsTheGuides();
    void dragsEditsAreNotPersistentAndEndsClearTheGuides();
};

void TransformSnapTests::targetsAreTheCanvasAndTheOtherVisibleLayersEdgesAndMiddles()
{
    EditorSession session;
    QCOMPARE(session.transformSnapTargets({}), SnapGuides{});
    session.createDocument(100, 80);
    QCOMPARE(session.transformSnapTargets({}), (SnapGuides{{0, 50, 100}, {0, 40, 80}}));
    const QUuid a = insertImage(session, 20, 10, {20, 15});
    const QUuid b = insertImage(session, 10, 10, {70, 60});
    QCOMPARE(session.transformSnapTargets({}), (SnapGuides{{0, 50, 100, 10, 20, 30, 65, 70, 75}, {0, 40, 80, 10, 15, 20, 55, 60, 65}}));
    // The layers being moved are no targets; hidden ones neither.
    QCOMPARE(session.transformSnapTargets({a}), (SnapGuides{{0, 50, 100, 65, 70, 75}, {0, 40, 80, 55, 60, 65}}));
    session.toggleLayerVisibility(b);
    QCOMPARE(session.transformSnapTargets({a}), (SnapGuides{{0, 50, 100}, {0, 40, 80}}));
    session.toggleLayerVisibility(b);
    // A turned layer offers the box around its corners, rounded.
    rewrite(session, [&](ProjectSnapshot &snapshot) { record(snapshot, b).transform = {.origin = {60, 50}, .size = {20, 10}, .rotation = 90}; });
    QCOMPARE(session.transformSnapTargets({a}), (SnapGuides{{0, 50, 100, 65, 70, 75}, {0, 40, 80, 45, 55, 65}}));
    rewrite(session, [&](ProjectSnapshot &snapshot) { record(snapshot, b).transform = {.origin = {60, 50}, .size = {20, 10}, .rotation = 30}; });
    QCOMPARE(session.transformSnapTargets({a}), (SnapGuides{{0, 50, 100, 59, 70, 81}, {0, 40, 80, 46, 55, 64}}));
    // A pending edit's placement counts, as the canvas shows it.
    session.selectLayer(b);
    session.beginTransform();
    session.previewTransform({.origin = {50, 50}, .size = {10, 10}});
    QCOMPARE(session.transformSnapTargets({a}), (SnapGuides{{0, 50, 100, 50, 55, 60}, {0, 40, 80, 50, 55, 60}}));
    session.cancelTransform();
    // A blank layer has no pixels to line up with.
    session.addBlankLayer();
    QCOMPARE(session.transformSnapTargets({a, b}), (SnapGuides{{0, 50, 100}, {0, 40, 80}}));
}

void TransformSnapTests::snappedMoveNudgesWithinToleranceAndKeepsTheGuides()
{
    EditorSession session;
    session.createDocument(100, 80);
    const QUuid a = insertImage(session, 20, 10, {20, 15});
    // Middle three off, top two off: both snap.
    const LayerTransform draft{.origin = {43, 38}, .size = {20, 10}};
    QCOMPARE(session.snappedMove(draft, {a}, 5), (LayerTransform{.origin = {40, 40}, .size = {20, 10}}));
    QCOMPARE(session.snapGuides, (SnapGuides{{50}, {40}}));
    // Past the tolerance the draft comes back untouched.
    QCOMPARE(session.snappedMove(draft, {a}, 1), draft);
    QCOMPARE(session.snapGuides, SnapGuides{});
    // The nearest of left, middle and right wins, one axis.
    const LayerTransform edge{.origin = {98, 60}, .size = {20, 10}};
    QCOMPARE(session.snappedMove(edge, {a}, 5), (LayerTransform{.origin = {100, 60}, .size = {20, 10}}));
    QCOMPARE(session.snapGuides, (SnapGuides{{100}, {}}));
    // A turned draft snaps by the box around its corners.
    const LayerTransform turned{.origin = {42, 40}, .size = {20, 10}, .rotation = 90};
    QCOMPARE(session.snappedMove(turned, {a}, 3), (LayerTransform{.origin = {40, 40}, .size = {20, 10}, .rotation = 90}));
    QCOMPARE(session.snapGuides, (SnapGuides{{50}, {}}));
    // Another layer's edge is a target unless it moves too.
    const QUuid b = insertImage(session, 10, 10, {70, 60});
    const LayerTransform near{.origin = {77, 20}, .size = {20, 10}};
    QCOMPARE(session.snappedMove(near, {a}, 2).origin, QPointF(75, 20));
    QCOMPARE(session.snappedMove(near, {a, b}, 2).origin, QPointF(77, 20));
}

void TransformSnapTests::dragsEditsAreNotPersistentAndEndsClearTheGuides()
{
    EditorSession session;
    session.createDocument(100, 80);
    insertImage(session, 20, 10, {20, 15});
    session.beginTransform();
    QVERIFY(session.transformEdit().value().persistent);
    session.snapGuides = {{50}, {}};
    session.commitTransform();
    QCOMPARE(session.snapGuides, SnapGuides{});
    session.beginTransform(false);
    QVERIFY(!session.transformEdit().value().persistent);
    session.snapGuides = {{}, {40}};
    session.cancelTransform();
    QCOMPARE(session.snapGuides, SnapGuides{});
    // Nothing pending: the ends still clear what a drag left.
    session.snapGuides = {{50}, {}};
    session.cancelTransform();
    QCOMPARE(session.snapGuides, SnapGuides{});
    session.snapGuides = {{50}, {}};
    session.commitTransform();
    QCOMPARE(session.snapGuides, SnapGuides{});
    // The Move settings start as Swift's and change on request.
    QVERIFY(!session.transformAutoSelect() && session.showsTransformControls() && session.locksTransformRatio());
    session.setTransformAutoSelect(true);
    session.setShowsTransformControls(false);
    session.setLocksTransformRatio(false);
    QVERIFY(session.transformAutoSelect() && !session.showsTransformControls() && !session.locksTransformRatio());
}

QTEST_GUILESS_MAIN(TransformSnapTests)
#include "TransformSnapTests.moc"
