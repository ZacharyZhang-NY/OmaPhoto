#include "Document/EditorSession.h"
#include "IO/CanvasResizer.h"
#include "IO/ImageExporter.h"
#include "IO/ImageResizer.h"
#include <QTemporaryDir>
#include <QtTest>

// The session cases of Swift's GroupTests.
namespace {
std::vector<QUuid> rowIDs(const EditorSession &session)
{
    std::vector<QUuid> result;
    for (const LayerHierarchy::Entry &row : session.layerRows())
        result.push_back(row.layer.id);
    return result;
}

std::vector<QUuid> renderIDs(const EditorSession &session)
{
    std::vector<QUuid> result;
    for (const ImageLayer &layer : session.document().value().renderLayers())
        result.push_back(layer.id);
    return result;
}

std::optional<QUuid> parentOf(const EditorSession &session, QUuid id)
{
    for (const ImageLayer &layer : session.document().value().layers) {
        if (layer.id == id)
            return layer.parentID;
    }
    throw std::runtime_error("no such layer");
}
}

class GroupSessionTests : public QObject {
    Q_OBJECT
private slots:
    void nestedGroupsMoveOutCollapseAndDeleteUndo();
    void layersArePlacedAboveBelowAndInsideFolders();
    void placingRespectsTheNestingLimit();
    void hiddenParentOverridesChildrenAndExportOrderFollowsGroups();
    void groupsRoundTripAndSurviveImageAndCanvasResize();
    void rowsOfASessionWithoutADocument();
};

void GroupSessionTests::nestedGroupsMoveOutCollapseAndDeleteUndo()
{
    EditorSession session;
    session.createDocument(100, 100);
    session.addGroup();
    const QUuid outer = session.activeLayerID().value();
    session.addGroup();
    const QUuid inner = session.activeLayerID().value();
    session.addBlankLayer();
    const QUuid child = session.activeLayerID().value();
    QCOMPARE(session.activeLayer().value().parentID, std::optional(inner));
    QVERIFY(!session.placeLayer(outer, inner));
    QVERIFY(!session.placeLayer(inner, inner));
    // The panel asks before a drop; validation never sees these.
    QVERIFY(!session.canPlaceLayer(outer, inner));
    QVERIFY(!session.canPlaceLayer(inner, inner));
    QVERIFY(!session.canPlaceLayer(outer, child));
    QVERIFY(!session.canPlaceLayer(outer, QUuid::createUuid()));
    QVERIFY(session.canPlaceLayer(child, outer));
    session.toggleGroupExpansion(outer);
    QCOMPARE(rowIDs(session), std::vector<QUuid>{outer});
    QCOMPARE(session.activeLayerID(), std::optional(outer));
    session.toggleGroupExpansion(outer);
    std::vector<int> depths;
    for (const LayerHierarchy::Entry &row : session.layerRows())
        depths.push_back(row.depth);
    QCOMPARE(depths, (std::vector<int>{0, 1, 2}));
    session.selectLayer(child);
    session.moveActiveLayerOutOfGroup();
    QCOMPARE(session.activeLayer().value().parentID, std::optional(outer));
    QCOMPARE(session.history.undoName(), QString("Move Layer"));
    session.undo();
    QCOMPARE(parentOf(session, child), std::optional(inner));
    session.selectLayer(outer);
    session.deleteActiveLayer();
    QVERIFY(session.document().value().layers.empty());
    session.undo();
    QCOMPARE(int(session.document().value().layers.size()), 3);
    QCOMPARE(parentOf(session, child), std::optional(inner));
}

void GroupSessionTests::layersArePlacedAboveBelowAndInsideFolders()
{
    EditorSession session;
    session.createDocument(100, 100);
    session.addBlankLayer();
    const QUuid bottom = session.activeLayerID().value();
    session.addBlankLayer();
    const QUuid middle = session.activeLayerID().value();
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    session.addBlankLayer();
    const QUuid inside = session.activeLayerID().value();
    // To the top of the root, then to its bottom.
    QVERIFY(session.placeLayer(bottom, std::nullopt));
    QCOMPARE(session.document().value().layers.back().id, bottom);
    QCOMPARE(session.activeLayerID(), std::optional(bottom));
    QVERIFY(session.placeLayer(bottom, std::nullopt, std::nullopt, true));
    QCOMPARE(session.document().value().layers.front().id, bottom);
    // Just above a sibling; the target shares the new parent.
    QVERIFY(session.placeLayer(bottom, std::nullopt, middle));
    QCOMPARE(rowIDs(session), (std::vector<QUuid>{folder, inside, bottom, middle}));
    QVERIFY(!session.placeLayer(bottom, std::nullopt, inside));
    // A target beats the bottom.
    QVERIFY(session.placeLayer(bottom, std::nullopt, middle, true));
    QCOMPARE(rowIDs(session), (std::vector<QUuid>{folder, inside, bottom, middle}));
    QVERIFY(!session.placeLayer(bottom, std::nullopt, bottom));
    QVERIFY(!session.placeLayer(bottom, std::nullopt, QUuid::createUuid()));
    // Into a folded folder, which opens, above a layer inside.
    session.selectLayer(middle);
    session.toggleGroupExpansion(folder);
    QVERIFY(session.placeLayer(bottom, folder, inside));
    QVERIFY(!session.collapsedGroupIDs().contains(folder));
    QCOMPARE(parentOf(session, bottom), std::optional(folder));
    QCOMPARE(rowIDs(session), (std::vector<QUuid>{folder, bottom, inside, middle}));
    // Only folders take layers; strangers and busy sessions move none.
    QVERIFY(!session.placeLayer(bottom, middle));
    QVERIFY(!session.canPlaceLayer(bottom, middle));
    QVERIFY(!session.placeLayer(QUuid::createUuid(), std::nullopt));
    QVERIFY(!session.placeLayer(bottom, QUuid::createUuid()));
    QVERIFY(session.canPlaceLayer(bottom, std::nullopt) && session.canPlaceLayer(bottom, folder));
    session.setIsImporting(true);
    QVERIFY(!session.canPlaceLayer(bottom, std::nullopt));
    QVERIFY(!session.placeLayer(bottom, std::nullopt));
    // Outside a folder there is no group to leave.
    session.setIsImporting(false);
    session.selectLayer(middle);
    const int count = session.history.undoCount();
    session.moveActiveLayerOutOfGroup();
    QCOMPARE(session.history.undoCount(), count);
    // A layer leaves its folder to sit just above it.
    session.selectLayer(std::nullopt);
    session.addBlankLayer();
    const QUuid top = session.activeLayerID().value();
    session.selectLayer(inside);
    session.moveActiveLayerOutOfGroup();
    QCOMPARE(rowIDs(session), (std::vector<QUuid>{top, inside, folder, bottom, middle}));
}

void GroupSessionTests::placingRespectsTheNestingLimit()
{
    EditorSession session;
    session.createDocument(8, 8);
    // Each new folder goes inside the active one: 64 deep.
    for (int depth = 0; depth < 64; ++depth)
        session.addGroup();
    const QUuid deepest = session.activeLayerID().value();
    QCOMPARE(int(session.document().value().layers.size()), 64);
    session.selectLayer(std::nullopt);
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    session.selectLayer(std::nullopt);
    session.addBlankLayer();
    const QUuid leaf = session.activeLayerID().value();
    const std::optional<CanvasDocument> before = session.document();
    // A folder may go no deeper; a layer still may.
    QVERIFY(session.canPlaceLayer(folder, deepest));
    QVERIFY(!session.placeLayer(folder, deepest));
    QCOMPARE(session.document(), before);
    QVERIFY(session.placeLayer(leaf, deepest));
    QCOMPARE(parentOf(session, leaf), std::optional(deepest));
}

void GroupSessionTests::hiddenParentOverridesChildrenAndExportOrderFollowsGroups()
{
    EditorSession session;
    session.createDocument(64, 32);
    session.addGroup();
    const QUuid group = session.activeLayerID().value();
    QTemporaryDir folder;
    QImage fixture(64, 32, QImage::Format_RGBA8888);
    fixture.fill(Qt::red);
    QVERIFY(fixture.save(folder.filePath("fixture.png"), "PNG"));
    bool done = false;
    session.importImages({QUrl::fromLocalFile(folder.filePath("fixture.png"))}, std::nullopt, [&] { done = true; });
    QVERIFY(QTest::qWaitFor([&] { return done; }, 10'000));
    const QUuid child = session.activeLayerID().value();
    QCOMPARE(session.activeLayer().value().parentID, std::optional(group));
    const ImageIdentity source = session.activeLayer().value().asset.value().identity();
    session.toggleLayerVisibility(group);
    QVERIFY(session.activeLayer().value().isVisible);
    QVERIFY(session.document().value().renderLayers().empty());
    QVERIFY(!session.canTransform());
    // A hidden folder exports nothing of what it holds.
    const QImage hiddenExport = QImage::fromData(ImageExporter::pngData(session.projectSnapshot().value()), "PNG");
    QCOMPARE(hiddenExport.size(), QSize(64, 32));
    QCOMPARE(hiddenExport.pixelColor(0, 0).alpha(), 0);
    session.toggleLayerVisibility(group);
    QCOMPARE(renderIDs(session), std::vector<QUuid>{child});
    QCOMPARE(QImage::fromData(ImageExporter::pngData(session.projectSnapshot().value()), "PNG").pixelColor(0, 0), QColor(255, 0, 0));
    QVERIFY(session.activeLayer().value().asset.value().identity() == source);
    session.selectLayer(std::nullopt);
    session.addGroup();
    const QUuid other = session.activeLayerID().value();
    session.addBlankLayer();
    const QUuid otherChild = session.activeLayerID().value();
    QCOMPARE(renderIDs(session), (std::vector<QUuid>{child, otherChild}));
    session.selectLayer(other);
    session.moveActiveLayer(-1);
    QCOMPARE(renderIDs(session), (std::vector<QUuid>{otherChild, child}));
    QCOMPARE(parentOf(session, otherChild), std::optional(other));
}

void GroupSessionTests::groupsRoundTripAndSurviveImageAndCanvasResize()
{
    EditorSession session;
    session.createDocument(20, 10);
    session.addGroup();
    const QUuid group = session.activeLayerID().value();
    session.renameLayer(group, "Artwork");
    session.addBlankLayer();
    const QUuid child = session.activeLayerID().value();
    const ProjectSnapshot snapshot = session.projectSnapshot().value();
    QCOMPARE(snapshot.manifest.version, qint64(7));
    const auto record = [](const ProjectSnapshot &from, QUuid id) {
        for (const ProjectLayerRecord &layer : from.manifest.layers) {
            if (layer.id == id)
                return layer;
        }
        throw std::runtime_error("no such record");
    };
    QTemporaryDir folder;
    const QString path = folder.filePath("Groups.comp");
    ProjectStore::save(snapshot, path);
    const ProjectSnapshot loaded = ProjectStore::load(path);
    QCOMPARE(record(loaded, group).isGroup, std::optional(true));
    QCOMPARE(record(loaded, group).name, QString("Artwork"));
    QCOMPARE(record(loaded, child).parentID, std::optional(group));
    const ProjectSnapshot resized = ImageResizer::resize(loaded, {.width = 40, .height = 20, .resolution = 72});
    const ProjectSnapshot cropped = CanvasResizer::resize(resized, {.width = 30, .height = 15});
    QCOMPARE(record(cropped, child).parentID, std::optional(group));
    QCOMPARE(record(cropped, group).isGroup, std::optional(true));
    // A version 1 project keeps its version through the store.
    ProjectSnapshot legacy{.manifest = {.documentID = QUuid::createUuid(), .width = 20, .height = 10, .activeLayerID = std::nullopt, .layers = {}},
                           .images = {}};
    legacy.manifest.version = 1;
    ProjectStore::save(legacy, path);
    QCOMPARE(ProjectStore::load(path).manifest.version, qint64(1));
}

void GroupSessionTests::rowsOfASessionWithoutADocument()
{
    QVERIFY(EditorSession().layerRows().empty());
}

QTEST_GUILESS_MAIN(GroupSessionTests)
#include "GroupSessionTests.moc"
