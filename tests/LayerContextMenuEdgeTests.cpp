#include "LayerContextMenuFixtures.h"
#include "ProjectFixtures.h"
#include "SessionFixtures.h"
#include <QLineEdit>

// The menu's gates at their edges, and its routing.
class LayerContextMenuEdgeTests : public QObject {
    Q_OBJECT
private slots:
    void aRefusedSelectionLeavesTheEntriesAtRest();
    void actionsReadTheActiveLayerWhenChosen();
    void aChosenEffectsLayerRowCountsAsUnselected();
    void theGatesHoldAtTheirEdges();
    void theMenuTargetsThePressedRowThroughARebuild();
    void aStaleEffectLeavesItsRowSelected();
    void anEntryWithNoActiveLayerDoesNothing();
};

void LayerContextMenuEdgeTests::aRefusedSelectionLeavesTheEntriesAtRest()
{
    // Levels open, nothing selected, then an effect's row.
    const auto session = paintedPair();
    Shown shown(*session);
    session->beginLevels();
    QVERIFY(session->levels());
    session->selectLayers({}, std::nullopt);
    QVERIFY(session->selectedLayerIDs().isEmpty() && !session->activeLayerID());
    auto *effect = shown.row(0).findChild<LayerEffectRow *>();
    QVERIFY(effect);
    const std::unique_ptr<QMenu> menu = shown.list.menuFor(shown.row(0), effect->mapTo(&shown.row(0), effect->rect().center()));
    QVERIFY(!session->activeLayerID());
    for (const char *name : {"duplicateLayer", "renameLayer", "deleteLayer", "clippingMask", "groupLayers", "moveOut", "addMask", "toggleMask",
                             "deleteMask", "linkMask", "layerVisibility"})
        QVERIFY2(!item(*menu, name).isEnabled(), name);
    session->cancelLevels();
}

void LayerContextMenuEdgeTests::actionsReadTheActiveLayerWhenChosen()
{
    const auto session = threeLayers();
    Shown shown(*session);
    const QUuid a = shown.row(0).layerID(), b = shown.row(1).layerID();
    // Built on A; B is active when chosen.
    std::unique_ptr<QMenu> menu = shown.menu(0);
    session->selectLayer(b);
    item(*menu, "layerVisibility").trigger();
    QVERIFY(layerWith(*session, a).isVisible && !layerWith(*session, b).isVisible);
    menu = shown.menu(0);
    session->selectLayer(b);
    item(*menu, "addWhiteMask").trigger();
    QVERIFY(!layerWith(*session, a).mask && layerWith(*session, b).mask && session->activeLayerID() == b);
    menu = shown.menu(0);
    session->selectLayer(b);
    item(*menu, "clippingMask").trigger();
    QVERIFY(!layerWith(*session, a).maskSourceID && layerWith(*session, b).maskSourceID);
    // Rename asks the gate again, as Swift's.
    menu = shown.menu(0);
    session->setIsProjectBusy(true);
    item(*menu, "renameLayer").trigger();
    QVERIFY(!session->renamingLayerID());
    session->setIsProjectBusy(false);
}

void LayerContextMenuEdgeTests::aChosenEffectsLayerRowCountsAsUnselected()
{
    // Swift's table shows no row while an effect is chosen.
    const auto session = paintedPair();
    Shown shown(*session);
    const QUuid top = shown.row(0).layerID();
    session->selectEffect(LayerEffectKind::stroke, top, false);
    QVERIFY(session->selectedLayerIDs().contains(top) && session->effectSelection());
    QLineEdit field;
    field.show();
    field.activateWindow();
    field.setFocus();
    QTRY_VERIFY(field.hasFocus());
    shown.list.activateWindow();
    QVERIFY(shown.menu(0));
    QTRY_VERIFY(shown.list.hasFocus());
    QVERIFY(!session->effectSelection() && session->selectedLayerIDs() == QSet<QUuid>{top});
}

void LayerContextMenuEdgeTests::theGatesHoldAtTheirEdges()
{
    // Nothing selected: the menu selects its row first.
    const auto session = threeLayers();
    Shown shown(*session);
    const QUuid a = shown.row(0).layerID(), b = shown.row(1).layerID();
    session->selectLayers({}, std::nullopt);
    QVERIFY(shown.list.contextMenu(a));
    QCOMPARE(session->selectedLayerIDs(), QSet<QUuid>{a});
    // Two layers chosen: no mask entry.
    session->selectLayers({a, b}, a);
    std::unique_ptr<QMenu> menu = shown.list.contextMenu(a);
    for (const char *name : {"addMask", "addWhiteMask", "addBlackMask"})
        QVERIFY2(!item(*menu, name).isEnabled(), name);
    // A folder's mask never links.
    session->groupSelectedLayers();
    session->addMask(true);
    QVERIFY(session->activeLayer().value().isGroup && session->activeLayer().value().mask);
    menu = shown.list.contextMenu(session->activeLayerID().value());
    QVERIFY(!item(*menu, "linkMask").isEnabled() && item(*menu, "deleteMask").isEnabled());
    // Grouping stops at ten thousand layers.
    ProjectSnapshot crowded = twoLayers();
    while (crowded.manifest.layers.size() < 9'999)
        crowded.manifest.layers.push_back({.id = QUuid::createUuid(), .name = "n", .isVisible = true, .transform = {.origin = {0, 0}, .size = {1, 1}},
                                           .imageFile = std::nullopt});
    EditorSession packed;
    packed.installProject(crowded, QString());
    NativeLayerList list(packed);
    const QUuid last = packed.document().value().layers.back().id;
    packed.selectLayer(last);
    QVERIFY(item(*list.contextMenu(last), "groupLayers").isEnabled());
    packed.addBlankLayer();
    QCOMPARE(packed.document().value().layers.size(), size_t(10'000));
    QVERIFY(!item(*list.contextMenu(packed.activeLayerID().value()), "groupLayers").isEnabled());
}

void LayerContextMenuEdgeTests::theMenuTargetsThePressedRowThroughARebuild()
{
    // Committing new text rebuilds the rows under the press.
    const auto session = threeLayers();
    Shown shown(*session);
    const QUuid pressed = shown.row(1).layerID();
    session->selectTool(NavigationTool::type);
    session->beginText(QPointF(10, 10), true);
    session->changeTextStyle([](LayerTextStyle &style) { style.content = QStringLiteral("new text"); });
    QVERIFY(session->textDraft());
    QPointer<LayerCell> cell = &shown.row(1);
    const std::unique_ptr<QMenu> menu = shown.list.menuFor(*cell, QPoint(cell->width() - 20, 20));
    QVERIFY(!cell && !session->textDraft());
    QCOMPARE(session->document().value().layers.size(), size_t(4));
    QCOMPARE(session->activeLayerID().value(), pressed);
    QCOMPARE(session->selectedLayerIDs(), QSet<QUuid>{pressed});
    item(*menu, "layerVisibility").trigger();
    QVERIFY(!layerWith(*session, pressed).isVisible);
}

void LayerContextMenuEdgeTests::aStaleEffectLeavesItsRowSelected()
{
    // Undo takes the effect; its row shows selected again.
    const auto session = paintedPair();
    Shown shown(*session);
    const QUuid top = shown.row(0).layerID();
    session->selectEffect(LayerEffectKind::stroke, top, false);
    session->undo();
    QVERIFY(session->effectSelection() && !session->selectedEffect());
    session->setRenamingLayerID(top);
    auto *field = shown.row(0).findChild<QLineEdit *>();
    QTRY_VERIFY(field && field->hasFocus());
    field->setText(QStringLiteral("Rename pending"));
    const std::unique_ptr<QMenu> menu = shown.menu(0);
    QVERIFY(field->hasFocus());
    QCOMPARE(session->renamingLayerID(), std::optional(top));
    QCOMPARE(layerWith(*session, top).name, QString("Red"));
    QVERIFY(!item(*menu, "duplicateLayer").isEnabled());
}

void LayerContextMenuEdgeTests::anEntryWithNoActiveLayerDoesNothing()
{
    const auto session = threeLayers();
    Shown shown(*session);
    std::unique_ptr<QMenu> menu = shown.menu(0);
    QVERIFY(item(*menu, "layerVisibility").isEnabled());
    session->selectLayers({}, std::nullopt);
    QVERIFY(!session->activeLayerID());
    const int steps = session->history.undoCount();
    item(*menu, "layerVisibility").trigger();
    for (const ImageLayer &layer : session->document().value().layers)
        QVERIFY(layer.isVisible);
    QCOMPARE(session->history.undoCount(), steps);
}

QTEST_MAIN(LayerContextMenuEdgeTests)
#include "LayerContextMenuEdgeTests.moc"
