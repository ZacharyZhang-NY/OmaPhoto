#include "SessionFixtures.h"
#include <QtTest>

// LayerFlip: mirrored placements and the session's flips.
namespace {
QUuid insertImage(EditorSession &session, int width, int height, QPointF center)
{
    session.insert(ImportedImage(QImage(width, height, QImage::Format_RGBA8888_Premultiplied), QImage(), "Image"), center);
    return session.activeLayerID().value();
}
}

class LayerFlipTests : public QObject {
    Q_OBJECT
private slots:
    void mirroredCrossesTheLineAndTurnsTheAngle();
    void aLayerFlipsAboutItsOwnMiddleAsOneStep();
    void aSelectionAndAFolderFlipAboutTheirBox();
    void aLinkedMaskFollowsAndAnUnlinkedOneStays();
    void flipsAreRefusedWithoutATransformableLayer();
    void theCanvasFlipsEveryLayerMaskAndTheSelection();
    void aCanvasFlipEndsACropAndWaitsItsTurn();
};

void LayerFlipTests::mirroredCrossesTheLineAndTurnsTheAngle()
{
    const LayerTransform placed{.origin = {10, 20}, .size = {30, 10}, .rotation = 15};
    const LayerTransform across = placed.mirrored(true, 100);
    QCOMPARE(across.origin, QPointF(160, 20));
    QCOMPARE(across.size, placed.size);
    QCOMPARE(across.rotation, -15.0);
    QVERIFY(across.flipX && !across.flipY);
    const LayerTransform over = placed.mirrored(false, 50);
    QCOMPARE(over.origin, QPointF(10, 70));
    QCOMPARE(over.rotation, -15.0);
    QVERIFY(!over.flipX && over.flipY);
    // Twice across the same line is the placement itself.
    QCOMPARE(across.mirrored(true, 100), placed);
    QCOMPARE(over.mirrored(false, 50), placed);
}

void LayerFlipTests::aLayerFlipsAboutItsOwnMiddleAsOneStep()
{
    EditorSession session;
    session.createDocument(100, 100);
    const QUuid id = insertImage(session, 30, 10, {25, 25});
    rewrite(session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform.rotation = 15; });
    const LayerTransform before = layerWith(session, id).transform;
    const int steps = session.history.undoCount();
    session.flipLayers(true);
    LayerTransform flipped = layerWith(session, id).transform;
    QCOMPARE(flipped.origin, before.origin);
    QCOMPARE(flipped.rotation, -15.0);
    QVERIFY(flipped.flipX);
    QCOMPARE(session.history.undoCount(), steps + 1);
    QCOMPARE(session.history.undoName(), QString("Flip Horizontal"));
    session.flipLayers(false);
    flipped = layerWith(session, id).transform;
    QCOMPARE(flipped.origin, before.origin);
    QCOMPARE(flipped.rotation, 15.0);
    QVERIFY(flipped.flipX && flipped.flipY);
    QCOMPARE(session.history.undoName(), QString("Flip Vertical"));
    session.undo();
    session.undo();
    QCOMPARE(layerWith(session, id).transform, before);
    // An open transform commits first, then the flip lands.
    session.beginTransform();
    LayerTransform moved = before;
    moved.origin += QPointF(5, 0);
    session.previewTransform(moved);
    session.flipLayers(true);
    QCOMPARE(layerWith(session, id).transform.origin, moved.origin);
    QVERIFY(layerWith(session, id).transform.flipX);
    QVERIFY(!session.transformEdit().has_value());
}

void LayerFlipTests::aSelectionAndAFolderFlipAboutTheirBox()
{
    EditorSession session;
    session.createDocument(100, 100);
    const QUuid left = insertImage(session, 10, 10, {5, 5});
    const QUuid right = insertImage(session, 10, 10, {95, 45});
    session.selectLayers({left, right}, right);
    session.flipLayers(true);
    // The box spans 0 to 100: the two swap sides.
    QCOMPARE(layerWith(session, left).transform.origin, QPointF(90, 0));
    QCOMPARE(layerWith(session, right).transform.origin, QPointF(0, 40));
    QVERIFY(layerWith(session, left).transform.flipX && layerWith(session, right).transform.flipX);
    QCOMPARE(session.history.undoName(), QString("Flip Horizontal"));
    session.undo();
    QCOMPARE(layerWith(session, left).transform.origin, QPointF(0, 0));
    // A folder flips its contents about the box around them.
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    QVERIFY(session.placeLayer(left, folder) && session.placeLayer(right, folder));
    session.selectLayer(folder);
    session.flipLayers(false);
    QCOMPARE(layerWith(session, left).transform.origin, QPointF(0, 40));
    QCOMPARE(layerWith(session, right).transform.origin, QPointF(90, 0));
    QVERIFY(layerWith(session, left).transform.flipY && !layerWith(session, left).transform.flipX);
    QCOMPARE(layerWith(session, folder).transform.flipY, false);
}

void LayerFlipTests::aLinkedMaskFollowsAndAnUnlinkedOneStays()
{
    EditorSession session;
    session.createDocument(100, 100);
    const QUuid linked = insertImage(session, 30, 10, {25, 25});
    const QUuid unlinked = insertImage(session, 30, 10, {75, 25});
    const LayerTransform placed{.origin = {12, 22}, .size = {10, 10}};
    rewrite(session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, linked, coverage());
        record(snapshot, linked).maskPlacement = placed;
        setMask(snapshot, unlinked, coverage());
        record(snapshot, unlinked).maskLinked = false;
    });
    session.selectLayer(linked);
    session.flipLayers(true);
    // The linked mask crosses the layer's middle, turned over.
    const LayerTransform followed = layerWith(session, linked).mask.value().placement.value();
    QCOMPARE(followed.center(), QPointF(33, 27));
    QCOMPARE(followed.size, QSizeF(10, 10));
    QCOMPARE(followed.unitToDocument().map(QPointF(0, 0)), QPointF(38, 22));
    QCOMPARE(followed.unitToDocument().map(QPointF(1, 1)), QPointF(28, 32));
    QCOMPARE(followed.unitToDocument().map(QPointF(1, 0)), QPointF(28, 22));
    session.selectLayer(unlinked);
    const LayerTransform before = layerWith(session, unlinked).transform;
    session.flipLayers(true);
    QVERIFY(layerWith(session, unlinked).transform.flipX);
    QCOMPARE(layerWith(session, unlinked).mask.value().placement.value(), before);
}

void LayerFlipTests::flipsAreRefusedWithoutATransformableLayer()
{
    EditorSession session;
    session.flipLayers(true);
    session.createDocument(100, 100);
    session.addBlankLayer();
    QVERIFY(!session.canTransform());
    int steps = session.history.undoCount();
    session.flipLayers(true);
    QCOMPARE(session.history.undoCount(), steps);
    const QUuid id = insertImage(session, 10, 10, {5, 5});
    session.toggleLayerVisibility(id);
    steps = session.history.undoCount();
    session.flipLayers(false);
    QCOMPARE(session.history.undoCount(), steps);
    QVERIFY(!layerWith(session, id).transform.flipY);
    session.toggleLayerVisibility(id);
    session.setIsProjectBusy(true);
    session.flipLayers(false);
    QVERIFY(!layerWith(session, id).transform.flipY);
}

void LayerFlipTests::theCanvasFlipsEveryLayerMaskAndTheSelection()
{
    EditorSession session;
    session.createDocument(100, 50);
    const QUuid id = insertImage(session, 20, 10, {20, 10});
    const LayerTransform placed{.origin = {12, 22}, .size = {10, 10}};
    rewrite(session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, id, coverage());
        record(snapshot, id).maskPlacement = placed;
    });
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    const LayerTransform folderBefore = layerWith(session, folder).transform;
    QPainterPath outline;
    outline.addRect(QRectF(10, 5, 20, 10));
    session.applySelection(outline, SelectionMode::replace, QStringLiteral("Select"));
    const CanvasDocument before = session.document().value();
    const int steps = session.history.undoCount();
    // Across the middle: every layer, a placed mask, the outline.
    session.flipCanvas(true);
    QCOMPARE(layerWith(session, id).transform.origin, QPointF(70, 5));
    QVERIFY(layerWith(session, id).transform.flipX);
    QCOMPARE(layerWith(session, folder).transform, folderBefore.mirrored(true, 50));
    const LayerTransform mirrored = layerWith(session, id).mask.value().placement.value();
    QCOMPARE(mirrored.origin, QPointF(78, 22));
    QVERIFY(mirrored.flipX);
    QCOMPARE(session.selection().value().path.boundingRect(), QRectF(70, 5, 20, 10));
    QCOMPARE(session.history.undoCount(), steps + 1);
    QCOMPARE(session.history.undoName(), QString("Flip Canvas Horizontal"));
    session.flipCanvas(false);
    QCOMPARE(layerWith(session, id).transform.origin, QPointF(70, 35));
    QVERIFY(layerWith(session, id).transform.flipY);
    QCOMPARE(layerWith(session, id).mask.value().placement.value().origin, QPointF(78, 18));
    QCOMPARE(session.selection().value().path.boundingRect(), QRectF(70, 35, 20, 10));
    QCOMPARE(session.history.undoName(), QString("Flip Canvas Vertical"));
    session.undo();
    session.undo();
    QVERIFY(session.document().value() == before);
}

void LayerFlipTests::aCanvasFlipEndsACropAndWaitsItsTurn()
{
    EditorSession session;
    session.flipCanvas(true);
    session.createDocument(100, 50);
    const QUuid id = insertImage(session, 20, 10, {20, 10});
    // A drawn frame goes, then the canvas flips.
    session.selectTool(NavigationTool::crop);
    session.setCropRect(QRectF(0, 0, 50, 50));
    session.flipCanvas(true);
    QCOMPARE(session.cropRect(), std::nullopt);
    QCOMPARE(layerWith(session, id).transform.origin, QPointF(70, 5));
    // An open transform commits first, then the flip lands.
    session.selectTool(NavigationTool::move);
    session.beginTransform();
    LayerTransform moved = layerWith(session, id).transform;
    moved.origin += QPointF(5, 0);
    session.previewTransform(moved);
    session.flipCanvas(true);
    QVERIFY(!session.transformEdit().has_value());
    QCOMPARE(layerWith(session, id).transform.origin, QPointF(5, 5));
    // Busy, the canvas stays.
    const int steps = session.history.undoCount();
    session.setIsProjectBusy(true);
    session.flipCanvas(false);
    QCOMPARE(session.history.undoCount(), steps);
    QVERIFY(!layerWith(session, id).transform.flipY);
}

QTEST_GUILESS_MAIN(LayerFlipTests)
#include "LayerFlipTests.moc"
