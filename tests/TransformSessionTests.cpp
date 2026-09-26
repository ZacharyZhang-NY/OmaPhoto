#include "Document/EditorSession.h"
#include <QtTest>

// Transform edits in the session beyond Swift's own cases.
namespace {
QUuid insertImage(EditorSession &session, int width, int height, QPointF center)
{
    session.insert(ImportedImage(QImage(width, height, QImage::Format_RGBA8888_Premultiplied), QImage(), "Image"), center);
    return session.activeLayerID().value();
}

ImageLayer layerWith(const EditorSession &session, QUuid id)
{
    for (const ImageLayer &layer : session.document().value().layers) {
        if (layer.id == id)
            return layer;
    }
    throw std::runtime_error("no such layer");
}

QSet<QUuid> ids(const std::vector<ImageLayer> &layers)
{
    QSet<QUuid> result;
    for (const ImageLayer &layer : layers)
        result.insert(layer.id);
    return result;
}
}

class TransformSessionTests : public QObject {
    Q_OBJECT
private slots:
    void severalLayersMoveInOneBox();
    void aFolderCarriesItsVisiblePixels();
    void aFolderReachesSixtyThreeLevelsDown();
    void aGroupMoveKeepsEachTurnFlipAndSampling();
    void aBoxSqueezedTooFarLeavesThatLayer();
    void theBoxHoldsEveryCornerOfATurnedLayer();
    void aUnitLayerStillGetsAWholeBox();
    void nudgingIsOneStepOrPartOfAnOpenEdit();
    void anotherLayerToolOrCanvasCommits();
    void anOpenTransformClosesTheGates();
    void handlesFollowTheDraftOrTheBox();
};

void TransformSessionTests::severalLayersMoveInOneBox()
{
    EditorSession session;
    session.createDocument(400, 300);
    // The lower right image comes first in the list.
    const QUuid right = insertImage(session, 20, 60, {300, 200});
    const QUuid left = insertImage(session, 40, 20, {50, 50});
    const QUuid apart = insertImage(session, 10, 10, {200, 20});
    QVERIFY(!session.transformsAsGroup());
    QVERIFY(session.groupTransformMembers().empty());
    QVERIFY(!session.groupTransformBox().has_value());
    session.selectLayers({left, right}, left);
    QVERIFY(session.transformsAsGroup() && session.canTransform());
    QCOMPARE(ids(session.groupTransformMembers()), (QSet<QUuid>{left, right}));
    // From the left image's corner to the right one's.
    const LayerTransform box{.origin = {30, 40}, .size = {280, 190}};
    QCOMPARE(session.groupTransformBox().value(), box);
    QCOMPARE(session.transformPixelSize(), std::optional(QSizeF(280, 190)));
    session.selectTool(NavigationTool::hand);
    session.selectLayers({left, right}, left);
    session.beginTransform();
    QCOMPARE(session.tool(), NavigationTool::move);
    const TransformEdit edit = session.transformEdit().value();
    QCOMPARE(edit.layerID, left);
    QCOMPARE(edit.draft, box);
    QCOMPARE(edit.group.value().box, box);
    QCOMPARE(edit.group.value().originals.value(left), layerWith(session, left).transform);
    QCOMPARE(edit.group.value().originals.value(right), layerWith(session, right).transform);
    QCOMPARE(int(edit.group.value().originals.size()), 2);
    LayerTransform moved = box;
    moved.origin += QPointF(10, -5);
    const int count = session.history.undoCount();
    session.previewTransform(moved);
    // The preview carries both; the document waits for the commit.
    QCOMPARE(session.displayedTransform(layerWith(session, left)).origin, QPointF(40, 35));
    QCOMPARE(session.displayedTransform(layerWith(session, right)).origin, QPointF(300, 165));
    QCOMPARE(session.pendingTransform(layerWith(session, apart)), std::nullopt);
    QCOMPARE(session.displayedTransform(layerWith(session, apart)), layerWith(session, apart).transform);
    QCOMPARE(layerWith(session, left).transform.origin, QPointF(30, 40));
    // The size shown is the box as it began.
    QCOMPARE(session.transformPixelSize(), std::optional(QSizeF(280, 190)));
    session.commitTransform();
    QVERIFY(!session.transformEdit().has_value());
    QCOMPARE(layerWith(session, left).transform.origin, QPointF(40, 35));
    QCOMPARE(layerWith(session, right).transform.origin, QPointF(300, 165));
    QCOMPARE(layerWith(session, apart).transform.origin, QPointF(195, 15));
    QCOMPARE(session.history.undoCount(), count + 1);
    QCOMPARE(session.history.undoName(), QString("Transform Layers"));
    session.undo();
    QCOMPARE(layerWith(session, left).transform.origin, QPointF(30, 40));
    QCOMPARE(layerWith(session, right).transform.origin, QPointF(290, 170));
}

void TransformSessionTests::aFolderCarriesItsVisiblePixels()
{
    EditorSession session;
    session.createDocument(400, 300);
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    const QUuid shown = insertImage(session, 40, 20, {50, 50});
    session.selectLayer(folder);
    session.addGroup();
    const QUuid nested = insertImage(session, 20, 20, {100, 100});
    session.selectLayer(folder);
    const QUuid hidden = insertImage(session, 30, 30, {300, 200});
    session.toggleLayerVisibility(hidden);
    session.selectLayer(folder);
    session.addBlankLayer();
    session.selectLayer(std::nullopt);
    const QUuid outside = insertImage(session, 10, 10, {350, 250});
    session.selectLayer(folder);
    // One selected folder: its shown pixels, however deep.
    QVERIFY(session.transformsAsGroup() && session.canTransform());
    QCOMPARE(ids(session.groupTransformMembers()), (QSet<QUuid>{shown, nested}));
    QCOMPARE(session.groupTransformBox().value(), (LayerTransform{.origin = {30, 40}, .size = {80, 70}}));
    // The handles sit on the box until an edit begins.
    QCOMPARE(session.editedTransform(layerWith(session, folder)), session.groupTransformBox().value());
    QCOMPARE(session.editedTransform(layerWith(session, outside)), layerWith(session, outside).transform);
    // Hidden, the folder shows nothing to move.
    session.toggleLayerVisibility(folder);
    QVERIFY(session.groupTransformMembers().empty());
    QVERIFY(!session.canTransform());
    QCOMPARE(session.editedTransform(layerWith(session, folder)), layerWith(session, folder).transform);
    session.beginTransform();
    QVERIFY(!session.transformEdit().has_value());
    QCOMPARE(session.transformPixelSize(), std::nullopt);
    // Inside a hidden folder a layer cannot transform either.
    session.selectLayer(shown);
    QVERIFY(!session.transformsAsGroup() && !session.canTransform());
    session.toggleLayerVisibility(folder);
    QVERIFY(session.canTransform());
}

void TransformSessionTests::aFolderReachesSixtyThreeLevelsDown()
{
    EditorSession session;
    session.createDocument(64, 64);
    // Each new folder goes inside the last: 64 deep.
    std::vector<QUuid> folders;
    for (int depth = 0; depth < 64; ++depth) {
        session.addGroup();
        folders.push_back(session.activeLayerID().value());
    }
    const QUuid image = insertImage(session, 8, 8, {32, 32});
    QCOMPARE(layerWith(session, image).parentID, std::optional(folders.back()));
    QVERIFY(session.document().value().effectiveVisibleIDs().contains(image));
    // The walk takes 64 steps, the layer itself among them.
    session.selectLayer(folders[1]);
    QCOMPARE(ids(session.groupTransformMembers()), QSet<QUuid>{image});
    QVERIFY(session.canTransform());
    session.selectLayer(folders[0]);
    QVERIFY(session.groupTransformMembers().empty());
    QVERIFY(!session.canTransform());
}

void TransformSessionTests::aGroupMoveKeepsEachTurnFlipAndSampling()
{
    EditorSession session;
    session.createDocument(400, 300);
    // Opposite flips, different turns and samplings.
    const auto placed = [&](QPointF center, double rotation, bool flipX, LayerSampling sampling) {
        const QUuid id = insertImage(session, 40, 20, center);
        session.beginTransform();
        LayerTransform value = session.transformEdit().value().draft;
        value.rotation = rotation;
        value.flipX = flipX;
        value.flipY = !flipX;
        value.sampling = sampling;
        session.previewTransform(value);
        session.commitTransform();
        return id;
    };
    const QUuid first = placed({100, 100}, 30, true, LayerSampling::nearest);
    const QUuid second = placed({250, 180}, -75, false, LayerSampling::smooth);
    const LayerTransform firstBefore = layerWith(session, first).transform, secondBefore = layerWith(session, second).transform;
    QCOMPARE(firstBefore.sampling, LayerSampling::nearest);
    QVERIFY(firstBefore.flipX && !firstBefore.flipY && !secondBefore.flipX && secondBefore.flipY);
    session.selectLayers({first, second}, second);
    session.beginTransform();
    QCOMPARE(session.transformEdit().value().group.value().originals.value(first), firstBefore);
    QCOMPARE(session.transformEdit().value().group.value().originals.value(second), secondBefore);
    session.nudgeLayer(5, 3);
    // The preview and the commit move origins and nothing else.
    LayerTransform firstAfter = firstBefore, secondAfter = secondBefore;
    firstAfter.origin += QPointF(5, 3);
    secondAfter.origin += QPointF(5, 3);
    QCOMPARE(session.displayedTransform(layerWith(session, first)), firstAfter);
    QCOMPARE(session.displayedTransform(layerWith(session, second)), secondAfter);
    session.commitTransform();
    QCOMPARE(layerWith(session, first).transform, firstAfter);
    QCOMPARE(layerWith(session, second).transform, secondAfter);
    // One layer's edit starts from all of its transform too.
    session.selectLayer(first);
    const int count = session.history.undoCount();
    session.beginTransform();
    QCOMPARE(session.transformEdit().value().draft, firstAfter);
    QVERIFY(!session.transformEdit().value().group.has_value());
    session.commitTransform();
    QCOMPARE(session.history.undoCount(), count);
}

void TransformSessionTests::aBoxSqueezedTooFarLeavesThatLayer()
{
    EditorSession session;
    session.createDocument(400, 300);
    const QUuid wide = insertImage(session, 200, 100, {100, 50});
    const QUuid thin = insertImage(session, 2, 100, {300, 50});
    session.selectLayers({wide, thin}, wide);
    session.beginTransform();
    LayerTransform squeezed = session.transformEdit().value().draft;
    QCOMPARE(squeezed, (LayerTransform{.origin = {0, 0}, .size = {301, 100}}));
    // Quartered, the thin layer would be half a pixel.
    squeezed.size.setWidth(squeezed.size.width() / 4);
    session.previewTransform(squeezed);
    // The pixels shown stay the box's as it began.
    QCOMPARE(session.transformPixelSize(), std::optional(QSizeF(301, 100)));
    session.commitTransform();
    QCOMPARE(layerWith(session, wide).transform.size, QSizeF(50, 100));
    QCOMPARE(layerWith(session, thin).transform, (LayerTransform{.origin = {299, 0}, .size = {2, 100}}));
}

void TransformSessionTests::theBoxHoldsEveryCornerOfATurnedLayer()
{
    EditorSession session;
    session.createDocument(200, 200);
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    insertImage(session, 40, 20, {100, 100});
    session.beginTransform();
    LayerTransform turned = session.transformEdit().value().draft;
    turned.rotation = 90;
    session.previewTransform(turned);
    session.commitTransform();
    session.selectLayer(folder);
    // A quarter turn: 20 by 40 about (100, 100).
    const LayerTransform box = session.groupTransformBox().value();
    QVERIFY2(std::abs(box.origin.x() - 90) < 1e-9 && std::abs(box.origin.y() - 80) < 1e-9, qPrintable(QString::number(box.origin.x())));
    QVERIFY2(std::abs(box.size.width() - 20) < 1e-9 && std::abs(box.size.height() - 40) < 1e-9, qPrintable(QString::number(box.size.width())));
    QCOMPARE(box.rotation, 0.0);
}

void TransformSessionTests::aUnitLayerStillGetsAWholeBox()
{
    EditorSession session;
    session.createDocument(64, 64);
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    const QUuid dot = insertImage(session, 1, 1, {10, 10});
    session.beginTransform();
    LayerTransform off = session.transformEdit().value().draft;
    off.origin = {1.3, 1.3};
    session.previewTransform(off);
    session.commitTransform();
    // In doubles these corners lie under one pixel apart.
    QVERIFY((1.3 + 0.5 + 0.5) - (1.3 + 0.5 - 0.5) < 1.0);
    session.selectLayer(folder);
    QCOMPARE(session.groupTransformBox().value().size, QSizeF(1, 1));
    session.beginTransform();
    QVERIFY(session.transformEdit().value().draft.isValid());
    QCOMPARE(layerWith(session, dot).transform.origin, QPointF(1.3, 1.3));
}

void TransformSessionTests::nudgingIsOneStepOrPartOfAnOpenEdit()
{
    EditorSession session;
    session.createDocument(100, 100);
    session.nudgeLayer(1, 0);
    QVERIFY(!session.transformEdit().has_value());
    const QUuid image = insertImage(session, 20, 10, {50, 50});
    const int count = session.history.undoCount();
    session.nudgeLayer(3, -2);
    QVERIFY(!session.transformEdit().has_value());
    QCOMPARE(layerWith(session, image).transform.origin, QPointF(43, 43));
    QCOMPARE(session.history.undoCount(), count + 1);
    QCOMPARE(session.history.undoName(), QString("Transform Layer"));
    // Inside an open edit a nudge moves the draft alone.
    session.beginTransform();
    session.nudgeLayer(1, 0);
    session.nudgeLayer(0, 4);
    QCOMPARE(session.transformEdit().value().draft.origin, QPointF(44, 47));
    QCOMPARE(layerWith(session, image).transform.origin, QPointF(43, 43));
    QCOMPARE(session.history.undoCount(), count + 1);
    session.cancelTransform();
    QVERIFY(!session.transformEdit().has_value());
    QCOMPARE(layerWith(session, image).transform.origin, QPointF(43, 43));
    QCOMPARE(session.history.undoCount(), count + 1);
    // A nudge past the valid origins moves nothing.
    session.nudgeLayer(2'000'000, 0);
    QVERIFY(!session.transformEdit().has_value());
    QCOMPARE(layerWith(session, image).transform.origin, QPointF(43, 43));
    QCOMPARE(session.history.undoCount(), count + 1);
}

void TransformSessionTests::anotherLayerToolOrCanvasCommits()
{
    EditorSession session;
    session.createDocument(100, 100);
    const QUuid first = insertImage(session, 20, 10, {50, 50});
    const QUuid second = insertImage(session, 20, 10, {50, 50});
    const auto nudged = [&](QUuid id) {
        session.selectLayer(id);
        session.selectTool(NavigationTool::move);
        session.beginTransform();
        session.nudgeLayer(1, 0);
        return layerWith(session, id).transform.origin.x();
    };
    // The same layer and tool leave the edit open.
    double x = nudged(second);
    session.selectLayer(second);
    session.selectTool(NavigationTool::move);
    QVERIFY(session.transformEdit().has_value());
    QCOMPARE(layerWith(session, second).transform.origin.x(), x);
    session.selectLayer(first);
    QVERIFY(!session.transformEdit().has_value());
    QCOMPARE(layerWith(session, second).transform.origin.x(), x + 1);
    x = nudged(first);
    session.selectLayers({first, second}, first);
    QVERIFY(!session.transformEdit().has_value());
    QCOMPARE(layerWith(session, first).transform.origin.x(), x + 1);
    // The same selection again commits nothing.
    session.beginTransform();
    session.nudgeLayer(5, 0);
    session.selectLayers({first, second}, first);
    QVERIFY(session.transformEdit().has_value());
    session.cancelTransform();
    x = nudged(second);
    const int count = session.history.undoCount();
    session.createDocument(50, 50);
    QVERIFY(!session.transformEdit().has_value());
    // The move, then the new canvas: two steps.
    QCOMPARE(session.history.undoCount(), count + 2);
    session.undo();
    QCOMPARE(layerWith(session, second).transform.origin.x(), x + 1);
}

void TransformSessionTests::anOpenTransformClosesTheGates()
{
    EditorSession session;
    session.createDocument(100, 100);
    const QUuid first = insertImage(session, 20, 10, {50, 50});
    const QUuid second = insertImage(session, 20, 10, {50, 50});
    QVERIFY(session.canEditLayers() && session.canUndo());
    session.beginTransform();
    QVERIFY(!session.canEditLayers() && !session.canUndo() && !session.canTransform());
    session.addBlankLayer();
    session.undo();
    QCOMPARE(int(session.document().value().layers.size()), 2);
    // A second begin keeps the draft of the first.
    session.nudgeLayer(7, 0);
    session.beginTransform();
    QCOMPARE(session.transformEdit().value().draft.origin.x(), 47.0);
    // Cmd-Shift-click still extends: the edit commits first.
    session.extendSelection(first);
    QVERIFY(!session.transformEdit().has_value());
    QCOMPARE(layerWith(session, second).transform.origin.x(), 47.0);
    QCOMPARE(session.selectedLayerIDs(), (QSet<QUuid>{first, second}));
    QVERIFY(session.canEditLayers() && session.canUndo());
}

void TransformSessionTests::handlesFollowTheDraftOrTheBox()
{
    EditorSession session;
    QCOMPARE(session.transformPixelSize(), std::nullopt);
    session.createDocument(100, 100);
    session.addBlankLayer();
    QCOMPARE(session.transformPixelSize(), std::nullopt);
    const QUuid image = insertImage(session, 20, 10, {50, 50});
    const QUuid other = insertImage(session, 8, 6, {20, 20});
    session.selectLayer(image);
    QCOMPARE(session.transformPixelSize(), std::optional(QSizeF(20, 10)));
    QCOMPARE(session.editedTransform(layerWith(session, image)), layerWith(session, image).transform);
    QCOMPARE(session.pendingTransform(layerWith(session, image)), std::nullopt);
    session.beginTransform();
    LayerTransform draft = session.transformEdit().value().draft;
    draft.size = {60, 30};
    draft.rotation = 45;
    session.previewTransform(draft);
    QCOMPARE(session.editedTransform(layerWith(session, image)), draft);
    QCOMPARE(session.pendingTransform(layerWith(session, image)), std::optional(draft));
    QCOMPARE(session.displayedTransform(layerWith(session, image)), draft);
    QCOMPARE(session.editedTransform(layerWith(session, other)), layerWith(session, other).transform);
    QCOMPARE(session.pendingTransform(layerWith(session, other)), std::nullopt);
    // The pixels stay the image's, whatever the draft or placement.
    QCOMPARE(session.transformPixelSize(), std::optional(QSizeF(20, 10)));
    session.commitTransform();
    QCOMPARE(layerWith(session, image).transform.size, QSizeF(60, 30));
    QCOMPARE(session.transformPixelSize(), std::optional(QSizeF(20, 10)));
    // Made active behind an open edit, a folder shows none.
    session.selectLayer(std::nullopt);
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    insertImage(session, 4, 4, {70, 70});
    session.selectLayer(image);
    session.beginTransform();
    session.setActiveLayerID(folder);
    QVERIFY(session.transformsAsGroup() && session.transformEdit().has_value());
    QCOMPARE(session.editedTransform(layerWith(session, folder)), layerWith(session, folder).transform);
    QCOMPARE(session.transformPixelSize(), std::nullopt);
    session.cancelTransform();
    QCOMPARE(session.editedTransform(layerWith(session, folder)), session.groupTransformBox().value());
}

QTEST_GUILESS_MAIN(TransformSessionTests)
#include "TransformSessionTests.moc"
