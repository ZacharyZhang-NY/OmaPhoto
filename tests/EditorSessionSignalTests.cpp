#include "SessionRecord.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

// `changed()` stands in for Swift's observation: its contract.
namespace {
ImportedImage asset(int width, int height, const QString &name)
{
    return ImportedImage(QImage(width, height, QImage::Format_RGBA8888_Premultiplied), QImage(), name);
}

}

class EditorSessionSignalTests : public QObject {
    Q_OBJECT
private slots:
    void everyChangeIsAnnouncedAndRefusalsAreSilent();
    void observersSeeFinishedStates();
    void groupingAndPlacingAnnounceAndRefuseInSilence();
    void transformsAnnounceAndRefuseInSilence();
    void appearanceAnnouncesAndRefusesInSilence();
    void theLastSignalOfEveryChangeSeesWhatItLeaves();
    void theLastSignalOfAnImportAndOfALongWaitSeeTheirResults();
    void projectsAnnounceAndTheirLastSignalSeesTheResult();
};

void EditorSessionSignalTests::everyChangeIsAnnouncedAndRefusalsAreSilent()
{
    EditorSession session;
    QSignalSpy changes(&session, &EditorSession::changed);
    const auto announced = [&](const std::function<void()> &change) {
        const qsizetype before = changes.count();
        change();
        return changes.count() > before;
    };
    // Nothing to act on yet: requests are refused in silence.
    QVERIFY(!announced([&] { session.addBlankLayer(); }));
    QVERIFY(!announced([&] { session.addGroup(); }));
    QVERIFY(!announced([&] { session.fit(); }));
    QVERIFY(!announced([&] { session.zoom(2); }));
    QVERIFY(!announced([&] { session.undo(); }));
    QVERIFY(announced([&] { session.createDocument(64, 64); }));
    QVERIFY(announced([&] { session.addBlankLayer(); }));
    const QUuid id = session.activeLayerID().value();
    QVERIFY(announced([&] { session.renameLayer(id, "Named"); }));
    QVERIFY(announced([&] { session.toggleLayerVisibility(id); }));
    QVERIFY(announced([&] { session.setVisibilityInSwipe(id, true); }));
    QVERIFY(!announced([&] { session.setVisibilityInSwipe(id, true); }));
    QVERIFY(announced([&] { session.addGroup(); }));
    QVERIFY(announced([&] { session.toggleGroupExpansion(session.activeLayerID().value()); }));
    QVERIFY(!announced([&] { session.toggleGroupExpansion(id); }));
    QVERIFY(announced([&] { session.selectLayer(id); }));
    QVERIFY(announced([&] { session.selectTool(NavigationTool::hand); }));
    QVERIFY(announced([&] { session.zoom(2); }));
    // A zoom the viewport refuses or already has says nothing.
    QVERIFY(!announced([&] { session.zoom(std::nan("")); }));
    QVERIFY(!announced([&] { session.zoom(2); }));
    QVERIFY(announced([&] { session.fit(); }));
    QVERIFY(!announced([&] { session.fit(); }));
    // An open transaction closes undo: observers hear both ends.
    QVERIFY(session.canUndo());
    QVERIFY(announced([&] { session.beginEdit("Gesture"); }));
    QVERIFY(!session.canUndo());
    QVERIFY(announced([&] { session.endEdit(); }));
    QVERIFY(session.canUndo());
    QVERIFY(announced([&] { session.undo(); }));
    QVERIFY(announced([&] { session.redo(); }));
    session.selectLayer(id);
    QVERIFY(announced([&] { session.insert(asset(4, 4, "Image")); }));
    QVERIFY(announced([&] { session.moveActiveLayer(-1); }));
    QVERIFY(announced([&] { session.reorderLayers({0}, 2); }));
    QVERIFY(announced([&] { session.deleteActiveLayer(); }));
    QVERIFY(announced([&] { session.setIsProjectBusy(true); }));
    QVERIFY(!announced([&] { session.selectTool(NavigationTool::zoom); }));
    QVERIFY(announced([&] { session.setIsProjectBusy(false); }));
    QVERIFY(announced([&] { session.setShowsNewDocument(true); }));
    QVERIFY(announced([&] { session.setShowsImporter(true); }));
    QVERIFY(announced([&] { session.setShowsPixelGrid(false); }));
    QVERIFY(announced([&] { session.requestCanvasFocus(); }));
    QVERIFY(announced([&] { session.setIsImporting(true); }));
    QVERIFY(announced([&] { session.setImportError(QString("failed")); }));
    QVERIFY(announced([&] { session.setRenamingLayerID(id); }));
    QVERIFY(announced([&] { session.notify(); }));
}

void EditorSessionSignalTests::observersSeeFinishedStates()
{
    EditorSession session;
    session.viewport.resize(QSizeF(400, 300), 1, std::nullopt);
    // What observers read at the last signal of a change.
    QSizeF size;
    bool fits = false;
    std::optional<QUuid> renaming;
    connect(&session, &EditorSession::changed, this, [&] {
        size = session.document().has_value() ? session.document().value().size() : QSizeF();
        fits = session.viewport.followsFit();
        renaming = session.renamingLayerID();
    });
    session.createDocument(64, 64);
    session.zoom(3);
    QVERIFY(!fits);
    session.setRenamingLayerID(QUuid::createUuid());
    session.createDocument(32, 16, true);
    QCOMPARE(size, QSizeF(32, 16));
    QVERIFY(fits && !renaming.has_value());
    session.zoom(3);
    // Another canvas comes back: the view fits before that signal.
    session.undo();
    QCOMPARE(size, QSizeF(64, 64));
    QVERIFY(fits);
}

void EditorSessionSignalTests::groupingAndPlacingAnnounceAndRefuseInSilence()
{
    EditorSession session;
    QSignalSpy changes(&session, &EditorSession::changed);
    const auto announced = [&](const std::function<void()> &change) {
        const qsizetype before = changes.count();
        change();
        return changes.count() > before;
    };
    // Without a document nothing is grouped, placed or deleted.
    QVERIFY(!announced([&] { session.groupSelectedLayers(); }));
    QVERIFY(!announced([&] { session.extendSelection(QUuid::createUuid()); }));
    QVERIFY(!announced([&] { session.placeLayer(QUuid::createUuid(), std::nullopt); }));
    QVERIFY(!announced([&] { session.moveActiveLayerOutOfGroup(); }));
    QVERIFY(!announced([&] { session.deleteSelectedLayers(); }));
    QVERIFY(!announced([&] { session.finishDeletingLayers({}, {}); }));
    session.createDocument(64, 64, true);
    const QUuid first = session.activeLayerID().value();
    session.addBlankLayer();
    const QUuid second = session.activeLayerID().value();
    QVERIFY(announced([&] { session.selectLayers({first, second}, first); }));
    QVERIFY(announced([&] { session.extendSelection(second); }));
    QVERIFY(!announced([&] { session.extendSelection(QUuid::createUuid()); }));
    QVERIFY(announced([&] { session.groupSelectedLayers(); }));
    const QUuid folder = session.activeLayerID().value();
    // A folder takes neither itself nor a layer above itself.
    QVERIFY(!announced([&] { session.placeLayer(folder, folder); }));
    QVERIFY(!announced([&] { session.placeLayer(second, std::nullopt, second); }));
    QVERIFY(!announced([&] { session.moveActiveLayerOutOfGroup(); }));
    QVERIFY(announced([&] { session.placeLayer(second, folder); }));
    QVERIFY(announced([&] { session.moveActiveLayerOutOfGroup(); }));
    session.selectLayers({first, second}, first);
    session.setIsImporting(true);
    QVERIFY(!announced([&] { session.deleteSelectedLayers(); }));
    QVERIFY(!announced([&] { session.groupSelectedLayers(); }));
    QVERIFY(!announced([&] { session.extendSelection(second); }));
    session.setIsImporting(false);
    QVERIFY(announced([&] { session.deleteSelectedLayers(); }));
    QCOMPARE(int(session.document().value().layers.size()), 1);
}

void EditorSessionSignalTests::transformsAnnounceAndRefuseInSilence()
{
    EditorSession session;
    QSignalSpy changes(&session, &EditorSession::changed);
    const auto announced = [&](const std::function<void()> &change) {
        const qsizetype before = changes.count();
        change();
        return changes.count() > before;
    };
    const LayerTransform valid{.origin = {1, 2}, .size = {3, 4}};
    // No canvas, no pixels, no edit: nothing to say.
    QVERIFY(!announced([&] { session.beginTransform(); }));
    QVERIFY(!announced([&] { session.nudgeLayer(1, 1); }));
    session.createDocument(64, 64, true);
    QVERIFY(!announced([&] { session.beginTransform(); }));
    QVERIFY(!announced([&] { session.previewTransform(valid); }));
    QVERIFY(!announced([&] { session.commitTransform(); }));
    QVERIFY(!announced([&] { session.cancelTransform(); }));
    session.insert(asset(8, 8, "Image"));
    QVERIFY(announced([&] { session.beginTransform(); }));
    QVERIFY(!announced([&] { session.beginTransform(); }));
    QVERIFY(announced([&] { session.previewTransform(valid); }));
    LayerTransform invalid = valid;
    invalid.size.setHeight(0);
    QVERIFY(!announced([&] { session.previewTransform(invalid); }));
    QVERIFY(announced([&] { session.cancelTransform(); }));
    QVERIFY(announced([&] { session.beginTransform(); }));
    // An edit that changes nothing still ends aloud.
    QVERIFY(announced([&] { session.commitTransform(); }));
    QVERIFY(announced([&] { session.nudgeLayer(1, 0); }));
    // Flips, merges and duplicate transforms speak when they act.
    QVERIFY(announced([&] { session.flipLayers(true); }));
    QVERIFY(announced([&] { session.setTransformAutoSelect(true); }));
    QVERIFY(announced([&] { session.setShowsTransformControls(false); }));
    QVERIFY(announced([&] { session.setLocksTransformRatio(false); }));
    QVERIFY(announced([&] { session.beginDuplicateTransform(); }));
    QVERIFY(!announced([&] { session.beginDuplicateTransform(); }));
    QVERIFY(announced([&] { session.cancelTransform(); }));
    QVERIFY(announced([&] { session.beginDuplicateTransform(); }));
    QVERIFY(announced([&] { session.commitTransform(); }));
    QVERIFY(announced([&] { session.mergeLayers(); }));
    session.setIsProjectBusy(true);
    QVERIFY(!announced([&] { session.flipLayers(false); }));
    QVERIFY(!announced([&] { session.mergeLayers(); }));
    QVERIFY(!announced([&] { session.beginDuplicateTransform(); }));
}

void EditorSessionSignalTests::appearanceAnnouncesAndRefusesInSilence()
{
    EditorSession session;
    QSignalSpy changes(&session, &EditorSession::changed);
    const auto announced = [&](const std::function<void()> &change) {
        const qsizetype before = changes.count();
        change();
        return changes.count() > before;
    };
    // No layer, no appearance: nothing to say.
    QVERIFY(!announced([&] { session.setLayerOpacity(0.5); }));
    QVERIFY(!announced([&] { session.setSelectedLayersOpacity(0.5); }));
    QVERIFY(!announced([&] { session.setLayerBlendMode(LayerBlendMode::screen); }));
    QVERIFY(!announced([&] { session.cycleBlendMode(true); }));
    QVERIFY(!announced([&] { session.beginOpacityEdit(); }));
    QVERIFY(!announced([&] { session.finishOpacityEdit(); }));
    QVERIFY(!announced([&] { session.previewBlendMode(LayerBlendMode::screen, QUuid::createUuid()); }));
    session.createDocument(8, 8);
    session.insert(asset(4, 4, "Image"));
    const QUuid id = session.activeLayerID().value();
    QVERIFY(announced([&] { session.previewBlendMode(LayerBlendMode::screen, id); }));
    QVERIFY(!announced([&] { session.previewBlendMode(LayerBlendMode::screen, id); }));
    QVERIFY(announced([&] { session.previewBlendMode(LayerBlendMode::overlay, id); }));
    QVERIFY(announced([&] { session.previewBlendMode(std::nullopt, std::nullopt); }));
    QVERIFY(!announced([&] { session.previewBlendMode(std::nullopt, std::nullopt); }));
    QVERIFY(announced([&] { session.beginOpacityEdit(); }));
    QVERIFY(!announced([&] { session.beginOpacityEdit(); }));
    QVERIFY(announced([&] { session.setLayerOpacity(0.5); }));
    QVERIFY(!announced([&] { session.setLayerOpacity(std::nan("")); }));
    QVERIFY(announced([&] { session.finishOpacityEdit(); }));
    QVERIFY(!announced([&] { session.finishOpacityEdit(); }));
    QVERIFY(announced([&] { session.setLayerOpacity(0.25); }));
    QVERIFY(announced([&] { session.setSelectedLayersOpacity(0.75); }));
    QVERIFY(!announced([&] { session.setSelectedLayersOpacity(0.75); }));
    QVERIFY(announced([&] { session.cycleBlendMode(false); }));
    QVERIFY(announced([&] { session.setLayerBlendMode(LayerBlendMode::normal); }));
    // A commit with only a preview open drops it aloud.
    session.previewBlendMode(LayerBlendMode::screen, id);
    bool sawItCleared = false;
    const QMetaObject::Connection watch = connect(&session, &EditorSession::changed, this, [&] { sawItCleared = !session.blendPreview().has_value(); });
    QVERIFY(announced([&] { session.commitTransform(); }));
    QVERIFY(sawItCleared);
    QVERIFY(!announced([&] { session.commitTransform(); }));
    disconnect(watch);
    // A refused mode still drops a preview, aloud.
    session.previewBlendMode(LayerBlendMode::screen, id);
    session.setIsImporting(true);
    QVERIFY(announced([&] { session.setLayerBlendMode(LayerBlendMode::screen); }));
    QVERIFY(!announced([&] { session.setLayerBlendMode(LayerBlendMode::screen); }));
}

void EditorSessionSignalTests::theLastSignalOfEveryChangeSeesWhatItLeaves()
{
    EditorSession session;
    session.viewport.resize(QSizeF(400, 300), 1, std::nullopt);
    QStringList seen;
    connect(&session, &EditorSession::changed, this, [&] { seen = described(session); });
    // Empty when the last signal saw the state left behind.
    const auto stale = [&](const std::function<void()> &change) {
        seen.clear();
        change();
        const QStringList left = described(session);
        return seen == left ? QString() : seen.join("; ") + " != " + left.join("; ");
    };
    QCOMPARE(stale([&] { session.setShowsNewDocument(true); }), QString());
    QCOMPARE(stale([&] { session.createDocument(64, 64, true); }), QString());
    const QUuid first = session.activeLayerID().value();
    QCOMPARE(stale([&] { session.addBlankLayer(); }), QString());
    const QUuid layer = session.activeLayerID().value();
    QCOMPARE(stale([&] { session.renameLayer(layer, "Named"); }), QString());
    QCOMPARE(stale([&] { session.toggleLayerVisibility(layer); }), QString());
    QCOMPARE(stale([&] { QCOMPARE(session.beginVisibilitySwipe(layer), std::optional(true)); }), QString());
    QCOMPARE(stale([&] { session.setVisibilityInSwipe(first, false); }), QString());
    QCOMPARE(stale([&] { session.endVisibilitySwipe(); }), QString());
    QCOMPARE(stale([&] { session.addGroup(); }), QString());
    const QUuid folder = session.activeLayerID().value();
    QCOMPARE(stale([&] { session.addBlankLayer(); }), QString());
    const QUuid inside = session.activeLayerID().value();
    // Folding moves the active layer out of what hides.
    QCOMPARE(stale([&] { session.toggleGroupExpansion(folder); }), QString());
    QCOMPARE(session.activeLayerID(), std::optional(folder));
    // A folder made inside a folded one opens it.
    QCOMPARE(stale([&] { session.addGroup(); }), QString());
    const QUuid nested = session.activeLayerID().value();
    QVERIFY(session.collapsedGroupIDs().isEmpty());
    QCOMPARE(stale([&] { session.toggleGroupExpansion(folder); }), QString());
    QCOMPARE(stale([&] { session.toggleGroupExpansion(folder); }), QString());
    QCOMPARE(stale([&] { session.selectLayer(layer); }), QString());
    QCOMPARE(stale([&] { session.selectTool(NavigationTool::hand); }), QString());
    QCOMPARE(stale([&] { session.zoom(2); }), QString());
    QCOMPARE(stale([&] { session.fit(); }), QString());
    // An open transaction closes undo until it ends.
    QCOMPARE(stale([&] { session.beginEdit("Gesture"); }), QString());
    QVERIFY(seen.contains("history 00"));
    QCOMPARE(stale([&] { session.endEdit(); }), QString());
    QVERIFY(seen.contains("history 10"));
    QCOMPARE(stale([&] { session.undo(); }), QString());
    QVERIFY(seen.contains("history 11"));
    QCOMPARE(stale([&] { session.redo(); }), QString());
    // Beside a root layer the image has a sibling below.
    QCOMPARE(stale([&] { session.selectLayer(layer); }), QString());
    QCOMPARE(stale([&] { session.insert(asset(4, 4, "Image")); }), QString());
    QCOMPARE(stale([&] { session.moveActiveLayer(-1); }), QString());
    QCOMPARE(stale([&] { session.reorderLayers({0}, 2); }), QString());
    QCOMPARE(stale([&] { session.deleteActiveLayer(); }), QString());
    // An appearance: tried on, dragged, set, cycled.
    QCOMPARE(stale([&] { session.selectLayer(layer); }), QString());
    QCOMPARE(stale([&] { session.previewBlendMode(LayerBlendMode::screen, layer); }), QString());
    QVERIFY(seen.filter("preview ").size() == 1);
    QCOMPARE(stale([&] { session.beginOpacityEdit(); }), QString());
    QVERIFY(seen.contains("history 00"));
    QCOMPARE(stale([&] { session.setLayerOpacity(0.5); }), QString());
    QCOMPARE(stale([&] { session.finishOpacityEdit(); }), QString());
    QVERIFY(seen.contains("history 10") && seen.contains("names Layer Opacity/"));
    QCOMPARE(stale([&] { session.setLayerBlendMode(LayerBlendMode::multiply); }), QString());
    QVERIFY(seen.filter("preview ").isEmpty());
    QCOMPARE(stale([&] { session.cycleBlendMode(true); }), QString());
    QCOMPARE(stale([&] { session.setLayerOpacity(0.25); }), QString());
    QCOMPARE(stale([&] { session.setSelectedLayersOpacity(1); }), QString());
    // Choosing another layer ends a drag and a preview.
    QCOMPARE(stale([&] { session.beginOpacityEdit(); }), QString());
    QCOMPARE(stale([&] { session.previewBlendMode(LayerBlendMode::screen, layer); }), QString());
    QCOMPARE(stale([&] { session.selectLayer(first); }), QString());
    QVERIFY(seen.contains("history 10") && seen.filter("preview ").isEmpty());
    // A transform: begun, previewed, dropped, committed, nudged.
    QCOMPARE(stale([&] { session.insert(asset(6, 4, "Moved")); }), QString());
    const QUuid movedLayer = session.activeLayerID().value();
    QCOMPARE(stale([&] { session.selectTool(NavigationTool::hand); }), QString());
    QCOMPARE(stale([&] { session.beginTransform(); }), QString());
    QVERIFY(seen.contains("tool 0") && seen.contains("history 00"));
    const LayerTransform draft{.origin = {5, 7}, .size = {12, 8}};
    QCOMPARE(stale([&] { session.previewTransform(draft); }), QString());
    QCOMPARE(stale([&] { session.cancelTransform(); }), QString());
    QCOMPARE(stale([&] { session.beginTransform(); }), QString());
    QCOMPARE(stale([&] { session.previewTransform(draft); }), QString());
    QCOMPARE(stale([&] { session.commitTransform(); }), QString());
    QVERIFY(seen.contains("history 10") && seen.contains("names Transform Layer/"));
    QCOMPARE(stale([&] { session.nudgeLayer(2, 3); }), QString());
    // Another layer, tool or selection commits the open edit.
    QCOMPARE(stale([&] { session.beginTransform(); }), QString());
    QCOMPARE(stale([&] { session.nudgeLayer(1, 1); }), QString());
    QCOMPARE(stale([&] { session.selectLayer(layer); }), QString());
    QCOMPARE(stale([&] { session.selectLayers({movedLayer, layer}, movedLayer); }), QString());
    QCOMPARE(stale([&] { session.beginTransform(); }), QString());
    QVERIFY(seen.filter("group").size() == 1);
    QCOMPARE(stale([&] { session.nudgeLayer(1, 1); }), QString());
    QCOMPARE(stale([&] { session.selectTool(NavigationTool::hand); }), QString());
    QCOMPARE(stale([&] { session.selectLayer(movedLayer); }), QString());
    QCOMPARE(stale([&] { session.deleteActiveLayer(); }), QString());
    // Several layers: chosen, wrapped, placed, taken out, deleted.
    QCOMPARE(stale([&] { session.selectLayers({first, layer}, first); }), QString());
    QCOMPARE(stale([&] { session.extendSelection(folder); }), QString());
    QCOMPARE(stale([&] { session.extendSelection(folder); }), QString());
    QCOMPARE(stale([&] { session.groupSelectedLayers(); }), QString());
    const QUuid wrapper = session.activeLayerID().value();
    // Wrapping inside a folded folder opens it, as placing does.
    QCOMPARE(stale([&] { session.toggleGroupExpansion(folder); }), QString());
    QCOMPARE(stale([&] { session.selectLayers({inside, nested}, inside); }), QString());
    QCOMPARE(stale([&] { session.groupSelectedLayers(); }), QString());
    QVERIFY(session.collapsedGroupIDs().isEmpty());
    QCOMPARE(stale([&] { session.toggleGroupExpansion(folder); }), QString());
    QCOMPARE(stale([&] { QVERIFY(session.placeLayer(wrapper, folder)); }), QString());
    QVERIFY(session.collapsedGroupIDs().isEmpty());
    QCOMPARE(stale([&] { session.moveActiveLayerOutOfGroup(); }), QString());
    QCOMPARE(stale([&] { session.selectLayers({first, layer}, layer); }), QString());
    QCOMPARE(stale([&] { session.deleteSelectedLayers(); }), QString());
    QCOMPARE(stale([&] { session.finishDeletingLayers({wrapper, folder}, {}); }), QString());
    QCOMPARE(stale([&] { session.setIsProjectBusy(true); }), QString());
    QCOMPARE(stale([&] { session.setIsProjectBusy(false); }), QString());
    QCOMPARE(stale([&] { session.setShowsImporter(true); }), QString());
    QCOMPARE(stale([&] { session.setShowsImporter(false); }), QString());
    QCOMPARE(stale([&] { session.setShowsPixelGrid(false); }), QString());
    QCOMPARE(stale([&] { session.requestCanvasFocus(); }), QString());
    QCOMPARE(stale([&] { session.setIsImporting(true); }), QString());
    QCOMPARE(stale([&] { session.setIsImporting(false); }), QString());
    QCOMPARE(stale([&] { session.setImportError(QString("failed")); }), QString());
    QCOMPARE(stale([&] { session.setImportError(std::nullopt); }), QString());
    QCOMPARE(stale([&] { session.setRenamingLayerID(layer); }), QString());
    QCOMPARE(stale([&] { session.setRenamingLayerID(std::nullopt); }), QString());
    // Another canvas, then back: the view fits before the signal.
    QCOMPARE(stale([&] { session.zoom(3); }), QString());
    QCOMPARE(stale([&] { session.createDocument(32, 16); }), QString());
    QCOMPARE(stale([&] { session.zoom(3); }), QString());
    QCOMPARE(stale([&] { session.undo(); }), QString());
    QVERIFY(session.viewport.followsFit());
}

void EditorSessionSignalTests::theLastSignalOfAnImportAndOfALongWaitSeeTheirResults()
{
    QTemporaryDir folder;
    QImage image(16, 8, QImage::Format_RGBA8888);
    image.fill(Qt::blue);
    QVERIFY(image.save(folder.filePath("blue.png"), "PNG"));
    const QList<QUrl> urls{QUrl::fromLocalFile(folder.filePath("blue.png")), QUrl::fromLocalFile(folder.filePath("none.png"))};
    EditorSession session;
    QStringList seen;
    connect(&session, &EditorSession::changed, this, [&] { seen = described(session); });
    bool done = false;
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^cannot import .*none\\.png: .*"));
    session.importImages(urls, std::nullopt, [&] { done = true; });
    QVERIFY(seen.contains("flags 0001"));
    QVERIFY(QTest::qWaitFor([&] { return done && !session.isImporting(); }, 10'000));
    // The error shows last, with the import already over.
    QCOMPARE(seen, described(session));
    QVERIFY(seen.contains("flags 0000") && seen.filter("error none.png: ").size() == 1);
    // The indicator comes on by itself, and says so.
    session.setImportError(std::nullopt);
    session.setIsProjectBusy(true);
    QVERIFY(seen.contains("dimmed 0 startable 0"));
    QVERIFY(QTest::qWaitFor([&] { return session.showsBusy(); }, 5'000));
    QCOMPARE(seen, described(session));
    QVERIFY(seen.contains("dimmed 1 startable 0"));
    session.setIsProjectBusy(false);
    QCOMPARE(seen, described(session));
    QVERIFY(seen.contains("dimmed 0 startable 1"));
}

void EditorSessionSignalTests::projectsAnnounceAndTheirLastSignalSeesTheResult()
{
    EditorSession session;
    session.viewport.resize(QSizeF(400, 300), 1, std::nullopt);
    QStringList seen;
    connect(&session, &EditorSession::changed, this, [&] { seen = described(session); });
    const auto stale = [&](const std::function<void()> &change) {
        seen.clear();
        change();
        const QStringList left = described(session);
        return seen == left ? QString() : seen.join("; ") + " != " + left.join("; ");
    };
    QCOMPARE(stale([&] { session.createNewProject(64, 48); }), QString());
    QCOMPARE(stale([&] { session.insert(asset(8, 8, "Image")); }), QString());
    QCOMPARE(stale([&] { session.zoom(3); }), QString());
    const ProjectSnapshot snapshot = session.projectSnapshot().value();
    // A canvas half as large under the same id.
    ProjectSnapshot smaller = snapshot;
    smaller.manifest.width = 32;
    smaller.manifest.height = 24;
    smaller.manifest.resolution = 144;
    QCOMPARE(stale([&] { session.applyDocumentSize(smaller, "Canvas Size"); }), QString());
    QVERIFY(seen.filter("canvas ").value(0).endsWith(" 32x24 at 144") && seen.contains("names Canvas Size/"));
    QCOMPARE(stale([&] { session.applyImageSize(snapshot); }), QString());
    QCOMPARE(stale([&] { session.setRenamingLayerID(QUuid::createUuid()); }), QString());
    QCOMPARE(stale([&] { session.zoom(3); }), QString());
    QCOMPARE(stale([&] { session.installProject(smaller, "/projects/small.comp"); }), QString());
    QVERIFY(seen.contains("path /projects/small.comp") && seen.contains("renaming none") && seen.contains("undos 0 modified 0"));
    QCOMPARE(stale([&] { session.clearProject(); }), QString());
    QVERIFY(seen.contains("path none") && seen.contains("active none") && seen.filter("canvas ").isEmpty());
    // A refused project or size says nothing.
    seen.clear();
    session.createNewProject(0, 10);
    session.applyImageSize(snapshot);
    QVERIFY(seen.isEmpty());
}

QTEST_GUILESS_MAIN(EditorSessionSignalTests)
#include "EditorSessionSignalTests.moc"
