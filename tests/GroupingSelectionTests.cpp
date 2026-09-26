#include "Document/EditorSession.h"
#include <QtTest>

namespace {
ImageLayer layerWith(const EditorSession &session, QUuid id)
{
    for (const ImageLayer &layer : session.document().value().layers) {
        if (layer.id == id)
            return layer;
    }
    throw std::runtime_error("no such layer");
}

ImportedImage asset(int width, int height, const QString &name)
{
    return ImportedImage(QImage(width, height, QImage::Format_RGBA8888_Premultiplied), QImage(), name);
}

QStringList names(const EditorSession &session)
{
    QStringList result;
    for (const ImageLayer &layer : session.document().value().layers)
        result << layer.name;
    return result;
}
}

class GroupingSelectionTests : public QObject {
    Q_OBJECT
private slots:
    void singleLayerAndFolderAreWrappedRatherThanCreatingAChildFolder();
    void multipleSelectionPreservesOrderAndSelectedFolderDescendants();
    void itemsFromDifferentFoldersUseCommonParentAndEmptySelectionCreatesEmptyGroup();
    void nestedSelectionsShareAnAncestorAboveTheirParents();
    void wrappedLayersKeepTheirStackingOrder();
    void theWrapperSitsAtTheTopmostSelectedBranch();
    void selectionKeepsOnlyLayersTheDocumentHolds();
    void extendingTheSelectionAddsAndTakesAway();
    void severalSelectedLayersDeleteAsOneStep();
    void aSelectionTheDocumentLacksMakesAnEmptyFolder();
    void deletingAListOfOneOrNone();
    void groupingIsRefusedAtItsLimits();
};

void GroupingSelectionTests::singleLayerAndFolderAreWrappedRatherThanCreatingAChildFolder()
{
    EditorSession session;
    session.createDocument(100, 100);
    session.addBlankLayer();
    const QUuid layer = session.activeLayerID().value();
    session.groupSelectedLayers();
    const QUuid inner = session.activeLayerID().value();
    QCOMPARE(layerWith(session, layer).parentID, std::optional(inner));
    QCOMPARE(session.history.undoName(), QString("Group Layers"));
    session.groupSelectedLayers();
    const QUuid outer = session.activeLayerID().value();
    QCOMPARE(layerWith(session, inner).parentID, std::optional(outer));
    QCOMPARE(layerWith(session, layer).parentID, std::optional(inner));
    QCOMPARE(session.activeLayer().value().parentID, std::nullopt);
    QVERIFY(session.activeLayer().value().isGroup);
    // What stays keeps its place; what is wrapped goes last.
    QCOMPARE(names(session), (QStringList{"Folder 2", "Layer 1", "Folder 1"}));
    session.undo();
    QCOMPARE(layerWith(session, inner).parentID, std::nullopt);
    QCOMPARE(int(session.document().value().layers.size()), 2);
}

void GroupingSelectionTests::multipleSelectionPreservesOrderAndSelectedFolderDescendants()
{
    EditorSession session;
    session.createDocument(100, 100);
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    session.addBlankLayer();
    const QUuid child = session.activeLayerID().value();
    session.selectLayer(std::nullopt);
    session.addBlankLayer();
    const QUuid sibling = session.activeLayerID().value();
    const std::optional<CanvasDocument> before = session.document();
    session.selectLayers({folder, child, sibling}, sibling);
    QCOMPARE(int(session.selectedLayerIDs().size()), 3);
    QCOMPARE(session.activeLayerID(), std::optional(sibling));
    QVERIFY(!session.canTransform());
    session.groupSelectedLayers();
    const QUuid wrapper = session.activeLayerID().value();
    QCOMPARE(layerWith(session, folder).parentID, std::optional(wrapper));
    QCOMPARE(layerWith(session, sibling).parentID, std::optional(wrapper));
    QCOMPARE(layerWith(session, child).parentID, std::optional(folder));
    std::vector<QUuid> rendered;
    for (const ImageLayer &layer : session.document().value().renderLayers())
        rendered.push_back(layer.id);
    QCOMPARE(rendered, (std::vector<QUuid>{child, sibling}));
    QCOMPARE(session.selectedLayerIDs(), QSet<QUuid>{wrapper});
    session.undo();
    QCOMPARE(session.document(), before);
    session.redo();
    QCOMPARE(int(session.document().value().layers.size()), 4);
}

void GroupingSelectionTests::itemsFromDifferentFoldersUseCommonParentAndEmptySelectionCreatesEmptyGroup()
{
    EditorSession session;
    session.createDocument(100, 100);
    session.groupSelectedLayers();
    const QUuid first = session.activeLayerID().value();
    QVERIFY(session.activeLayer().value().isGroup);
    session.addBlankLayer();
    const QUuid a = session.activeLayerID().value();
    session.selectLayer(std::nullopt);
    session.groupSelectedLayers();
    const QUuid second = session.activeLayerID().value();
    session.addBlankLayer();
    const QUuid b = session.activeLayerID().value();
    session.selectLayers({a, b}, b);
    session.groupSelectedLayers();
    const QUuid group = session.activeLayerID().value();
    QCOMPARE(session.activeLayer().value().parentID, std::nullopt);
    QCOMPARE(layerWith(session, a).parentID, std::optional(group));
    QCOMPARE(layerWith(session, b).parentID, std::optional(group));
    QCOMPARE(layerWith(session, first).parentID, std::nullopt);
    QCOMPARE(layerWith(session, second).parentID, std::nullopt);
    // Two layers of one folder are wrapped inside that folder.
    session.selectLayer(first);
    session.addBlankLayer();
    const QUuid c = session.activeLayerID().value();
    session.addBlankLayer();
    const QUuid d = session.activeLayerID().value();
    session.toggleGroupExpansion(first);
    session.selectLayers({c, d}, d);
    session.groupSelectedLayers();
    QCOMPARE(session.activeLayer().value().parentID, std::optional(first));
    QVERIFY(!session.collapsedGroupIDs().contains(first));
    QCOMPARE(layerWith(session, c).parentID, session.activeLayerID());
    QCOMPARE(layerWith(session, d).parentID, session.activeLayerID());
}

void GroupingSelectionTests::nestedSelectionsShareAnAncestorAboveTheirParents()
{
    EditorSession session;
    session.createDocument(100, 100);
    // G holds A(C(a)), then B(b) below it.
    session.addGroup();
    const QUuid g = session.activeLayerID().value();
    session.addGroup();
    const QUuid folderA = session.activeLayerID().value();
    session.addGroup();
    const QUuid folderC = session.activeLayerID().value();
    session.addBlankLayer();
    const QUuid a = session.activeLayerID().value();
    session.selectLayer(g);
    session.addGroup();
    const QUuid folderB = session.activeLayerID().value();
    session.addBlankLayer();
    const QUuid b = session.activeLayerID().value();
    // b comes first: its own folder is no shared one.
    QCOMPARE(names(session), (QStringList{"Folder 1", "Folder 4", "Layer 2", "Folder 2", "Folder 3", "Layer 1"}));
    QCOMPARE(layerWith(session, folderB).parentID, std::optional(g));
    QCOMPARE(layerWith(session, folderA).parentID, std::optional(g));
    QCOMPARE(layerWith(session, folderC).parentID, std::optional(folderA));
    QCOMPARE(layerWith(session, a).parentID, std::optional(folderC));
    // A carries a; b and A meet in G.
    session.selectLayers({folderA, a, b}, b);
    session.groupSelectedLayers();
    const ImageLayer wrapper = session.activeLayer().value();
    QVERIFY(wrapper.isGroup);
    QCOMPARE(wrapper.parentID, std::optional(g));
    QCOMPARE(layerWith(session, folderA).parentID, std::optional(wrapper.id));
    QCOMPARE(layerWith(session, b).parentID, std::optional(wrapper.id));
    QCOMPARE(layerWith(session, a).parentID, std::optional(folderC));
    QCOMPARE(layerWith(session, folderC).parentID, std::optional(folderA));
    QCOMPARE(layerWith(session, folderB).parentID, std::optional(g));
    // The wrapper stands where A stood: G's higher branch.
    QCOMPARE(names(session), (QStringList{"Folder 1", "Folder 4", "Folder 5", "Folder 3", "Layer 1", "Layer 2", "Folder 2"}));
}

void GroupingSelectionTests::wrappedLayersKeepTheirStackingOrder()
{
    EditorSession session;
    session.createDocument(100, 100);
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    session.selectLayer(std::nullopt);
    session.addBlankLayer();
    const QUuid above = session.activeLayerID().value();
    session.addBlankLayer();
    const QUuid inside = session.activeLayerID().value();
    // Last in the list, yet inside the folder below.
    QVERIFY(session.placeLayer(inside, folder));
    QCOMPARE(names(session), (QStringList{"Folder 1", "Layer 1", "Layer 2"}));
    session.selectLayers({above, inside}, above);
    session.groupSelectedLayers();
    QCOMPARE(names(session), (QStringList{"Folder 1", "Folder 2", "Layer 2", "Layer 1"}));
    std::vector<QUuid> rendered;
    for (const ImageLayer &layer : session.document().value().renderLayers())
        rendered.push_back(layer.id);
    QCOMPARE(rendered, (std::vector<QUuid>{inside, above}));
}

void GroupingSelectionTests::theWrapperSitsAtTheTopmostSelectedBranch()
{
    EditorSession session;
    session.createDocument(100, 100);
    for (int index = 0; index < 4; ++index)
        session.addBlankLayer();
    const std::vector<ImageLayer> layers = session.document().value().layers;
    // Layers 1 and 3 are wrapped where Layer 3 stood.
    session.selectLayers({layers[0].id, layers[2].id}, layers[0].id);
    session.groupSelectedLayers();
    QCOMPARE(names(session), (QStringList{"Layer 2", "Folder 1", "Layer 4", "Layer 1", "Layer 3"}));
    std::vector<QString> rows;
    for (const LayerHierarchy::Entry &row : session.layerRows())
        rows.push_back(QString(row.depth, '>') + row.layer.name);
    QCOMPARE(rows, (std::vector<QString>{"Layer 4", "Folder 1", ">Layer 3", ">Layer 1", "Layer 2"}));
    // A layer deep in a folder stands for that folder.
    EditorSession deep;
    deep.createDocument(100, 100);
    deep.addBlankLayer();
    const QUuid low = deep.activeLayerID().value();
    deep.addGroup();
    const QUuid folder = deep.activeLayerID().value();
    deep.selectLayer(std::nullopt);
    deep.addBlankLayer();
    deep.addBlankLayer();
    const QUuid child = deep.activeLayerID().value();
    // Placed last in the list, yet inside the folder.
    QVERIFY(deep.placeLayer(child, folder));
    QCOMPARE(names(deep), (QStringList{"Layer 1", "Folder 1", "Layer 2", "Layer 3"}));
    deep.selectLayers({low, child}, low);
    deep.groupSelectedLayers();
    QCOMPARE(names(deep), (QStringList{"Folder 1", "Folder 2", "Layer 2", "Layer 1", "Layer 3"}));
    QCOMPARE(deep.activeLayer().value().parentID, std::nullopt);
}

void GroupingSelectionTests::selectionKeepsOnlyLayersTheDocumentHolds()
{
    EditorSession session;
    session.selectLayers({QUuid::createUuid()}, std::nullopt);
    QCOMPARE(session.selectedLayerIDs(), QSet<QUuid>());
    session.createDocument(100, 100);
    session.addBlankLayer();
    const QUuid first = session.activeLayerID().value();
    session.addBlankLayer();
    const QUuid second = session.activeLayerID().value();
    const QUuid stranger = QUuid::createUuid();
    session.selectLayers({first, second, stranger}, stranger);
    QCOMPARE(session.selectedLayerIDs(), (QSet<QUuid>{first, second}));
    // A primary the document lacks leaves the set to choose.
    QVERIFY(session.selectedLayerIDs().contains(session.activeLayerID().value()));
    session.selectLayers({first, second}, first);
    QCOMPARE(session.activeLayerID(), std::optional(first));
    session.selectLayers({}, std::nullopt);
    QCOMPARE(session.selectedLayerIDs(), QSet<QUuid>());
    QCOMPARE(session.activeLayerID(), std::nullopt);
}

void GroupingSelectionTests::extendingTheSelectionAddsAndTakesAway()
{
    EditorSession session;
    session.createDocument(100, 100);
    session.addBlankLayer();
    const QUuid first = session.activeLayerID().value();
    session.addBlankLayer();
    const QUuid second = session.activeLayerID().value();
    session.extendSelection(first);
    QCOMPARE(session.selectedLayerIDs(), (QSet<QUuid>{first, second}));
    QCOMPARE(session.activeLayerID(), std::optional(first));
    // Taking the active layer out hands its role on.
    session.extendSelection(first);
    QCOMPARE(session.selectedLayerIDs(), QSet<QUuid>{second});
    QCOMPARE(session.activeLayerID(), std::optional(second));
    // The last selected layer stays selected.
    session.extendSelection(second);
    QCOMPARE(session.selectedLayerIDs(), QSet<QUuid>{second});
    session.extendSelection(first);
    session.extendSelection(second);
    QCOMPARE(session.selectedLayerIDs(), QSet<QUuid>{first});
    QCOMPARE(session.activeLayerID(), std::optional(first));
    session.extendSelection(QUuid::createUuid());
    session.setIsImporting(true);
    session.extendSelection(second);
    QCOMPARE(session.selectedLayerIDs(), QSet<QUuid>{first});
    // Taking a third layer out leaves either active layer alone.
    session.setIsImporting(false);
    session.addBlankLayer();
    const QUuid third = session.activeLayerID().value();
    for (const QUuid primary : {first, second}) {
        session.selectLayers({first, second, third}, primary);
        session.extendSelection(third);
        QCOMPARE(session.activeLayerID(), std::optional(primary));
        QCOMPARE(session.selectedLayerIDs(), (QSet<QUuid>{first, second}));
    }
}

void GroupingSelectionTests::severalSelectedLayersDeleteAsOneStep()
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
    QCOMPARE(layerWith(session, child).parentID, std::optional(folder));
    const std::optional<CanvasDocument> before = session.document();
    const int count = session.history.undoCount();
    session.selectLayers({folder, top}, top);
    session.deleteSelectedLayers();
    QCOMPARE(names(session), QStringList{"Layer 1"});
    QCOMPARE(session.history.undoCount(), count + 1);
    QCOMPARE(session.history.undoName(), QString("Delete Layers"));
    QCOMPARE(session.activeLayerID(), std::optional(keep));
    QCOMPARE(session.selectedLayerIDs(), QSet<QUuid>{keep});
    session.undo();
    QCOMPARE(session.document(), before);
    // One selected layer is deleted as itself.
    session.selectLayers({keep}, keep);
    session.deleteSelectedLayers();
    QVERIFY(names(session).indexOf("Layer 1") < 0);
    QCOMPARE(session.history.undoName(), QString("Delete Layer"));
    session.setIsImporting(true);
    session.deleteSelectedLayers();
    QCOMPARE(int(session.document().value().layers.size()), 3);
}

void GroupingSelectionTests::aSelectionTheDocumentLacksMakesAnEmptyFolder()
{
    EditorSession session;
    session.createDocument(100, 100);
    session.addBlankLayer();
    session.addBlankLayer();
    session.selectLayer(QUuid::createUuid());
    session.groupSelectedLayers();
    // Nothing to wrap: the empty folder goes on top.
    QCOMPARE(names(session), (QStringList{"Layer 1", "Layer 2", "Folder 1"}));
    QCOMPARE(session.activeLayer().value().parentID, std::nullopt);
    for (const ImageLayer &layer : session.document().value().layers)
        QCOMPARE(layer.parentID, std::nullopt);
}

void GroupingSelectionTests::deletingAListOfOneOrNone()
{
    EditorSession session;
    session.createDocument(100, 100);
    session.addBlankLayer();
    const QUuid first = session.activeLayerID().value();
    session.addBlankLayer();
    const int count = session.history.undoCount();
    session.finishDeletingLayers({}, {});
    QCOMPARE(session.history.undoCount(), count);
    QCOMPARE(names(session), (QStringList{"Layer 1", "Layer 2"}));
    session.finishDeletingLayers({first}, {});
    QCOMPARE(names(session), QStringList{"Layer 2"});
    QCOMPARE(session.history.undoName(), QString("Delete Layer"));
    QCOMPARE(session.history.undoCount(), count + 1);
}

void GroupingSelectionTests::groupingIsRefusedAtItsLimits()
{
    EditorSession session;
    session.groupSelectedLayers();
    QVERIFY(!session.document().has_value());
    session.createDocument(8, 8);
    session.addBlankLayer();
    // Wrapped 64 times over, a layer can go no deeper.
    for (int depth = 0; depth < 70; ++depth) {
        session.selectLayer(session.document().value().layers.back().id);
        session.groupSelectedLayers();
    }
    QCOMPARE(int(session.document().value().layers.size()), 1 + 64);
    session.setIsImporting(true);
    session.groupSelectedLayers();
    QCOMPARE(int(session.document().value().layers.size()), 1 + 64);
    // A document holds 10,000 layers and no more.
    EditorSession crowded;
    crowded.createDocument(8, 8);
    crowded.beginEdit("Fill");
    const ImportedImage dot = asset(1, 1, "Dot");
    for (int index = 0; index < 9'999; ++index)
        crowded.insert(dot);
    crowded.endEdit();
    crowded.groupSelectedLayers();
    QCOMPARE(int(crowded.document().value().layers.size()), 10'000);
    QVERIFY(crowded.activeLayer().value().isGroup);
    crowded.groupSelectedLayers();
    QCOMPARE(int(crowded.document().value().layers.size()), 10'000);
}

QTEST_GUILESS_MAIN(GroupingSelectionTests)
#include "GroupingSelectionTests.moc"
