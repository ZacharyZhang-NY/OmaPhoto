#include "Document/EditorSession.h"
#include "SessionFixtures.h"
#include <QtTest>

namespace {
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

ImageLayer layerNamed(const EditorSession &session, const QString &name)
{
    for (const ImageLayer &layer : session.document().value().layers) {
        if (layer.name == name)
            return layer;
    }
    throw std::runtime_error("no layer named " + name.toStdString());
}
}

class EditorSessionTests : public QObject {
    Q_OBJECT
private slots:
    void settersKeepExactlyWhatTheyAreGiven();
    void theSelectionFollowsTheActiveLayer();
    void aBusyProjectKeepsItsTool();
    void tabSwitchesEachToolsShapeOrMode();
    void everyGateClosesLayerEdits_data();
    void everyGateClosesLayerEdits();
    void theFirstImageMakesTheCanvas();
    void imagesLandCentredOnWholePixels();
    void newLayersJoinTheActiveFolder();
    void foldersAreNamedPlacedAndLimited();
    void aWideFolderIsWalkedOnce();
    void foldingAFolderTakesTheSelectionFromItsContents();
    void deletingAFolderTakesItsContents();
    void movingStaysAmongSiblings();
    void aNewCanvasCanStartWithALayer();
    void sessionChangesAreLogged();
};

void EditorSessionTests::settersKeepExactlyWhatTheyAreGiven()
{
    EditorSession session;
    const auto flags = [&] {
        return QList<bool>{session.isProjectBusy(), session.showsNewDocument(), session.showsImporter(), session.isImporting()};
    };
    QCOMPARE(flags(), (QList<bool>{false, false, false, false}));
    QVERIFY(!session.importError().has_value() && !session.renamingLayerID().has_value());
    // Each flag is its own.
    session.setIsProjectBusy(true);
    QCOMPARE(flags(), (QList<bool>{true, false, false, false}));
    session.setIsProjectBusy(false);
    session.setShowsNewDocument(true);
    QCOMPARE(flags(), (QList<bool>{false, true, false, false}));
    session.setShowsNewDocument(false);
    session.setShowsImporter(true);
    QCOMPARE(flags(), (QList<bool>{false, false, true, false}));
    session.setShowsImporter(false);
    session.setIsImporting(true);
    QCOMPARE(flags(), (QList<bool>{false, false, false, true}));
    session.setIsImporting(false);
    QCOMPARE(flags(), (QList<bool>{false, false, false, false}));
    const QUuid first = QUuid::createUuid(), second = QUuid::createUuid();
    session.setRenamingLayerID(first);
    session.setImportError(QString("first.png: unreadable"));
    QCOMPARE(session.renamingLayerID(), std::optional(first));
    QCOMPARE(session.importError(), std::optional(QString("first.png: unreadable")));
    session.setRenamingLayerID(second);
    session.setImportError(QString("second.png: too large"));
    QCOMPARE(session.renamingLayerID(), std::optional(second));
    QCOMPARE(session.importError(), std::optional(QString("second.png: too large")));
    session.setRenamingLayerID(std::nullopt);
    session.setImportError(std::nullopt);
    QVERIFY(!session.importError().has_value() && !session.renamingLayerID().has_value());
    QCOMPARE(flags(), (QList<bool>{false, false, false, false}));
}

void EditorSessionTests::theSelectionFollowsTheActiveLayer()
{
    EditorSession session;
    session.createDocument(64, 64);
    QCOMPARE(session.selectedLayerIDs(), QSet<QUuid>());
    QVERIFY(!session.activeLayer().has_value());
    session.addBlankLayer();
    const QUuid first = session.activeLayerID().value();
    session.addBlankLayer();
    QCOMPARE(session.selectedLayerIDs(), QSet<QUuid>{session.activeLayerID().value()});
    session.selectLayer(first);
    QCOMPARE(session.selectedLayerIDs(), QSet<QUuid>{first});
    QCOMPARE(session.activeLayer().value().name, QString("Layer 1"));
    session.selectLayer(std::nullopt);
    QCOMPARE(session.selectedLayerIDs(), QSet<QUuid>());
    // An id the document does not hold selects no layer.
    session.setActiveLayerID(QUuid::createUuid());
    QVERIFY(!session.activeLayer().has_value());
}

void EditorSessionTests::aBusyProjectKeepsItsTool()
{
    EditorSession session;
    QCOMPARE(session.tool(), NavigationTool::move);
    session.selectTool(NavigationTool::hand);
    QCOMPARE(session.tool(), NavigationTool::hand);
    session.setIsProjectBusy(true);
    session.selectTool(NavigationTool::zoom);
    QCOMPARE(session.tool(), NavigationTool::hand);
}

void EditorSessionTests::everyGateClosesLayerEdits_data()
{
    QTest::addColumn<int>("gate");
    QTest::newRow("a busy project") << 0;
    QTest::newRow("an import") << 1;
    QTest::newRow("the new canvas sheet") << 2;
    QTest::newRow("the importer") << 3;
    QTest::newRow("a rename") << 4;
}

void EditorSessionTests::everyGateClosesLayerEdits()
{
    QFETCH(int, gate);
    EditorSession session;
    QVERIFY(!session.canEditLayers());
    session.createDocument(64, 64);
    session.addBlankLayer();
    QVERIFY(session.canEditLayers());
    const auto set = [&](bool on) {
        if (gate == 0)
            session.setIsProjectBusy(on);
        else if (gate == 1)
            session.setIsImporting(on);
        else if (gate == 2)
            session.setShowsNewDocument(on);
        else if (gate == 3)
            session.setShowsImporter(on);
        else
            session.setRenamingLayerID(on ? std::optional(QUuid::createUuid()) : std::nullopt);
    };
    set(true);
    QVERIFY(!session.canEditLayers());
    session.addBlankLayer();
    QCOMPARE(int(session.document().value().layers.size()), 1);
    set(false);
    QVERIFY(session.canEditLayers());
    // An import error stops history, not layer edits.
    session.setImportError(QString("failed"));
    QVERIFY(session.canEditLayers() && !session.canUseHistory());
}

void EditorSessionTests::tabSwitchesEachToolsShapeOrMode()
{
    EditorSession session;
    session.createDocument(20, 20);
    session.addBlankLayer();
    session.selectTool(NavigationTool::marquee);
    session.cycleToolMode();
    QCOMPARE(session.marqueeKind(), LassoKind::ellipse);
    session.selectTool(NavigationTool::lasso);
    session.cycleToolMode();
    QCOMPARE(session.lassoKind(), LassoKind::polygonal);
    session.cycleToolMode();
    QCOMPARE(session.lassoKind(), LassoKind::freehand);
    QCOMPARE(session.marqueeKind(), LassoKind::ellipse);
    session.selectTool(NavigationTool::brush);
    session.cycleToolMode();
    QCOMPARE(session.brushMode(), BrushToolMode::erase);
    // Busy or mid-stroke, nothing switches, and nothing is said.
    QSignalSpy changes(&session, &EditorSession::changed);
    session.setIsProjectBusy(true);
    changes.clear();
    session.cycleToolMode();
    session.setIsProjectBusy(false);
    session.beginBrush(QPointF(5, 5));
    changes.clear();
    session.cycleToolMode();
    QCOMPARE(changes.count(), 0);
    QCOMPARE(session.brushMode(), BrushToolMode::erase);
    session.cancelBrush();
    // A tool without modes stays silent.
    session.selectTool(NavigationTool::hand);
    changes.clear();
    session.cycleToolMode();
    QCOMPARE(changes.count(), 0);
}

void EditorSessionTests::theFirstImageMakesTheCanvas()
{
    EditorSession session;
    session.viewport.resize(QSizeF(400, 300), 1, std::nullopt);
    session.viewport.setZoom(3, session.viewport.center(), QSizeF(64, 32));
    session.insert(asset(64, 32, "First"), QPointF(999, 999));
    const CanvasDocument document = session.document().value();
    QCOMPARE(document.size(), QSizeF(64, 32));
    QVERIFY(session.viewport.followsFit());
    QCOMPARE(session.history.undoName(), QString("Import Image"));
    // The drop point still places the layer.
    QCOMPARE(document.layers[0].origin(), QPointF(967, 983));
    QCOMPARE(session.activeLayerID(), std::optional(document.layers[0].id));
    session.undo();
    QVERIFY(!session.document().has_value());
}

void EditorSessionTests::imagesLandCentredOnWholePixels()
{
    EditorSession session;
    session.createDocument(101, 51);
    session.insert(asset(64, 32, "Centred"));
    // Half pixels fall to the left and up.
    QCOMPARE(session.activeLayer().value().origin(), QPointF(18, 9));
    QCOMPARE(session.activeLayer().value().size(), QSizeF(64, 32));
    // Odd in odd centres exactly: both halves count.
    session.insert(asset(65, 33, "Odd"));
    QCOMPARE(session.activeLayer().value().origin(), QPointF(18, 9));
    session.insert(asset(64, 33, "Dropped"), QPointF(10.25, -4.5));
    QCOMPARE(session.activeLayer().value().origin(), QPointF(-22, -21));
    QCOMPARE(names(session), (QStringList{"Centred", "Odd", "Dropped"}));
    QCOMPARE(session.document().value().size(), QSizeF(101, 51));
}

void EditorSessionTests::newLayersJoinTheActiveFolder()
{
    EditorSession session;
    session.createDocument(64, 64);
    session.addBlankLayer();
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    session.addBlankLayer();
    QCOMPARE(session.activeLayer().value().parentID, std::optional(folder));
    // Beside a layer in the folder, the next joins it.
    session.addBlankLayer();
    QCOMPARE(session.activeLayer().value().parentID, std::optional(folder));
    session.insert(asset(4, 4, "Image"));
    QCOMPARE(session.activeLayer().value().parentID, std::optional(folder));
    session.selectLayer(folder);
    session.toggleGroupExpansion(folder);
    QVERIFY(session.collapsedGroupIDs().contains(folder));
    // Filling a folded folder opens it, above all it holds.
    session.addBlankLayer();
    QVERIFY(!session.collapsedGroupIDs().contains(folder));
    QCOMPARE(names(session), (QStringList{"Layer 1", "Folder 1", "Layer 2", "Layer 3", "Image", "Layer 4"}));
    session.selectLayer(folder);
    session.toggleGroupExpansion(folder);
    session.insert(asset(4, 4, "Inside"));
    QVERIFY(!session.collapsedGroupIDs().contains(folder));
    QCOMPARE(session.activeLayer().value().parentID, std::optional(folder));
    // A nested folder's contents count as the outer folder's.
    session.addGroup();
    const QUuid inner = session.activeLayerID().value();
    session.addBlankLayer();
    QCOMPARE(session.activeLayer().value().parentID, std::optional(inner));
    session.selectLayer(folder);
    session.addBlankLayer();
    QCOMPARE(names(session).last(), QString("Layer 6"));
    QCOMPARE(session.activeLayer().value().parentID, std::optional(folder));
    session.selectLayer(std::nullopt);
    session.insert(asset(4, 4, "Outside"));
    QCOMPARE(session.activeLayer().value().parentID, std::nullopt);
}

void EditorSessionTests::foldersAreNamedPlacedAndLimited()
{
    EditorSession session;
    session.createDocument(64, 64);
    session.addBlankLayer();
    session.addBlankLayer();
    session.selectLayer(session.document().value().layers[0].id);
    session.addGroup();
    QCOMPARE(names(session), (QStringList{"Layer 1", "Folder 1", "Layer 2"}));
    QCOMPARE(session.history.undoName(), QString("New Folder"));
    const ImageLayer folder = session.activeLayer().value();
    QVERIFY(folder.isGroup && !folder.asset.has_value());
    QCOMPARE(folder.size(), QSizeF(64, 64));
    QCOMPARE(folder.parentID, std::nullopt);
    // Each new folder nests in the active one, 64 deep.
    for (int depth = 0; depth < 70; ++depth)
        session.addGroup();
    QCOMPARE(int(session.document().value().layers.size()), 2 + 64);
    QCOMPARE(session.activeLayer().value().name, QString("Folder 64"));
    session.toggleGroupExpansion(folder.id);
    QCOMPARE(session.activeLayerID(), std::optional(folder.id));
    session.addGroup();
    QVERIFY(!session.collapsedGroupIDs().contains(folder.id));
    // Ten thousand layers leave no room for a folder.
    EditorSession crowded;
    crowded.createDocument(8, 8);
    crowded.beginEdit("Fill");
    const ImportedImage dot = asset(1, 1, "Dot");
    for (int index = 0; index < 9'999; ++index)
        crowded.insert(dot);
    crowded.endEdit();
    crowded.addGroup();
    QCOMPARE(int(crowded.document().value().layers.size()), 10'000);
    crowded.addGroup();
    QCOMPARE(int(crowded.document().value().layers.size()), 10'000);
}

void EditorSessionTests::aWideFolderIsWalkedOnce()
{
    EditorSession session;
    session.createDocument(8, 8);
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    session.beginEdit("Fill");
    const ImportedImage dot = asset(1, 1, "Dot");
    for (int index = 0; index < 9'999; ++index) {
        session.selectLayer(folder);
        session.insert(dot);
    }
    session.endEdit();
    // Copying the layers for each child would take minutes.
    QElapsedTimer timer;
    timer.start();
    QCOMPARE(int(session.descendantIDs(folder).size()), 9'999);
    QVERIFY2(timer.elapsed() < 2'000, qPrintable(QString::number(timer.elapsed())));
}

void EditorSessionTests::foldingAFolderTakesTheSelectionFromItsContents()
{
    EditorSession session;
    session.createDocument(64, 64);
    session.addBlankLayer();
    const QUuid outside = session.activeLayerID().value();
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    session.addGroup();
    const QUuid inner = session.activeLayerID().value();
    session.addBlankLayer();
    const QUuid deep = session.activeLayerID().value();
    QCOMPARE(session.descendantIDs(folder), (QSet<QUuid>{inner, deep}));
    QCOMPARE(session.descendantIDs(inner), QSet<QUuid>{deep});
    QCOMPARE(session.descendantIDs(deep), QSet<QUuid>());
    QCOMPARE(session.descendantIDs(QUuid::createUuid()), QSet<QUuid>());
    QCOMPARE(EditorSession().descendantIDs(folder), QSet<QUuid>());
    session.toggleGroupExpansion(folder);
    QCOMPARE(session.activeLayerID(), std::optional(folder));
    QCOMPARE(session.collapsedGroupIDs(), QSet<QUuid>{folder});
    session.toggleGroupExpansion(folder);
    QCOMPARE(session.collapsedGroupIDs(), QSet<QUuid>());
    // A selection outside stays; layers and strangers do not fold.
    session.selectLayer(outside);
    session.toggleGroupExpansion(inner);
    QCOMPARE(session.activeLayerID(), std::optional(outside));
    session.toggleGroupExpansion(outside);
    session.toggleGroupExpansion(QUuid::createUuid());
    QCOMPARE(session.collapsedGroupIDs(), QSet<QUuid>{inner});
    session.setIsProjectBusy(true);
    session.toggleGroupExpansion(inner);
    QCOMPARE(session.collapsedGroupIDs(), QSet<QUuid>{inner});
}

void EditorSessionTests::deletingAFolderTakesItsContents()
{
    EditorSession session;
    session.createDocument(64, 64);
    session.addBlankLayer();
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    session.addBlankLayer();
    session.addGroup();
    session.addBlankLayer();
    session.selectLayer(std::nullopt);
    session.addBlankLayer();
    QCOMPARE(names(session), (QStringList{"Layer 1", "Folder 1", "Layer 2", "Folder 2", "Layer 3", "Layer 4"}));
    session.selectLayer(layerNamed(session, "Layer 3").id);
    const int count = session.history.undoCount();
    session.deleteLayer(folder);
    QCOMPARE(names(session), (QStringList{"Layer 1", "Layer 4"}));
    // The folder sat at index 1; its successor is selected.
    QCOMPARE(session.activeLayer().value().name, QString("Layer 4"));
    QCOMPARE(session.history.undoCount(), count + 1);
    session.undo();
    QCOMPARE(int(session.document().value().layers.size()), 6);
    QCOMPARE(session.activeLayer().value().name, QString("Layer 3"));
    // Deleting something else leaves the selection alone.
    session.deleteLayer(layerNamed(session, "Layer 4").id);
    QCOMPARE(session.activeLayer().value().name, QString("Layer 3"));
}

void EditorSessionTests::movingStaysAmongSiblings()
{
    EditorSession session;
    session.createDocument(64, 64);
    session.addBlankLayer();
    session.addGroup();
    session.addBlankLayer();
    session.addBlankLayer();
    session.selectLayer(std::nullopt);
    session.addBlankLayer();
    QCOMPARE(names(session), (QStringList{"Layer 1", "Folder 1", "Layer 2", "Layer 3", "Layer 4"}));
    // Layer 4's siblings: Layer 1 and the folder, both below.
    QVERIFY(session.canMoveActiveLayer(-2) && !session.canMoveActiveLayer(-3) && !session.canMoveActiveLayer(1));
    session.moveActiveLayer(-1);
    QCOMPARE(names(session), (QStringList{"Layer 1", "Layer 4", "Layer 2", "Layer 3", "Folder 1"}));
    // Inside the folder two layers trade places and no more.
    session.selectLayer(layerNamed(session, "Layer 2").id);
    QVERIFY(session.canMoveActiveLayer(1) && !session.canMoveActiveLayer(-1) && !session.canMoveActiveLayer(2));
    session.moveActiveLayer(1);
    QCOMPARE(names(session), (QStringList{"Layer 1", "Layer 4", "Layer 3", "Layer 2", "Folder 1"}));
    session.selectLayer(std::nullopt);
    QVERIFY(!session.canMoveActiveLayer(0));
}

void EditorSessionTests::aNewCanvasCanStartWithALayer()
{
    EditorSession session;
    session.setShowsNewDocument(true);
    session.setRenamingLayerID(QUuid::createUuid());
    session.createDocument(640, 480, true);
    const CanvasDocument document = session.document().value();
    QCOMPARE(int(document.layers.size()), 1);
    QCOMPARE(session.activeLayer().value().name, QString("Layer 1"));
    QVERIFY(!session.activeLayer().value().asset.has_value());
    QCOMPARE(session.activeLayer().value().size(), QSizeF(640, 480));
    QVERIFY(!session.showsNewDocument() && !session.renamingLayerID().has_value());
    QCOMPARE(document.resolution, 72.0);
    // A plain canvas selects nothing; busy or importing makes none.
    session.createDocument(10, 10);
    QVERIFY(!session.activeLayerID().has_value() && session.document().value().layers.empty());
    session.setIsProjectBusy(true);
    session.createDocument(20, 20);
    session.setIsProjectBusy(false);
    session.setIsImporting(true);
    session.createDocument(30, 30);
    QCOMPARE(session.document().value().size(), QSizeF(10, 10));
}

void EditorSessionTests::sessionChangesAreLogged()
{
    EditorSession session;
    QTest::ignoreMessage(QtInfoMsg, "new canvas 12 x 34");
    session.createDocument(12, 34);
    session.addBlankLayer();
    QTest::ignoreMessage(QtInfoMsg, "restored a history snapshot of 0 layers");
    session.undo();
}

// Alt-dragging a row drops a duplicate, one step.
QTEST_GUILESS_MAIN(EditorSessionTests)
#include "EditorSessionTests.moc"
