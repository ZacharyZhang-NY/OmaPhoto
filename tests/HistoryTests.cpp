#include "Document/DocumentHistory.h"
#include "Document/EditorSession.h"
#include <QtTest>
#include <functional>

namespace {
ImportedImage asset(int width, int height, const QString &name)
{
    QImage image(width, height, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::red);
    QImage thumbnail(8, 8, QImage::Format_RGBA8888_Premultiplied);
    thumbnail.fill(Qt::red);
    return ImportedImage(image, thumbnail, name);
}

std::optional<CanvasDocument> documentNamed(const QString &layerName)
{
    CanvasDocument document(64, 32);
    document.layers.push_back(ImageLayer(layerName, document.size()));
    return document;
}
}

class HistoryTests : public QObject {
    Q_OBJECT
private slots:
    void undoAndRedoReturnTheRecordedSnapshots();
    void nestedTransactionsRecordOneEntry();
    void noOpEditsPreserveRedo();
    void aNewEditClearsRedo();
    void savedRevisionFollowsUndoAndRedo();
    void endWithoutBeginIsIgnored();
    void resetClearsEverything();
    void historyBoundsEntriesAndUniqueRetainedPixels();
    void retainedBytesCountEachHistoryOnlyImageOnce();
    void byteLimitDropsTheOldestPastEntryFirst();
    void undoTrimsRedoWhenItsPixelsExceedTheLimit();
    void undoTrimsThePastBeforeTheRedoStack();
    void negativeLimitsBecomeZero();
    void everyLayerEditRoundTripsWithSelection();
    void navigationNoOpsAndSaveRevisionPreserveHistory();
    void replacementCanvasAndNestedTransactionsUndoAsOne();
    void historyBlockedDuringImportsAndDialogs();
};

void HistoryTests::undoAndRedoReturnTheRecordedSnapshots()
{
    DocumentHistory history;
    const std::optional<CanvasDocument> before = documentNamed("Before");
    std::optional<CanvasDocument> after = before;
    after->layers[0].name = "After";
    const QUuid selection = before->layers[0].id;
    history.begin("Rename Layer", before, std::nullopt);
    history.end(after, selection);
    QCOMPARE(history.undoCount(), 1);
    QCOMPARE(history.undoName(), QString("Rename Layer"));
    QCOMPARE(history.redoName(), QString());
    QVERIFY(history.canUndo() && !history.canRedo());

    const DocumentHistory::Snapshot undone = history.undo().value();
    QVERIFY(undone.document == before);
    QCOMPARE(undone.activeLayerID, std::nullopt);
    QCOMPARE(history.undoName(), QString());
    QCOMPARE(history.redoName(), QString("Rename Layer"));
    QVERIFY(!history.canUndo() && history.canRedo());
    QVERIFY(!history.undo().has_value());

    const DocumentHistory::Snapshot redone = history.redo().value();
    QVERIFY(redone.document == after);
    QCOMPARE(redone.activeLayerID, std::optional<QUuid>(selection));
    QVERIFY(history.canUndo() && !history.canRedo());
    QVERIFY(!history.redo().has_value());

    QVERIFY(!undone.revision.isNull() && !redone.revision.isNull());
    QVERIFY(undone.revision != redone.revision);
    history.begin("Hide Layer", after, selection);
    history.end(std::nullopt, std::nullopt);
    const DocumentHistory::Snapshot second = history.undo().value();
    QCOMPARE(second.revision, redone.revision);
}

void HistoryTests::nestedTransactionsRecordOneEntry()
{
    DocumentHistory history;
    const std::optional<CanvasDocument> start = documentNamed("A");
    std::optional<CanvasDocument> edited = start;
    history.begin("First", start, std::nullopt);
    history.end(documentNamed("Earlier"), std::nullopt);
    QVERIFY(history.canUndo());
    history.undo();
    QVERIFY(history.canRedo());
    history.begin("Layer Setup", start, std::nullopt);
    QVERIFY(!history.canRedo());
    QVERIFY(!history.redo().has_value());
    history.end(start, std::nullopt);
    history.redo();
    history.begin("Layer Setup", start, std::nullopt);
    QVERIFY(!history.canUndo());
    edited->layers[0].name = "B";
    history.begin("Inner", edited, std::nullopt);
    edited->layers[0].name = "C";
    history.end(edited, std::nullopt);
    QCOMPARE(history.undoCount(), 1);
    QVERIFY(!history.canUndo());
    QVERIFY(!history.undo().has_value());
    history.end(edited, std::nullopt);
    QCOMPARE(history.undoCount(), 2);
    QCOMPARE(history.undoName(), QString("Layer Setup"));
    QVERIFY(history.undo().value().document == start);
}

void HistoryTests::noOpEditsPreserveRedo()
{
    DocumentHistory history;
    const std::optional<CanvasDocument> start = documentNamed("A");
    std::optional<CanvasDocument> renamed = start;
    renamed->layers[0].name = "B";
    history.begin("Rename Layer", start, std::nullopt);
    history.end(renamed, std::nullopt);
    history.undo();
    history.begin("Rename Layer", start, std::nullopt);
    history.end(start, start->layers[0].id);
    QCOMPARE(history.undoCount(), 0);
    QVERIFY(history.canRedo());
    QVERIFY(!history.isModified());
}

void HistoryTests::aNewEditClearsRedo()
{
    DocumentHistory history;
    const std::optional<CanvasDocument> start = documentNamed("A");
    history.begin("Rename Layer", start, std::nullopt);
    history.end(documentNamed("B"), std::nullopt);
    history.undo();
    QVERIFY(history.canRedo());
    history.begin("Delete Layer", start, std::nullopt);
    history.end(std::nullopt, std::nullopt);
    QVERIFY(!history.canRedo());
    QCOMPARE(history.undoName(), QString("Delete Layer"));
}

void HistoryTests::savedRevisionFollowsUndoAndRedo()
{
    DocumentHistory history;
    QVERIFY(!history.isModified());
    const std::optional<CanvasDocument> start = documentNamed("A");
    history.begin("Rename Layer", start, std::nullopt);
    history.end(documentNamed("B"), std::nullopt);
    QVERIFY(history.isModified());
    history.markSaved();
    QVERIFY(!history.isModified());
    history.undo();
    QVERIFY(history.isModified());
    history.redo();
    QVERIFY(!history.isModified());
    history.undo();
    history.markSaved();
    history.redo();
    QVERIFY(history.isModified());
}

void HistoryTests::endWithoutBeginIsIgnored()
{
    DocumentHistory history;
    history.end(documentNamed("A"), std::nullopt);
    QCOMPARE(history.undoCount(), 0);
    QVERIFY(!history.isModified());
    history.begin("Rename Layer", documentNamed("A"), std::nullopt);
    history.end(documentNamed("B"), std::nullopt);
    QCOMPARE(history.undoCount(), 1);
}

void HistoryTests::resetClearsEverything()
{
    DocumentHistory history;
    const std::optional<CanvasDocument> start = documentNamed("A");
    history.begin("One", start, std::nullopt);
    history.end(documentNamed("B"), std::nullopt);
    history.begin("Two", start, std::nullopt);
    history.end(documentNamed("C"), std::nullopt);
    const QUuid revisionBeforeReset = history.undo().value().revision;
    history.begin("Open", start, std::nullopt);
    history.reset();
    QVERIFY(!history.canUndo() && !history.canRedo());
    QCOMPARE(history.undoCount(), 0);
    QVERIFY(!history.isModified());
    history.begin("Three", start, std::nullopt);
    history.end(documentNamed("E"), std::nullopt);
    QVERIFY(history.canUndo());
    const QUuid revisionAfterReset = history.undo().value().revision;
    QVERIFY(!revisionAfterReset.isNull());
    QVERIFY(revisionAfterReset != revisionBeforeReset);

    DocumentHistory interrupted;
    interrupted.begin("Open", start, std::nullopt);
    interrupted.reset();
    interrupted.end(documentNamed("D"), std::nullopt);
    QCOMPARE(interrupted.undoCount(), 0);
    QVERIFY(!interrupted.isModified());
}

void HistoryTests::historyBoundsEntriesAndUniqueRetainedPixels()
{
    DocumentHistory history(2, 0);
    std::optional<CanvasDocument> doc = CanvasDocument(64, 32);
    doc->layers.push_back(ImageLayer(asset(64, 32, "fixture"), QPointF(0, 0)));
    for (const QString name : {"A", "B", "C"}) {
        history.begin("Rename", doc, doc->layers[0].id);
        doc->layers[0].name = name;
        history.end(doc, doc->layers[0].id);
    }
    QCOMPARE(history.undoCount(), 2);
    QCOMPARE(history.retainedBytes(doc), 0);
    history.begin("Delete", doc, doc->layers[0].id);
    doc->layers.clear();
    history.end(doc, std::nullopt);
    QCOMPARE(history.undoCount(), 0);
    QCOMPARE(history.retainedBytes(doc), 0);
}

void HistoryTests::retainedBytesCountEachHistoryOnlyImageOnce()
{
    DocumentHistory history;
    std::optional<CanvasDocument> doc = CanvasDocument(64, 32);
    doc->layers.push_back(ImageLayer(asset(10, 10, "old"), QPointF(0, 0)));
    for (const QString name : {"A", "B"}) {
        history.begin("Rename", doc, std::nullopt);
        doc->layers[0].name = name;
        history.end(doc, std::nullopt);
    }
    QCOMPARE(history.retainedBytes(doc), 0);
    history.begin("Replace", doc, std::nullopt);
    doc->layers[0].asset = asset(20, 10, "new");
    history.end(doc, std::nullopt);
    const qint64 oldBytes = 10 * 10 * 4 + 8 * 8 * 4;
    QCOMPARE(history.retainedBytes(doc), oldBytes);
    QCOMPARE(history.retainedBytes(std::nullopt), oldBytes + 20 * 10 * 4 + 8 * 8 * 4);
}

void HistoryTests::byteLimitDropsTheOldestPastEntryFirst()
{
    const qint64 small = 10 * 10 * 4 + 8 * 8 * 4;
    DocumentHistory history(100, small);
    std::optional<CanvasDocument> doc = CanvasDocument(64, 32);
    doc->layers.push_back(ImageLayer(asset(10, 10, "x"), QPointF(0, 0)));
    history.begin("First", doc, std::nullopt);
    doc->layers[0].asset = asset(10, 10, "y");
    history.end(doc, std::nullopt);
    QCOMPARE(history.undoCount(), 1);
    history.begin("Second", doc, std::nullopt);
    doc->layers[0].asset = asset(10, 10, "z");
    history.end(doc, std::nullopt);
    QCOMPARE(history.undoCount(), 1);
    QCOMPARE(history.undoName(), QString("Second"));
    QCOMPARE(history.retainedBytes(doc), small);
}

void HistoryTests::undoTrimsRedoWhenItsPixelsExceedTheLimit()
{
    const qint64 small = 10 * 10 * 4 + 8 * 8 * 4;
    DocumentHistory history(100, small);
    std::optional<CanvasDocument> doc = CanvasDocument(64, 32);
    doc->layers.push_back(ImageLayer(asset(10, 10, "small"), QPointF(0, 0)));
    const std::optional<CanvasDocument> before = doc;
    history.begin("Enlarge", doc, std::nullopt);
    doc->layers[0].asset = asset(40, 40, "large");
    history.end(doc, std::nullopt);
    QCOMPARE(history.undoCount(), 1);
    const DocumentHistory::Snapshot undone = history.undo().value();
    QVERIFY(undone.document == before);
    QVERIFY(!history.canRedo());
    QCOMPARE(history.retainedBytes(before), 0);
}

void HistoryTests::undoTrimsThePastBeforeTheRedoStack()
{
    const qint64 small = 10 * 10 * 4 + 8 * 8 * 4;
    DocumentHistory history(100, 2 * small);
    std::optional<CanvasDocument> doc = CanvasDocument(64, 32);
    doc->layers.push_back(ImageLayer(asset(10, 10, "a"), QPointF(0, 0)));
    history.begin("First", doc, std::nullopt);
    doc->layers[0].asset = asset(10, 10, "b");
    history.end(doc, std::nullopt);
    history.begin("Second", doc, std::nullopt);
    doc->layers[0].asset = asset(15, 15, "c");
    history.end(doc, std::nullopt);
    QCOMPARE(history.undoCount(), 2);
    history.undo();
    QCOMPARE(history.undoCount(), 0);
    QVERIFY(history.canRedo());
    QCOMPARE(history.redoName(), QString("Second"));
}

void HistoryTests::negativeLimitsBecomeZero()
{
    DocumentHistory history(-5, -1);
    QCOMPARE(history.entryLimit, 0);
    QCOMPARE(history.retainedByteLimit, 0);
    history.begin("Rename", documentNamed("A"), std::nullopt);
    history.end(documentNamed("B"), std::nullopt);
    QCOMPARE(history.undoCount(), 0);
    QVERIFY(history.isModified());
}

void HistoryTests::everyLayerEditRoundTripsWithSelection()
{
    EditorSession session;
    std::vector<std::pair<std::optional<CanvasDocument>, std::optional<QUuid>>> states{{std::nullopt, std::nullopt}};
    const auto capture = [&] { states.push_back({session.document(), session.activeLayerID()}); };
    session.createDocument(800, 600);
    capture();
    session.addBlankLayer();
    capture();
    const QUuid base = session.activeLayerID().value();
    session.addBlankLayer();
    capture();
    const QUuid id = session.activeLayerID().value();
    session.renameLayer(id, "Foreground");
    capture();
    session.toggleLayerVisibility(id);
    capture();
    session.reorderLayers({0}, 2);
    capture();
    session.moveActiveLayer(1);
    capture();
    session.deleteActiveLayer();
    capture();
    // Beyond Swift's list: an image, a folder, several layers.
    session.insert(asset(8, 8, "Image"));
    capture();
    const QUuid image = session.activeLayerID().value();
    session.setLayerOpacity(0.5);
    capture();
    session.setLayerBlendMode(LayerBlendMode::overlay);
    capture();
    session.beginOpacityEdit();
    session.setLayerOpacity(0.25);
    session.finishOpacityEdit();
    capture();
    session.beginTransform();
    session.previewTransform({.origin = {3, 4}, .size = {16, 24}, .rotation = 10});
    session.commitTransform();
    capture();
    session.addGroup();
    capture();
    const QUuid folder = session.activeLayerID().value();
    session.selectLayers({image, folder}, folder);
    session.groupSelectedLayers();
    capture();
    // Placing makes another layer active: redo must bring it back.
    QVERIFY(session.placeLayer(image, std::nullopt, std::nullopt, true));
    capture();
    session.selectLayers({image, base}, image);
    session.deleteSelectedLayers();
    capture();
    QCOMPARE(int(session.document().value().layers.size()), 2);
    for (auto expected = states.rbegin() + 1; expected != states.rend(); ++expected) {
        QVERIFY(session.canUndo());
        session.undo();
        QCOMPARE(session.document(), expected->first);
        QCOMPARE(session.activeLayerID(), expected->second);
    }
    QVERIFY(!session.canUndo());
    for (auto expected = states.begin() + 1; expected != states.end(); ++expected) {
        session.redo();
        QCOMPARE(session.document(), expected->first);
        QCOMPARE(session.activeLayerID(), expected->second);
    }
    QVERIFY(!session.canRedo());
}

void HistoryTests::navigationNoOpsAndSaveRevisionPreserveHistory()
{
    EditorSession session;
    // With a view to fit, a refit undoes the zoom.
    session.viewport.resize(QSizeF(400, 300), 1, std::nullopt);
    session.createDocument(800, 600);
    session.addBlankLayer();
    const QUuid id = session.activeLayerID().value();
    session.history.markSaved();
    QVERIFY(!session.isModified());
    session.renameLayer(id, "Changed");
    QVERIFY(session.isModified());
    session.undo();
    QVERIFY(!session.isModified());
    const int count = session.history.undoCount();
    session.zoom(2);
    session.renameLayer(id, "Layer 1");
    session.renameLayer(id, "   ");
    session.reorderLayers({0}, 1);
    QCOMPARE(session.history.undoCount(), count);
    QVERIFY(session.canRedo());
    session.redo();
    QCOMPARE(session.viewport.zoom(), 2.0);
    QVERIFY(session.isModified());
    session.undo();
    session.addBlankLayer();
    QVERIFY(!session.canRedo());
    QVERIFY(session.isModified());
}

void HistoryTests::replacementCanvasAndNestedTransactionsUndoAsOne()
{
    EditorSession session;
    session.createDocument(100, 200);
    session.beginEdit("Layer Setup");
    session.addBlankLayer();
    session.addBlankLayer();
    QVERIFY(!session.canUndo());
    session.endEdit();
    QCOMPARE(session.history.undoName(), QString("Layer Setup"));
    session.undo();
    QVERIFY(session.document().value().layers.empty());
    session.redo();
    const std::optional<CanvasDocument> previous = session.document();
    session.viewport.resize(QSizeF(800, 600), 1, previous.value().size());
    session.zoom(3);
    session.createDocument(300, 400);
    QCOMPARE(session.history.undoName(), QString("New Canvas"));
    session.zoom(3);
    session.undo();
    QCOMPARE(session.document(), previous);
    // Another canvas came back: the view fits it again.
    QVERIFY(session.viewport.followsFit());
    session.redo();
    QCOMPARE(session.document().value().size(), QSizeF(300, 400));
}

void HistoryTests::historyBlockedDuringImportsAndDialogs()
{
    EditorSession session;
    session.createDocument(40, 40);
    session.setIsImporting(true);
    session.undo();
    QVERIFY(session.document().has_value());
    session.setIsImporting(false);
    session.setShowsNewDocument(true);
    session.undo();
    QVERIFY(session.document().has_value());
    session.setShowsNewDocument(false);
    session.undo();
    session.setShowsImporter(true);
    session.redo();
    QVERIFY(!session.document().has_value());
    session.setShowsImporter(false);
    session.redo();
    QVERIFY(session.document().has_value());
    // A busy project, a rename, an import error block too.
    const auto blocked = [&](const std::function<void(bool)> &set) {
        set(true);
        const bool refused = !session.canUndo() && !session.canUseHistory();
        set(false);
        return refused && session.canUndo();
    };
    QVERIFY(blocked([&](bool on) { session.setIsProjectBusy(on); }));
    QVERIFY(blocked([&](bool on) { session.setRenamingLayerID(on ? std::optional(QUuid::createUuid()) : std::nullopt); }));
    QVERIFY(blocked([&](bool on) { session.setImportError(on ? std::optional(QString("failed")) : std::nullopt); }));
}

QTEST_GUILESS_MAIN(HistoryTests)
#include "HistoryTests.moc"
