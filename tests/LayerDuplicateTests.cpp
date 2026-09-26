#include "Document/EditorSession.h"
#include "SessionFixtures.h"
#include <QtTest>

// Duplicating layers and folders in the session.
class LayerDuplicateTests : public QObject {
    Q_OBJECT
private slots:
    void duplicatingALayerByDraggingPlacesTheCopyAsOneStep();
    void aFolderCopyKeepsLinksInsideAndStopsAtTheLimit();
};

void LayerDuplicateTests::duplicatingALayerByDraggingPlacesTheCopyAsOneStep()
{
    EditorSession session;
    session.createDocument(800, 600);
    for (int each = 0; each < 3; ++each)
        session.addBlankLayer();
    const std::vector<LayerHierarchy::Entry> rows = session.layerRows();
    const ProjectLayerRecord top = rows.front().layer, bottom = rows.back().layer;
    const std::optional<CanvasDocument> before = session.document();
    const int count = session.history.undoCount();
    QVERIFY(session.duplicateLayer(bottom.id, std::nullopt, top.id));
    const std::vector<LayerHierarchy::Entry> after = session.layerRows();
    QCOMPARE(after.size(), size_t(4));
    QCOMPARE(session.history.undoCount(), count + 1);
    QCOMPARE(session.history.undoName(), QString("Duplicate Layer"));
    QCOMPARE(after.front().layer.name, bottom.name + " copy");
    QCOMPARE(after.front().layer.id, session.activeLayerID());
    QVERIFY(indexOf(session.document().value().layers, bottom.id) >= 0);
    session.undo();
    QCOMPARE(session.document(), before);
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    // A stranger or a place inside itself is refused.
    QVERIFY(!session.duplicateLayer(QUuid::createUuid(), std::nullopt));
    QVERIFY(!session.duplicateLayer(bottom.id, bottom.id));
    QVERIFY(!session.duplicateLayer(folder, folder));
    QCOMPARE(session.layerRows().size(), size_t(4));
    // Into a folder, at its top.
    QVERIFY(session.duplicateLayer(bottom.id, folder));
    const QUuid inside = session.activeLayerID().value();
    QCOMPARE(layerWith(session, inside).parentID, std::optional(folder));
    session.setIsImporting(true);
    QVERIFY(!session.duplicateLayer(bottom.id, std::nullopt));
    session.setIsImporting(false);
    // A folder copies with what it holds, at the bottom.
    session.toggleGroupExpansion(folder);
    const int layers = int(session.document().value().layers.size());
    QVERIFY(session.duplicateLayer(folder, std::nullopt, std::nullopt, true));
    const ImageLayer copy = layerWith(session, session.activeLayerID().value());
    QVERIFY(copy.isGroup && copy.id != folder && !copy.parentID);
    QCOMPARE(copy.name, layerWith(session, folder).name + " copy");
    const QSet<QUuid> held = session.descendantIDs(copy.id);
    QCOMPARE(int(held.size()), 1);
    const ImageLayer child = layerWith(session, *held.begin());
    // The child keeps its name and moves to the copy.
    QVERIFY(child.id != inside && child.name == layerWith(session, inside).name && child.parentID == copy.id);
    QVERIFY(session.collapsedGroupIDs().contains(copy.id));
    QCOMPARE(int(session.document().value().layers.size()), layers + 2);
    QCOMPARE(session.layerRows().back().layer.id, copy.id);
    session.undo();
    QCOMPARE(int(session.document().value().layers.size()), layers);
    // The copy sits right above its source.
    session.selectLayer(top.id);
    session.duplicateActiveLayer();
    const int original = indexOf(session.document().value().layers, top.id);
    QCOMPARE(session.document().value().layers[original + 1].name, top.name + " copy");
    QCOMPARE(session.activeLayerID(), std::optional(session.document().value().layers[original + 1].id));
}

void LayerDuplicateTests::aFolderCopyKeepsLinksInsideAndStopsAtTheLimit()
{
    EditorSession session;
    session.createDocument(40, 40, true);
    const QUuid base = session.activeLayerID().value();
    QImage white(10, 10, QImage::Format_RGBA8888_Premultiplied);
    white.fill(Qt::white);
    session.insert(ImportedImage(white, white, QStringLiteral("White")));
    const QUuid clipped = session.activeLayerID().value();
    QVERIFY(session.canToggleClippingMask(clipped));
    session.toggleClippingMask(clipped);
    session.selectLayers({base, clipped}, clipped);
    session.groupSelectedLayers();
    const QUuid folder = session.activeLayerID().value();
    session.duplicateActiveLayer();
    const QUuid copy = session.activeLayerID().value();
    QVERIFY(copy != folder);
    // Links inside the copy point inside the copy.
    std::optional<QUuid> copiedBase, copiedClip;
    for (const QUuid &id : session.descendantIDs(copy)) {
        const ImageLayer layer = layerWith(session, id);
        (layer.maskSourceID ? copiedClip : copiedBase) = id;
    }
    QCOMPARE(layerWith(session, copiedClip.value()).maskSourceID, copiedBase);
    QVERIFY(copiedBase != base);
    QCOMPARE(layerWith(session, clipped).maskSourceID, std::optional(base));
    // A copy alone keeps links and a parent outside it.
    session.selectLayer(clipped);
    session.duplicateActiveLayer();
    const ImageLayer lone = layerWith(session, session.activeLayerID().value());
    QVERIFY(lone.id != clipped && lone.maskSourceID == base && lone.parentID == folder);
    session.undo();
    // Past 10,000 layers nothing is copied or recorded.
    ProjectSnapshot snapshot = session.projectSnapshot().value();
    // Three copies into 9,997 reach 10,000 exactly: allowed.
    while (snapshot.manifest.layers.size() < 9'997)
        snapshot.manifest.layers.push_back(ProjectLayerRecord{.id = QUuid::createUuid(), .name = "Blank", .isVisible = true,
                                                              .transform = {.origin = {0, 0}, .size = {40, 40}}, .imageFile = std::nullopt});
    session.installProject(snapshot, QString());
    session.selectLayer(folder);
    session.duplicateActiveLayer();
    QVERIFY(session.activeLayerID() != folder);
    QCOMPARE(int(session.document().value().layers.size()), 10'000);
    // Past it nothing is copied or recorded.
    session.selectLayer(folder);
    const int steps = session.history.undoCount();
    session.duplicateActiveLayer();
    QCOMPARE(session.activeLayerID(), std::optional(folder));
    QCOMPARE(int(session.document().value().layers.size()), 10'000);
    QCOMPARE(session.history.undoCount(), steps);
    QVERIFY(!session.duplicateLayer(folder, std::nullopt, std::nullopt, true));
}

QTEST_GUILESS_MAIN(LayerDuplicateTests)
#include "LayerDuplicateTests.moc"
