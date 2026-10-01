#include "LayerContextMenuFixtures.h"
#include "SessionFixtures.h"
#include <QApplication>
#include <QMenu>
#include <QTimer>
#include <QtTest>

// Swift's layer context menu tests (1.2.5), on the list.

class LayerContextMenuTests : public QObject {
    Q_OBJECT
private slots:
    void testContextMenuContainsCoreLayerActions();
    void testRightClickOnUnselectedLayerSelectsIt();
    void testRightClickInsideMultiSelectionPreservesSelection();
    void testRightClickOutsideRowsReturnsNoMenu();
    void testContextMenuDuplicateUsesExistingDuplicateOperation();
    void testContextMenuMaskActionsFollowLayerState();
    void testContextMenuShowsCreateOrReleaseClippingAction();
    void testContextMenuUsesExistingMergeTitleAndAction();
    void testContextMenuActionsPreserveUndoRedo();
    void entriesFollowTheSelectionAndTheGates();
    void thumbnailsAndEffectsChooseWhatTheMenuTargets();
    void testContextMenuDuplicateFolderPreservesHierarchy();
    void aFoldersMenuOffersUngroup();
};

void LayerContextMenuTests::testContextMenuContainsCoreLayerActions()
{
    const auto session = threeLayers();
    Shown shown(*session);
    const std::unique_ptr<QMenu> menu = shown.menu(0);
    // Swift's order, the Layer Effects entry left out (5d7adba).
    QCOMPARE(titles(*menu), (QStringList{"Duplicate Layer", "Rename…", "Delete Layer", "-", "Create Clipping Mask", "Group Selected Layers",
                                         "Move Out of Folder", "Merge Down", "-", "Add Mask", "Disable Mask", "Delete Mask", "Unlink Mask", "-",
                                         "Hide Layer"}));
    QMenu *add = item(*menu, "addMask").menu();
    QVERIFY(add);
    QCOMPARE(titles(*add), (QStringList{"Reveal All (White)", "Hide All (Black)"}));
}

void LayerContextMenuTests::testRightClickOnUnselectedLayerSelectsIt()
{
    const auto session = threeLayers();
    Shown shown(*session);
    const QUuid a = shown.row(0).layerID(), b = shown.row(1).layerID();
    session->selectLayers({a}, a);
    QVERIFY(shown.menu(1));
    QCOMPARE(session->selectedLayerIDs(), QSet<QUuid>{b});
    QCOMPARE(session->activeLayerID(), std::optional(b));
    // The list takes the keys, as Swift's first responder.
    QTRY_VERIFY(shown.list.hasFocus());
    // A real right-click shows the menu, for the row pressed.
    QStringList shownTitles;
    QTimer::singleShot(0, [&shownTitles] {
        auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (popup) {
            shownTitles = titles(*popup);
            popup->close();
        }
    });
    LayerCell &third = shown.row(2);
    QContextMenuEvent click(QContextMenuEvent::Mouse, QPoint(third.width() - 20, 20), third.mapToGlobal(QPoint(third.width() - 20, 20)));
    QApplication::sendEvent(&third, &click);
    QCOMPARE(shownTitles.value(0), QString("Duplicate Layer"));
    QCOMPARE(session->activeLayerID(), std::optional(third.layerID()));
}

void LayerContextMenuTests::testRightClickInsideMultiSelectionPreservesSelection()
{
    const auto session = threeLayers();
    Shown shown(*session);
    const QSet<QUuid> all{shown.row(0).layerID(), shown.row(1).layerID(), shown.row(2).layerID()};
    session->selectLayers(all, shown.row(0).layerID());
    QVERIFY(shown.menu(1));
    QCOMPARE(session->selectedLayerIDs(), all);
    QCOMPARE(session->activeLayerID(), std::optional(shown.row(1).layerID()));
}

void LayerContextMenuTests::testRightClickOutsideRowsReturnsNoMenu()
{
    const auto session = threeLayers();
    Shown shown(*session);
    const QSet<QUuid> before = session->selectedLayerIDs();
    // Below every row: no menu shows, the rows stay.
    QContextMenuEvent below(QContextMenuEvent::Mouse, QPoint(100, 380), shown.list.viewport()->mapToGlobal(QPoint(100, 380)));
    QApplication::sendEvent(shown.list.viewport(), &below);
    QVERIFY(!QApplication::activePopupWidget());
    QCOMPARE(session->selectedLayerIDs(), before);
}

void LayerContextMenuTests::testContextMenuDuplicateUsesExistingDuplicateOperation()
{
    const auto session = threeLayers();
    Shown shown(*session);
    const ImageLayer target = session->activeLayer().value();
    const size_t count = session->document().value().layers.size();
    const int steps = session->history.undoCount();
    item(*shown.menu(0), "duplicateLayer").trigger();
    const std::vector<ImageLayer> &layers = session->document().value().layers;
    QCOMPARE(layers.size(), count + 1);
    QVERIFY(std::any_of(layers.begin(), layers.end(), [&](const ImageLayer &layer) { return layer.id == target.id; }));
    const ImageLayer copy = session->activeLayer().value();
    QVERIFY(copy.id != target.id);
    QCOMPARE(copy.name, target.name + " copy");
    QCOMPARE(session->history.undoCount(), steps + 1);
    QCOMPARE(session->history.undoName(), QString("Duplicate Layer"));
}

void LayerContextMenuTests::testContextMenuMaskActionsFollowLayerState()
{
    const auto session = threeLayers();
    Shown shown(*session);
    std::unique_ptr<QMenu> menu = shown.menu(0);
    QVERIFY(item(*menu, "addMask").isEnabled() && !item(*menu, "deleteMask").isEnabled() && !item(*menu, "toggleMask").isEnabled());
    item(*menu, "addWhiteMask").trigger();
    QVERIFY(session->activeLayer().value().mask && session->activeLayer().value().mask.value().isEnabled);
    menu = shown.menu(0);
    QVERIFY(!item(*menu, "addMask").isEnabled() && !item(*menu, "addWhiteMask").isEnabled() && !item(*menu, "addBlackMask").isEnabled());
    QVERIFY(item(*menu, "toggleMask").isEnabled() && item(*menu, "deleteMask").isEnabled());
    QCOMPARE(item(*menu, "toggleMask").text(), QString("Disable Mask"));
    item(*menu, "toggleMask").trigger();
    QVERIFY(!session->activeLayer().value().mask.value().isEnabled);
    menu = shown.menu(0);
    QCOMPARE(item(*menu, "toggleMask").text(), QString("Enable Mask"));
    // Link follows the mask; the entries target the pixels first.
    QCOMPARE(item(*menu, "linkMask").text(), QString("Unlink Mask"));
    item(*menu, "linkMask").trigger();
    QVERIFY(!session->activeLayer().value().mask.value().isLinked);
    menu = shown.menu(0);
    QCOMPARE(item(*menu, "linkMask").text(), QString("Link Mask"));
    // With the mask chosen, an entry targets the pixels again.
    session->selectLayerTarget(session->activeLayerID().value(), true);
    item(*menu, "toggleMask").trigger();
    QVERIFY(session->activeLayer().value().mask.value().isEnabled && !session->isMaskSelected());
    session->selectLayerTarget(session->activeLayerID().value(), true);
    item(*menu, "deleteMask").trigger();
    QVERIFY(!session->activeLayer().value().mask && !session->isMaskSelected());
    // Black hides all.
    item(*shown.menu(0), "addBlackMask").trigger();
    QCOMPARE(session->activeLayer().value().mask.value().asset.image().pixelColor(0, 0), QColor(0, 0, 0));
}

void LayerContextMenuTests::testContextMenuShowsCreateOrReleaseClippingAction()
{
    const auto session = threeLayers();
    Shown shown(*session);
    std::unique_ptr<QMenu> menu = shown.menu(0);
    QCOMPARE(item(*menu, "clippingMask").text(), QString("Create Clipping Mask"));
    QVERIFY(item(*menu, "clippingMask").isEnabled());
    item(*menu, "clippingMask").trigger();
    QVERIFY(session->activeLayer().value().maskSourceID);
    menu = shown.menu(0);
    QCOMPARE(item(*menu, "clippingMask").text(), QString("Release Clipping Mask"));
    QVERIFY(item(*menu, "clippingMask").isEnabled());
    item(*menu, "clippingMask").trigger();
    QVERIFY(!session->activeLayer().value().maskSourceID);
    // The bottom layer has nothing to clip to.
    QVERIFY(!item(*shown.menu(2), "clippingMask").isEnabled());
}

void LayerContextMenuTests::testContextMenuUsesExistingMergeTitleAndAction()
{
    const auto session = threeLayers();
    Shown shown(*session);
    std::unique_ptr<QMenu> menu = shown.menu(0);
    QCOMPARE(item(*menu, "mergeLayers").text(), QString("Merge Down"));
    QCOMPARE(item(*menu, "mergeLayers").isEnabled(), session->canMergeLayers());
    session->selectLayers({shown.row(0).layerID(), shown.row(1).layerID()}, shown.row(0).layerID());
    menu = shown.menu(0);
    QCOMPARE(item(*menu, "mergeLayers").text(), QString("Merge Layers"));
    QVERIFY(item(*menu, "mergeLayers").isEnabled() && session->canMergeLayers());
    const size_t before = session->document().value().layers.size();
    item(*menu, "mergeLayers").trigger();
    QCOMPARE(session->document().value().layers.size(), before - 1);
}

void LayerContextMenuTests::testContextMenuActionsPreserveUndoRedo()
{
    const auto session = threeLayers();
    Shown shown(*session);
    const std::vector<ImageLayer> initial = session->document().value().layers;
    item(*shown.menu(0), "duplicateLayer").trigger();
    QCOMPARE(session->document().value().layers.size(), size_t(4));
    session->undo();
    QVERIFY(session->document().value().layers == initial);
    session->redo();
    QCOMPARE(session->document().value().layers.size(), size_t(4));
    session->undo();
    item(*shown.menu(0), "addWhiteMask").trigger();
    QVERIFY(session->activeLayer().value().mask);
    session->undo();
    QVERIFY(!session->activeLayer().value().mask);
    const bool wasVisible = session->activeLayer().value().isVisible;
    QCOMPARE(item(*shown.menu(0), "layerVisibility").text(), QString("Hide Layer"));
    item(*shown.menu(0), "layerVisibility").trigger();
    QCOMPARE(session->activeLayer().value().isVisible, !wasVisible);
    QCOMPARE(item(*shown.menu(0), "layerVisibility").text(), QString("Show Layer"));
    session->undo();
    QCOMPARE(session->activeLayer().value().isVisible, wasVisible);
}

void LayerContextMenuTests::entriesFollowTheSelectionAndTheGates()
{
    const auto session = threeLayers();
    Shown shown(*session);
    // Rename for one layer; Delete names a selection or mask.
    std::unique_ptr<QMenu> menu = shown.menu(0);
    QVERIFY(item(*menu, "renameLayer").isEnabled() && item(*menu, "deleteLayer").text() == "Delete Layer");
    item(*menu, "renameLayer").trigger();
    QCOMPARE(session->renamingLayerID(), std::optional(shown.row(0).layerID()));
    session->setRenamingLayerID(std::nullopt);
    session->selectLayers({shown.row(0).layerID(), shown.row(1).layerID()}, shown.row(0).layerID());
    menu = shown.menu(1);
    QVERIFY(!item(*menu, "renameLayer").isEnabled() && item(*menu, "deleteLayer").text() == "Delete Selected Layers");
    item(*menu, "deleteLayer").trigger();
    QCOMPARE(session->document().value().layers.size(), size_t(1));
    // A chosen mask names Delete Mask.
    item(*shown.menu(0), "addWhiteMask").trigger();
    session->selectLayerTarget(shown.row(0).layerID(), true);
    menu = shown.list.contextMenu(shown.row(0).layerID());
    QCOMPARE(item(*menu, "deleteLayer").text(), QString("Delete Mask"));
    // Group, then Move Out for the layer inside.
    QVERIFY(item(*menu, "groupLayers").isEnabled() && !item(*menu, "moveOut").isEnabled());
    const QUuid child = shown.row(0).layerID();
    item(*menu, "groupLayers").trigger();
    QVERIFY(layerWith(*session, child).parentID.has_value());
    session->selectLayer(child);
    menu = shown.list.contextMenu(child);
    QVERIFY(item(*menu, "moveOut").isEnabled());
    item(*menu, "moveOut").trigger();
    QVERIFY(!session->activeLayer().value().parentID);
    // While a project is busy, the layer entries rest.
    session->groupSelectedLayers();
    session->selectLayer(child);
    session->addBlankLayer();
    const QUuid blank = session->activeLayerID().value();
    menu = shown.list.contextMenu(blank);
    QVERIFY(item(*menu, "moveOut").isEnabled() && item(*menu, "mergeLayers").isEnabled());
    session->setIsProjectBusy(true);
    menu = shown.list.contextMenu(blank);
    for (const char *name : {"duplicateLayer", "renameLayer", "deleteLayer", "groupLayers", "moveOut", "mergeLayers", "linkMask", "layerVisibility"})
        QVERIFY2(!item(*menu, name).isEnabled(), name);
    session->setIsProjectBusy(false);
    // An adjustment's mask never links, as Swift's.
    session->addAdjustment(AdjustmentKind::levels);
    session->setAdjustmentEditingID(std::nullopt);
    session->addMask(true);
    menu = shown.list.contextMenu(session->activeLayerID().value());
    QVERIFY(session->activeLayer().value().mask && !item(*menu, "linkMask").isEnabled() && item(*menu, "deleteMask").isEnabled());
}

void LayerContextMenuTests::thumbnailsAndEffectsChooseWhatTheMenuTargets()
{
    const auto session = threeLayers();
    session->addMask(true);
    Shown shown(*session);
    const QUuid top = shown.row(0).layerID();
    const auto at = [&](const char *name) {
        auto *button = shown.row(0).findChild<QWidget *>(QString::fromLatin1(name));
        return button->mapTo(&shown.row(0), button->rect().center());
    };
    // A selected row's mask thumbnail targets its mask; pixels, pixels.
    shown.list.menuFor(shown.row(0), at("maskThumbnail"));
    QVERIFY(session->isMaskSelected());
    shown.list.menuFor(shown.row(0), at("layerThumbnail"));
    QVERIFY(!session->isMaskSelected());
    // An unselected row's mask: it alone, its mask.
    session->selectLayer(shown.row(1).layerID());
    shown.list.menuFor(shown.row(0), at("maskThumbnail"));
    QVERIFY(session->selectedLayerIDs() == QSet<QUuid>{top} && session->isMaskSelected());
    // An effect's row chooses that effect; effects need pixels.
    EditorSession painted;
    painted.createDocument(8, 8);
    for (int layer = 0; layer < 2; ++layer) {
        QImage pixels(8, 8, QImage::Format_RGBA8888_Premultiplied);
        pixels.fill(Qt::red);
        painted.insert(ImportedImage(pixels, pixels, QStringLiteral("Red")));
    }
    painted.setEffects(LayerEffects{.stroke = StrokeEffect()});
    Shown both(painted);
    painted.selectLayer(both.row(1).layerID());
    auto *effect = both.row(0).findChild<LayerEffectRow *>();
    QVERIFY(effect);
    QVERIFY(both.list.menuFor(both.row(0), effect->mapTo(&both.row(0), effect->rect().center())));
    QVERIFY(painted.effectSelection() && painted.effectSelection().value().kind == LayerEffectKind::stroke);
    QCOMPARE(painted.effectSelection().value().layerID, both.row(0).layerID());
    // Anywhere else drops the chosen effect.
    both.menu(1);
    QVERIFY(!painted.effectSelection());
}

void LayerContextMenuTests::testContextMenuDuplicateFolderPreservesHierarchy()
{
    const auto session = threeLayers();
    QSet<QUuid> all;
    for (const ImageLayer &layer : session->document().value().layers)
        all.insert(layer.id);
    session->selectLayers(all, session->activeLayerID());
    session->groupSelectedLayers();
    const QUuid folder = session->activeLayerID().value();
    Shown shown(*session);
    std::unique_ptr<QMenu> menu = shown.list.contextMenu(folder);
    QVERIFY(item(*menu, "duplicateLayer").isEnabled());
    const size_t children = session->document().value().layers.size() - 1;
    item(*menu, "duplicateLayer").trigger();
    const ImageLayer copy = session->activeLayer().value();
    QVERIFY(copy.id != folder && copy.isGroup);
    size_t copied = 0;
    for (const ImageLayer &layer : session->document().value().layers)
        copied += layer.parentID == copy.id;
    QCOMPARE(copied, children);
    QCOMPARE(session->document().value().layers.size(), 2 * children + 2);
}

void LayerContextMenuTests::aFoldersMenuOffersUngroup()
{
    const auto session = threeLayers();
    const QUuid lower = session->document().value().layers[0].id;
    session->selectLayers({lower}, lower);
    session->groupSelectedLayers();
    Shown shown(*session);
    // The folder sits lowest; the top row is none.
    session->selectLayer(std::nullopt);
    QVERIFY(!titles(*shown.menu(0)).contains("Ungroup Layers"));
    const int folder = int(shown.list.cells().size()) - 2;
    QVERIFY(shown.row(folder).layerID() != lower);
    std::unique_ptr<QMenu> menu = shown.menu(folder);
    const QStringList entries = titles(*menu);
    QCOMPARE(entries.indexOf("Ungroup Layers"), entries.indexOf("Group Selected Layers") + 1);
    QVERIFY(item(*menu, "ungroupLayers").isEnabled());
    session->setIsProjectBusy(true);
    QVERIFY(!item(*shown.menu(folder), "ungroupLayers").isEnabled());
    session->setIsProjectBusy(false);
    item(*shown.menu(folder), "ungroupLayers").trigger();
    QCOMPARE(session->history.undoName(), QString("Ungroup Layers"));
    QCOMPARE(session->document().value().layers.size(), size_t(3));
    QCOMPARE(session->selectedLayerIDs(), QSet<QUuid>{lower});
}

QTEST_MAIN(LayerContextMenuTests)
#include "LayerContextMenuTests.moc"
