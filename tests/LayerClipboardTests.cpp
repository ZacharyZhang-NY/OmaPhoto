#include "Document/ProjectWorkspace.h"
#include "MenuFixtures.h"
#include "SelectionFixtures.h"
#include "SessionFixtures.h"
#include <QClipboard>
#include <QMimeData>
#include <QtTest>

// Swift 1.2.5: whole layers copied, pasted and duplicated.
namespace {
ImportedImage square(const QString &name, int side = 2)
{
    QImage image(side, side, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::red);
    return ImportedImage(image, QImage(), name);
}

// Three red layers, A under B under C.
QUuid fill(EditorSession &session, std::vector<QUuid> &ids)
{
    session.createDocument(20, 20);
    for (const char *name : {"A", "B", "C"}) {
        session.insert(square(QString::fromLatin1(name)), QPointF(4 + 6 * ids.size(), 5));
        ids.push_back(session.activeLayerID().value());
    }
    return ids.front();
}

// A clipboard history's replay: every format, written anew.
QMimeData *saved()
{
    auto *copy = new QMimeData;
    const QMimeData *data = QGuiApplication::clipboard()->mimeData();
    for (const QString &format : data->formats())
        copy->setData(format, data->data(format));
    // Offscreen reads no image from PNG bytes; a desktop does.
    copy->setImageData(data->imageData());
    return copy;
}

// Bottom to top, as the document lists them.
QString names(const EditorSession &session)
{
    QStringList result;
    for (const ImageLayer &layer : session.document().value().layers)
        result << layer.name;
    return result.join(QLatin1Char(','));
}
}

class LayerClipboardTests : public QObject {
    Q_OBJECT
private slots:
    void copyWithoutASelectionTakesTheLayersWhole();
    void pasteDuplicatesInTheSameProject();
    void anotherCopyOrAGoneLayerEndsTheLayerPaste();
    void pasteBringsTheLayersIntoAnotherProject();
    void severalDuplicatesStackAboveTheTopmost();
    void foldersLeaveTheAnchorToThePixels();
    void theEditMenuCopiesAndPastesAFolder();
    void clipboardNoticesKeepTheCopy();
    void aRestoredOldCopyStaysExpired();
    void theLatestCopyAnywhereWins();
    void aLayerTheSourceTabLacksIsNotSelected();
};

void LayerClipboardTests::copyWithoutASelectionTakesTheLayersWhole()
{
    EditorSession session;
    std::vector<QUuid> ids;
    fill(session, ids);
    session.selectLayers({}, std::nullopt);
    QVERIFY(!session.canCopyLayer());
    session.selectLayers({ids[2], ids[0]}, ids[2]);
    QVERIFY(session.canCopyLayer());
    session.copySelection();
    // Pixels too: a layer's copy also serves other apps.
    QCOMPARE(session.copiedLayer().value().ids, (std::vector<QUuid>{ids[0], ids[2]}));
    QVERIFY(session.pixelClipboard() && QGuiApplication::clipboard()->mimeData()->hasImage());
    // A folder has no pixels: its id alone goes out.
    session.selectLayers({ids[1]}, ids[1]);
    session.groupSelectedLayers();
    const QUuid folder = session.activeLayerID().value();
    session.selectLayers({folder, ids[1]}, folder);
    session.copySelection();
    QCOMPARE(session.copiedLayer().value().ids, std::vector<QUuid>{folder});
    QVERIFY(!session.pixelClipboard());
    QCOMPARE(QGuiApplication::clipboard()->mimeData()->data("com.compositor.copied-layer"), uuidString(folder).toUtf8());
    // A selection or a chosen mask copies pixels, not layers.
    session.selectLayer(ids[0]);
    session.selectAll();
    QVERIFY(!session.canCopyLayer() && session.canCopyPixels());
    session.copySelection();
    QVERIFY(!session.copiedLayer() && session.pixelClipboard());
    session.deselect();
    session.addMask(true);
    QVERIFY(session.isMaskSelected() && !session.canCopyLayer());
    session.selectLayerTarget(ids[0], false);
    QVERIFY(session.canCopyLayer());
    session.setIsProjectBusy(true);
    QVERIFY(!session.canCopyLayer());
}

void LayerClipboardTests::pasteDuplicatesInTheSameProject()
{
    ProjectWorkspace workspace;
    EditorSession &session = workspace.current().session;
    std::vector<QUuid> ids;
    fill(session, ids);
    session.selectLayers({ids[0], ids[1]}, ids[0]);
    session.copySelection();
    const int steps = session.history.undoCount();
    QVERIFY(workspace.pasteCopiedLayer());
    QCOMPARE(session.history.undoCount(), steps + 1);
    QCOMPARE(session.history.undoName(), QString("Paste"));
    // Stacked above the topmost original, in their order.
    QCOMPARE(names(session), QString("A,B,A copy,B copy,C"));
    const std::vector<ImageLayer> layers = session.document().value().layers;
    QCOMPARE(session.selectedLayerIDs(), (QSet<QUuid>{layers[2].id, layers[3].id}));
    QCOMPARE(session.activeLayerID().value(), layers[2].id);
    // With C active, the first copied layer's copy leads.
    session.undo();
    session.selectLayer(ids[2]);
    QVERIFY(workspace.pasteCopiedLayer());
    QCOMPARE(session.activeLayer().value().name, QString("A copy"));
    session.undo();
    QVERIFY(workspace.pasteCopiedLayer());
    // A busy project refuses; the paste goes on to pixels.
    session.setIsProjectBusy(true);
    QVERIFY(!workspace.pasteCopiedLayer());
    session.setIsProjectBusy(false);
    QVERIFY(workspace.pasteCopiedLayer());
    QCOMPARE(session.document().value().layers.size(), size_t(7));
}

void LayerClipboardTests::anotherCopyOrAGoneLayerEndsTheLayerPaste()
{
    ProjectWorkspace workspace;
    EditorSession &session = workspace.current().session;
    std::vector<QUuid> ids;
    fill(session, ids);
    session.selectLayers({ids[0], ids[1]}, ids[0]);
    session.copySelection();
    // A layer gone since stays out; all gone, nothing pastes.
    session.selectLayer(ids[0]);
    session.deleteLayerOrMask();
    QVERIFY(workspace.pasteCopiedLayer());
    QCOMPARE(names(session), QString("B,B copy,C"));
    session.selectLayer(ids[1]);
    session.deleteLayerOrMask();
    QVERIFY(!workspace.pasteCopiedLayer());
    // Another app's copy ends it too.
    session.selectLayer(ids[2]);
    session.copySelection();
    QVERIFY(session.copiedLayer());
    QGuiApplication::clipboard()->setText(QStringLiteral("elsewhere"));
    QVERIFY(!workspace.pasteCopiedLayer());
    QCOMPARE(session.document().value().layers.size(), size_t(2));
}

void LayerClipboardTests::pasteBringsTheLayersIntoAnotherProject()
{
    ProjectWorkspace workspace;
    EditorSession &from = workspace.current().session;
    std::vector<QUuid> ids;
    fill(from, ids);
    from.selectLayer(ids[2]);
    LayerEffects effects;
    effects.stroke = StrokeEffect();
    from.setEffects(effects);
    from.selectLayers({ids[0], ids[2]}, ids[2]);
    from.copySelection();
    ProjectTab &target = workspace.addTab();
    target.session.createDocument(40, 30);
    QCOMPARE(workspace.selectedID(), target.id);
    QVERIFY(workspace.pasteCopiedLayer());
    QTRY_COMPARE(target.session.document().value().layers.size(), size_t(2));
    QCOMPARE(target.session.history.undoName(), QString("Copy Layers from Project"));
    const std::vector<ImageLayer> layers = target.session.document().value().layers;
    QCOMPARE(names(target.session), QString("A,C"));
    // A and C keep their gap, centred on the canvas.
    QCOMPARE(layers[0].transform.origin, QPointF(13, 14));
    QCOMPARE(layers[1].transform.origin, QPointF(25, 14));
    QVERIFY(layers[1].effects == effects);
    QCOMPARE(target.session.activeLayerID().value(), layers[0].id);
    QCOMPARE(target.session.selectedLayerIDs(), (QSet<QUuid>{layers[0].id, layers[1].id}));
    QCOMPARE(from.document().value().layers.size(), size_t(3));
}

void LayerClipboardTests::foldersLeaveTheAnchorToThePixels()
{
    ProjectWorkspace workspace;
    EditorSession &from = workspace.current().session;
    from.createDocument(20, 20);
    from.insert(square("A"), QPointF(4, 5));
    const QUuid a = from.activeLayerID().value();
    from.addGroup();
    const QUuid first = from.activeLayerID().value();
    from.addGroup();
    const QUuid second = from.activeLayerID().value();
    ProjectTab &target = workspace.addTab();
    target.session.createDocument(40, 30);
    const auto copy = [&](const std::vector<QUuid> &ids) {
        bool done = false;
        workspace.copyLayers(ids, target.id, std::nullopt, [&] { done = true; });
        return QTest::qWaitFor([&] { return done; }, 10'000);
    };
    // A folder spans the canvas; only A's box counts.
    QVERIFY(copy({a, first}));
    QCOMPARE(target.session.document().value().layers.front().transform.origin, QPointF(19, 14));
    // Folders alone: the first one's centre.
    target.session.undo();
    QVERIFY(copy({first, second}));
    QCOMPARE(target.session.document().value().layers.front().transform.origin, QPointF(10, 5));
}

void LayerClipboardTests::severalDuplicatesStackAboveTheTopmost()
{
    EditorSession session;
    std::vector<QUuid> ids;
    fill(session, ids);
    // One copy sits just above its original.
    session.selectLayer(ids[0]);
    session.duplicateActiveLayer();
    QCOMPARE(names(session), QString("A,A copy,B,C"));
    session.undo();
    // C goes into a folder; A's copy joins C's there.
    session.selectLayer(ids[2]);
    session.groupSelectedLayers();
    const QUuid folder = session.activeLayerID().value();
    // C is active, not first: its copy leads.
    session.selectLayers({ids[0], ids[2]}, ids[2]);
    const int steps = session.history.undoCount();
    session.duplicateActiveLayer();
    QCOMPARE(session.history.undoCount(), steps + 1);
    QCOMPARE(session.history.undoName(), QString("Duplicate Layer"));
    QCOMPARE(names(session), QString("A,B,Folder 1,C,A copy,C copy"));
    const std::vector<ImageLayer> layers = session.document().value().layers;
    QVERIFY(layers[4].parentID == folder && layers[5].parentID == folder);
    QCOMPARE(session.activeLayerID().value(), layers[5].id);
    QCOMPARE(session.selectedLayerIDs(), (QSet<QUuid>{layers[4].id, layers[5].id}));
    session.undo();
    QCOMPARE(names(session), QString("A,B,Folder 1,C"));
}

void LayerClipboardTests::theEditMenuCopiesAndPastesAFolder()
{
    Bar bar;
    bar.session().createDocument(20, 20);
    bar.session().insert(square("A"));
    bar.session().groupSelectedLayers();
    bar.action("copy").trigger();
    bar.action("paste").trigger();
    // The folder's copy holds a copy of A.
    QCOMPARE(names(bar.session()), QString("Folder 1,Folder 1 copy,A,A"));
    const std::vector<ImageLayer> layers = bar.session().document().value().layers;
    QVERIFY(layers[2].parentID == layers[1].id && layers[3].parentID == layers[0].id);
    QCOMPARE(bar.session().history.undoName(), QString("Paste"));
}

void LayerClipboardTests::clipboardNoticesKeepTheCopy()
{
    // Wayland announces one copy twice; neither ends it.
    ProjectWorkspace workspace;
    EditorSession &session = workspace.current().session;
    std::vector<QUuid> ids;
    fill(session, ids);
    session.selectLayer(ids[0]);
    session.copySelection();
    emit QGuiApplication::clipboard()->dataChanged();
    emit QGuiApplication::clipboard()->dataChanged();
    QVERIFY(workspace.pasteCopiedLayer());
    QCOMPARE(names(session), QString("A,A copy,B,C"));
    // Pixels copied under a selection go back in place too.
    session.selectLayer(ids[1]);
    session.applySelection(rectPath(QRectF(9, 4, 2, 2)), SelectionMode::replace, QStringLiteral("Select"));
    session.copySelection();
    emit QGuiApplication::clipboard()->dataChanged();
    QVERIFY(!workspace.pasteCopiedLayer() && session.canPaste());
    session.paste();
    QCOMPARE(session.activeLayer().value().transform.origin, QPointF(9, 4));
}

void LayerClipboardTests::aRestoredOldCopyStaysExpired()
{
    ProjectWorkspace workspace;
    EditorSession &session = workspace.current().session;
    std::vector<QUuid> ids;
    fill(session, ids);
    // Another app sets the same entry again: theirs now.
    session.selectLayer(ids[0]);
    session.copySelection();
    QGuiApplication::clipboard()->setMimeData(saved());
    QVERIFY(!workspace.pasteCopiedLayer());
    // A layer copy, replaced by text, then restored from history.
    session.copySelection();
    QMimeData *old = saved();
    QGuiApplication::clipboard()->setText(QStringLiteral("elsewhere"));
    QGuiApplication::clipboard()->setMimeData(old);
    QVERIFY(!EditorSession::clipboardHolds(session.copiedLayer().value().data));
    QVERIFY(!workspace.pasteCopiedLayer());
    // Its pixels come back as another app's: centred, canvas-sized.
    session.paste();
    QCOMPARE(names(session), QString("A,Layer 1,B,C"));
    QCOMPARE(session.activeLayer().value().size(), QSizeF(20, 20));
    // Selected pixels replayed so land centred, not in place.
    session.selectLayer(ids[1]);
    session.applySelection(rectPath(QRectF(9, 4, 2, 2)), SelectionMode::replace, QStringLiteral("Select"));
    session.copySelection();
    old = saved();
    QGuiApplication::clipboard()->setText(QStringLiteral("elsewhere"));
    QGuiApplication::clipboard()->setMimeData(old);
    session.paste();
    QCOMPARE(session.activeLayer().value().transform.origin, QPointF(9, 9));
}

void LayerClipboardTests::theLatestCopyAnywhereWins()
{
    ProjectWorkspace workspace;
    EditorSession &first = workspace.current().session;
    first.createDocument(20, 20);
    first.addGroup();
    first.copySelection();
    EditorSession &second = workspace.addTab().session;
    second.createDocument(20, 20);
    second.addGroup();
    second.addGroup();
    second.copySelection();
    // The second copy ends the first's at once.
    QVERIFY(!EditorSession::clipboardHolds(first.copiedLayer().value().data));
    QVERIFY(EditorSession::clipboardHolds(second.copiedLayer().value().data));
    // A live object elsewhere is no clipboard of ours.
    QMimeData elsewhere;
    QVERIFY(!EditorSession::clipboardHolds(QPointer<QMimeData>(&elsewhere)));
    // The second tab's folder is the one pasted.
    QVERIFY(workspace.pasteCopiedLayer());
    QCOMPARE(names(second), QString("Folder 1,Folder 2,Folder 2 copy"));
    QCOMPARE(names(first), QString("Folder 1"));
}

void LayerClipboardTests::aLayerTheSourceTabLacksIsNotSelected()
{
    // Two tabs of one project share ids; the first wins.
    ProjectWorkspace workspace;
    EditorSession &first = workspace.current().session;
    first.createDocument(20, 20);
    first.insert(square("Shared"));
    const QUuid shared = first.activeLayerID().value();
    ProjectTab &second = workspace.addTab();
    second.session.installProject(first.projectSnapshot().value(), QString("twin.comp"));
    second.session.insert(square("Own"));
    second.session.selectLayers({shared, second.session.activeLayerID().value()}, shared);
    second.session.copySelection();
    ProjectTab &target = workspace.addTab();
    target.session.createDocument(20, 20);
    QVERIFY(workspace.pasteCopiedLayer());
    QTRY_COMPARE(names(target.session), QString("Shared"));
    const QUuid copy = target.session.document().value().layers.front().id;
    QCOMPARE(target.session.selectedLayerIDs(), QSet<QUuid>{copy});
    QCOMPARE(target.session.activeLayerID().value(), copy);
}

QTEST_MAIN(LayerClipboardTests)
#include "LayerClipboardTests.moc"
