#include "Document/EditorSession.h"
#include <QtTest>
#include <memory>

class LayerTests : public QObject {
    Q_OBJECT
private slots:
    void importedLayerTakesItsSizeAndNameFromTheAsset();
    void blankLayerHasNoPixels();
    void layersCompareByImageIdentityNotContent();
    void layersCompareEveryField();
    void documentsCompareEveryField();
    void brushAndSelectionToolFamilies();
    void blankLayersAreTransparentAndInsertedAboveSelection();
    void deletionPreservesCanvasAndChoosesNeighbor();
    void renameAndVisibilityKeepIdentity();
    void reorderTranslatesVisibleOrderAndKeepsSelection();
    void unavailableActionsDoNotChangeDocument();
    void anEyeSwipeIsOneUndoStep();
    void newCanvasStartsWithOneSelectedEmptyLayer();
    void deletingAMultiSelectionRemovesEveryLayerInOneStep();
};

void LayerTests::importedLayerTakesItsSizeAndNameFromTheAsset()
{
    const ImportedImage asset(QImage(320, 200, QImage::Format_RGBA8888_Premultiplied), QImage(), "Photo");
    const ImageLayer layer(asset, QPointF(40, -10));
    QVERIFY(!layer.id.isNull());
    QCOMPARE(layer.name, QString("Photo"));
    QCOMPARE(layer.origin(), QPointF(40, -10));
    QCOMPARE(layer.size(), QSizeF(320, 200));
    QCOMPARE(layer.asset->image().size(), QSize(320, 200));
    QVERIFY(layer.isVisible);
    QCOMPARE(layer.opacity, 1.0);
    QCOMPARE(layer.blendMode, LayerBlendMode::normal);
}

void LayerTests::blankLayerHasNoPixels()
{
    const ImageLayer layer("Layer 1", QSizeF(1200, 800));
    QVERIFY(!layer.asset.has_value());
    QCOMPARE(layer.origin(), QPointF(0, 0));
    QCOMPARE(layer.size(), QSizeF(1200, 800));
    QVERIFY(!(layer.id == ImageLayer("Layer 1", QSizeF(1200, 800)).id));
}

void LayerTests::layersCompareByImageIdentityNotContent()
{
    QImage pixels(4, 4, QImage::Format_RGBA8888_Premultiplied);
    pixels.fill(Qt::red);
    const ImageLayer layer(ImportedImage(pixels, QImage(), "A"), QPointF(0, 0));
    ImageLayer same = layer;
    QCOMPARE(same, layer);
    same.opacity = 0.5;
    QVERIFY(!(same == layer));
    ImageLayer lookalike = layer;
    lookalike.asset = ImportedImage(pixels.copy(), QImage(), "A");
    QVERIFY(!(lookalike == layer));
    ImageLayer shared = layer;
    shared.asset = ImportedImage(pixels, QImage(), "A");
    QCOMPARE(shared, layer);
    ImageLayer emptied = layer;
    emptied.asset.reset();
    QVERIFY(!(emptied == layer));
}

void LayerTests::layersCompareEveryField()
{
    const ImageLayer layer("Layer 1", QSizeF(10, 10));
    const auto differs = [&](auto change) {
        ImageLayer other = layer;
        change(other);
        return !(layer == other);
    };
    QVERIFY(differs([](ImageLayer &other) { other.id = QUuid::createUuid(); }));
    QVERIFY(differs([](ImageLayer &other) { other.name = "Layer 2"; }));
    QVERIFY(differs([](ImageLayer &other) { other.isVisible = false; }));
    QVERIFY(differs([](ImageLayer &other) { other.transform.rotation = 1; }));
    QVERIFY(differs([](ImageLayer &other) { other.parentID = QUuid::createUuid(); }));
    QVERIFY(differs([](ImageLayer &other) { other.isGroup = true; }));
    QVERIFY(differs([](ImageLayer &other) { other.opacity = 1 - 1e-13; }));
    QVERIFY(differs([](ImageLayer &other) { other.blendMode = LayerBlendMode::multiply; }));
}

void LayerTests::documentsCompareEveryField()
{
    CanvasDocument document(400, 300);
    document.layers.push_back(ImageLayer("Layer 1", document.size()));
    QCOMPARE(document.size(), QSizeF(400, 300));
    QCOMPARE(document.resolution, 72.0);
    QVERIFY(!document.id.isNull());
    QVERIFY(document == CanvasDocument(document));
    const auto differs = [&](auto change) {
        CanvasDocument other = document;
        change(other);
        return !(document == other);
    };
    QVERIFY(differs([](CanvasDocument &other) { other.id = QUuid::createUuid(); }));
    QVERIFY(differs([](CanvasDocument &other) { other.width = 401; }));
    QVERIFY(differs([](CanvasDocument &other) { other.height = 301; }));
    QVERIFY(differs([](CanvasDocument &other) { other.resolution = 300; }));
    QVERIFY(differs([](CanvasDocument &other) { other.layers[0].isVisible = false; }));
    QVERIFY(differs([](CanvasDocument &other) { other.layers.clear(); }));
}

void LayerTests::brushAndSelectionToolFamilies()
{
    for (NavigationTool tool : {NavigationTool::brush, NavigationTool::spotHealing, NavigationTool::cloneStamp, NavigationTool::blur})
        QVERIFY(isBrushTool(tool) && !isSelectionTool(tool));
    for (NavigationTool tool : {NavigationTool::marquee, NavigationTool::lasso, NavigationTool::wand})
        QVERIFY(isSelectionTool(tool) && !isBrushTool(tool));
    for (NavigationTool tool : {NavigationTool::move, NavigationTool::crop, NavigationTool::gradient, NavigationTool::shape,
                                NavigationTool::type, NavigationTool::eyedropper, NavigationTool::hand, NavigationTool::zoom, NavigationTool::idle})
        QVERIFY(!isBrushTool(tool) && !isSelectionTool(tool));
}

namespace {
// 800x600 with three blank layers; the last one is active.
std::unique_ptr<EditorSession> sessionWithThreeLayers()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(800, 600);
    for (int index = 0; index < 3; ++index)
        session->addBlankLayer();
    return session;
}

QStringList names(const EditorSession &session)
{
    QStringList result;
    for (const ImageLayer &layer : session.document().value().layers)
        result << layer.name;
    return result;
}
}

void LayerTests::blankLayersAreTransparentAndInsertedAboveSelection()
{
    const auto session = sessionWithThreeLayers();
    session->setActiveLayerID(session->document().value().layers.front().id);
    session->addBlankLayer();
    const std::vector<ImageLayer> layers = session->document().value().layers;
    QCOMPARE(names(*session), (QStringList{"Layer 1", "Layer 4", "Layer 2", "Layer 3"}));
    QCOMPARE(std::optional(layers[1].id), session->activeLayerID());
    QVERIFY(!layers[1].asset.has_value());
    QCOMPARE(layers[1].size(), QSizeF(800, 600));
    QCOMPARE(layers[1].origin(), QPointF(0, 0));
    QCOMPARE(session->history.undoName(), QString("New Blank Layer"));
    // A freed number is used again; unselected, it tops.
    session->deleteLayer(layers[2].id);
    session->selectLayer(std::nullopt);
    session->addBlankLayer();
    QCOMPARE(names(*session), (QStringList{"Layer 1", "Layer 4", "Layer 3", "Layer 2"}));
}

void LayerTests::deletionPreservesCanvasAndChoosesNeighbor()
{
    const auto session = sessionWithThreeLayers();
    const std::vector<ImageLayer> layers = session->document().value().layers;
    session->setActiveLayerID(layers[1].id);
    session->deleteLayer(layers[0].id);
    QCOMPARE(session->activeLayerID(), std::optional(layers[1].id));
    session->deleteActiveLayer();
    QCOMPARE(session->activeLayerID(), std::optional(layers[2].id));
    session->deleteActiveLayer();
    QCOMPARE(session->activeLayerID(), std::nullopt);
    QVERIFY(session->document().value().layers.empty());
    QCOMPARE(session->document().value().size(), QSizeF(800, 600));
    session->addBlankLayer();
    QCOMPARE(int(session->document().value().layers.size()), 1);
    // Deleting the top layer moves down to its neighbour.
    const auto other = sessionWithThreeLayers();
    const std::vector<ImageLayer> three = other->document().value().layers;
    other->deleteActiveLayer();
    QCOMPARE(other->activeLayerID(), std::optional(three[1].id));
    QCOMPARE(other->history.undoName(), QString("Delete Layer"));
    other->deleteLayer(QUuid::createUuid());
    QCOMPARE(int(other->document().value().layers.size()), 2);
    // Deleting below the active layer leaves the selection alone.
    const auto third = sessionWithThreeLayers();
    const std::vector<ImageLayer> stack = third->document().value().layers;
    third->deleteLayer(stack[0].id);
    QCOMPARE(third->activeLayerID(), std::optional(stack[2].id));
    // What takes a deleted bottom layer's place is active.
    third->undo();
    third->setActiveLayerID(stack[0].id);
    third->deleteActiveLayer();
    QCOMPARE(third->activeLayerID(), std::optional(stack[1].id));
}

void LayerTests::renameAndVisibilityKeepIdentity()
{
    const auto session = sessionWithThreeLayers();
    const QUuid id = session->activeLayerID().value();
    session->renameLayer(id, "  Foreground \n");
    session->renameLayer(id, " \n ");
    QCOMPARE(session->activeLayer().value().name, QString("Foreground"));
    QCOMPARE(session->history.undoName(), QString("Rename Layer"));
    session->toggleLayerVisibility(id);
    QCOMPARE(session->activeLayer().value().isVisible, false);
    QCOMPARE(session->history.undoName(), QString("Hide Layer"));
    QCOMPARE(session->activeLayerID(), std::optional(id));
    session->toggleLayerVisibility(id);
    QCOMPARE(session->activeLayer().value().isVisible, true);
    QCOMPARE(session->history.undoName(), QString("Show Layer"));
    // Renaming waits out a project operation and an import.
    session->setIsProjectBusy(true);
    session->renameLayer(id, "Busy");
    session->setIsProjectBusy(false);
    session->setIsImporting(true);
    session->renameLayer(id, "Importing");
    session->renameLayer(QUuid::createUuid(), "Nobody");
    QCOMPARE(session->activeLayer().value().name, QString("Foreground"));
}

void LayerTests::reorderTranslatesVisibleOrderAndKeepsSelection()
{
    const auto session = sessionWithThreeLayers();
    const std::optional<QUuid> active = session->activeLayerID();
    session->reorderLayers({0}, 3);
    QCOMPARE(names(*session), (QStringList{"Layer 3", "Layer 1", "Layer 2"}));
    QCOMPARE(session->activeLayerID(), active);
    QVERIFY(!session->canMoveActiveLayer(-1));
    QVERIFY(session->canMoveActiveLayer(2));
    QVERIFY(!session->canMoveActiveLayer(3));
    session->moveActiveLayer(1);
    QCOMPARE(names(*session), (QStringList{"Layer 1", "Layer 3", "Layer 2"}));
    QCOMPARE(session->history.undoName(), QString("Reorder Layers"));
    const int count = session->history.undoCount();
    session->reorderLayers({99}, 0);
    session->reorderLayers({3}, 0);
    session->reorderLayers({0}, 4);
    session->reorderLayers({-1}, 0);
    session->reorderLayers({0}, -1);
    // One row out of range refuses the whole move.
    session->reorderLayers({0, 3}, 3);
    session->reorderLayers({-1, 0}, 3);
    session->moveActiveLayer(2);
    QCOMPARE(names(*session), (QStringList{"Layer 1", "Layer 3", "Layer 2"}));
    QCOMPARE(session->history.undoCount(), count);
    // Rows 0 and 2 land together before row 2.
    session->reorderLayers({0, 2}, 2);
    QCOMPARE(names(*session), (QStringList{"Layer 1", "Layer 2", "Layer 3"}));
    session->reorderLayers({1, 2}, 0);
    QCOMPARE(names(*session), (QStringList{"Layer 3", "Layer 1", "Layer 2"}));
    session->reorderLayers({0}, 3);
    QCOMPARE(names(*session), (QStringList{"Layer 2", "Layer 3", "Layer 1"}));
}

void LayerTests::unavailableActionsDoNotChangeDocument()
{
    EditorSession session;
    session.addBlankLayer();
    session.addGroup();
    QVERIFY(!session.document().has_value());
    session.createDocument(30'000, 30'000);
    session.addBlankLayer();
    QVERIFY(!session.activeLayer().value().asset.has_value());
    session.setIsImporting(true);
    session.addBlankLayer();
    session.addGroup();
    session.deleteActiveLayer();
    session.toggleLayerVisibility(session.activeLayerID().value());
    session.reorderLayers({0}, 1);
    session.moveActiveLayer(0);
    QVERIFY(!session.beginVisibilitySwipe(session.activeLayerID().value()).has_value());
    QCOMPARE(int(session.document().value().layers.size()), 1);
    QVERIFY(session.activeLayer().value().isVisible);
}

void LayerTests::anEyeSwipeIsOneUndoStep()
{
    const auto session = sessionWithThreeLayers();
    const std::vector<ImageLayer> layers = session->document().value().layers;
    const int count = session->history.undoCount();
    QCOMPARE(session->beginVisibilitySwipe(layers[0].id), std::optional(false));
    session->setVisibilityInSwipe(layers[1].id, false);
    session->setVisibilityInSwipe(layers[1].id, false);
    session->setVisibilityInSwipe(QUuid::createUuid(), false);
    session->endVisibilitySwipe();
    QCOMPARE(session->history.undoCount(), count + 1);
    QCOMPARE(session->history.undoName(), QString("Hide Layer"));
    QVERIFY(!session->document().value().layers[0].isVisible && !session->document().value().layers[1].isVisible);
    QVERIFY(session->document().value().layers[2].isVisible);
    // Pressing a hidden eye shows, and says so.
    QCOMPARE(session->beginVisibilitySwipe(layers[1].id), std::optional(true));
    session->endVisibilitySwipe();
    QCOMPARE(session->history.undoName(), QString("Show Layer"));
    QVERIFY(session->document().value().layers[1].isVisible);
    QVERIFY(!session->beginVisibilitySwipe(QUuid::createUuid()).has_value());
}

void LayerTests::newCanvasStartsWithOneSelectedEmptyLayer()
{
    EditorSession session;
    session.createNewProject(640, 480);
    QCOMPARE(int(session.document().value().layers.size()), 1);
    const ImageLayer active = session.activeLayer().value();
    QVERIFY(active.name == "Layer 1" && !active.asset.has_value());
    QCOMPARE(active.size(), QSizeF(640, 480));
    QCOMPARE(session.selectedLayerIDs(), QSet<QUuid>{active.id});
    QVERIFY(session.canPaint());
}

QTEST_GUILESS_MAIN(LayerTests)
#include "LayerTests.moc"

void LayerTests::deletingAMultiSelectionRemovesEveryLayerInOneStep()
{
    EditorSession session;
    session.createDocument(100, 100);
    session.addBlankLayer();
    const QUuid keep = session.activeLayerID().value();
    session.selectLayer(std::nullopt);
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    session.addBlankLayer();
    const QUuid child = session.activeLayerID().value();
    session.selectLayer(std::nullopt);
    session.addBlankLayer();
    const QUuid top = session.activeLayerID().value();
    const std::vector<ImageLayer> &layers = session.document().value().layers;
    QCOMPARE(layers[size_t(indexOf(layers, child))].parentID, std::optional(folder));
    const std::optional<CanvasDocument> before = session.document();
    const int count = session.history.undoCount();
    session.selectLayers({folder, top}, top);
    session.deleteLayerOrMask();
    // The folder, its child and the other layer are gone.
    QCOMPARE(layers.size(), size_t(1));
    QCOMPARE(layers[0].id, keep);
    QCOMPARE(session.history.undoCount(), count + 1);
    QCOMPARE(session.history.undoName(), QString("Delete Layers"));
    QCOMPARE(session.activeLayerID(), std::optional(keep));
    QCOMPARE(session.selectedLayerIDs(), QSet<QUuid>{keep});
    session.undo();
    QVERIFY(session.document() == before);
    // A single selection still deletes just that layer.
    session.selectLayers({keep}, keep);
    session.deleteLayerOrMask();
    QVERIFY(indexOf(layers, keep) < 0);
    QCOMPARE(session.history.undoName(), QString("Delete Layer"));
}
