#include "SessionFixtures.h"
#include <QtTest>

// Mask edits in the session beyond Swift's own cases.
class LayerMaskEditTests : public QObject {
    Q_OBJECT
private slots:
    void masksAreNamedStepsAndRefusedBehindGates();
    void aMaskCopiesToAnotherLayerWhereItSits();
    void theMaskTargetFollowsTheLayerAndItsMask();
    void aGroupMoveCarriesLinkedMasksAndLeavesUnlinkedOnes();
    void aMaskAloneHasNoPixelSizeAndItsOwnHandles();
    void maskStepsEndAnOpenOpacityDragFirst();
    void aTargetIsChosenOnAnyLayer();
    void whatOnlyNeedsEditableLayersWorksWithSeveralSelected();
};

void LayerMaskEditTests::masksAreNamedStepsAndRefusedBehindGates()
{
    EditorSession empty;
    empty.addLayerMask();
    empty.toggleLayerMask();
    empty.deleteLayerMask();
    empty.toggleMaskLink(QUuid::createUuid());
    QVERIFY(!empty.canEditMask() && !empty.document().has_value());
    const std::unique_ptr<EditorSession> session = redSession();
    const QUuid id = session->activeLayerID().value();
    // No mask: none to toggle, delete or unlink.
    const int count = session->history.undoCount();
    session->toggleLayerMask();
    session->deleteLayerMask();
    session->toggleMaskLink(id);
    QCOMPARE(session->history.undoCount(), count);
    const QStringList names{"Add Reveal-All Mask", "Disable Layer Mask", "Enable Layer Mask", "Unlink Layer Mask", "Link Layer Mask", "Delete Layer Mask"};
    const std::vector<std::function<void()>> steps{[&] { session->addLayerMask(); }, [&] { session->toggleLayerMask(); },
                                                   [&] { session->toggleLayerMask(); }, [&] { session->toggleMaskLink(id); },
                                                   [&] { session->toggleMaskLink(id); }, [&] { session->deleteLayerMask(); }};
    for (size_t index = 0; index < steps.size(); ++index) {
        steps[index]();
        QCOMPARE(session->history.undoCount(), count + int(index) + 1);
        QCOMPARE(session->history.undoName(), names[qsizetype(index)]);
    }
    session->addLayerMask(false);
    QCOMPARE(session->history.undoName(), QString("Add Hide-All Mask"));
    // A link change ends an open opacity drag first.
    session->beginOpacityEdit();
    session->setLayerOpacity(0.5);
    session->toggleMaskLink(id);
    QVERIFY(session->canUndo());
    QCOMPARE(session->history.undoName(), QString("Unlink Layer Mask"));
    session->undo();
    QCOMPARE(session->history.undoName(), QString("Layer Opacity"));
    // Several layers, or a gate, and no mask is edited.
    session->insert(ImportedImage(QImage(2, 2, QImage::Format_RGBA8888_Premultiplied), QImage(), "Other"));
    const QUuid other = session->activeLayerID().value();
    session->selectLayers({id, other}, id);
    QVERIFY(!session->canEditMask());
    const std::optional<CanvasDocument> before = session->document();
    session->toggleLayerMask();
    session->deleteLayerMask();
    session->selectLayer(other);
    session->setIsImporting(true);
    session->addLayerMask();
    session->toggleMaskLink(id);
    session->selectLayerTarget(id, true);
    QCOMPARE(session->document(), before);
    QCOMPARE(session->activeLayerID(), std::optional(other));
    session->setIsImporting(false);
    session->setIsProjectBusy(true);
    session->selectLayerTarget(id, true);
    QCOMPARE(session->activeLayerID(), std::optional(other));
    QVERIFY(!session->isMaskSelected());
}

void LayerMaskEditTests::aMaskCopiesToAnotherLayerWhereItSits()
{
    const std::unique_ptr<EditorSession> session = redSession();
    const QUuid source = session->activeLayerID().value();
    session->insert(ImportedImage(QImage(2, 2, QImage::Format_RGBA8888_Premultiplied), QImage(), "Target"), QPointF(2, 1));
    const QUuid target = session->activeLayerID().value();
    session->addGroup();
    const QUuid folder = session->activeLayerID().value();
    // No mask yet; never onto itself or a folder.
    QVERIFY(!session->canCopyMask(source, target));
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, source, coverage());
        record(snapshot, source).maskEnabled = false;
        record(snapshot, source).maskLinked = false;
    });
    QVERIFY(session->canCopyMask(source, target));
    QVERIFY(!session->canCopyMask(source, source) && !session->canCopyMask(source, folder));
    QVERIFY(!session->canCopyMask(target, source) && !session->canCopyMask(source, QUuid::createUuid()));
    session->selectLayer(source);
    session->beginOpacityEdit();
    session->copyMask(source, target);
    QCOMPARE(session->history.undoName(), QString("Copy Layer Mask"));
    QVERIFY(session->canUndo());
    // Same flags and pixels, placed where the source sits.
    const LayerMask original = layerWith(*session, source).mask.value();
    const LayerMask copy = layerWith(*session, target).mask.value();
    QVERIFY(copy.asset.identity() == original.asset.identity());
    QVERIFY(!copy.isEnabled && !copy.isLinked);
    QCOMPARE(copy.placement, std::optional(layerWith(*session, source).transform));
    QCOMPARE(original.placement, std::nullopt);
    QCOMPARE(session->activeLayerID(), std::optional(target));
    QVERIFY(session->isMaskSelected());
    // Onto a masked layer the copy replaces the mask.
    session->copyMask(target, source);
    QCOMPARE(session->history.undoName(), QString("Replace Layer Mask"));
    QCOMPARE(layerWith(*session, source).mask.value().placement, copy.placement);
    // Neither end active: the named source gives pixels and place.
    session->insert(ImportedImage(QImage(2, 2, QImage::Format_RGBA8888_Premultiplied), QImage(), "Third"));
    const QUuid third = session->activeLayerID().value();
    QImage flat(3, 3, QImage::Format_Grayscale8);
    flat.fill(40);
    const LayerTransform far{.origin = {9, 8}, .size = {3, 3}};
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, third, LayerMask::assetFrom(flat));
        record(snapshot, third).maskPlacement = far;
    });
    session->addBlankLayer();
    const QUuid active = session->activeLayerID().value();
    session->addLayerMask(false);
    session->copyMask(third, target);
    const LayerMask landed = layerWith(*session, target).mask.value();
    QVERIFY(landed.asset.identity() == layerWith(*session, third).mask.value().asset.identity());
    QCOMPARE(landed.placement, std::optional(far));
    QVERIFY(landed.isEnabled && landed.isLinked);
    QVERIFY(layerWith(*session, active).mask.value().asset.identity() != landed.asset.identity());
    session->setShowsImporter(true);
    QVERIFY(!session->canCopyMask(source, target));
}

void LayerMaskEditTests::theMaskTargetFollowsTheLayerAndItsMask()
{
    const std::unique_ptr<EditorSession> session = redSession();
    const QUuid id = session->activeLayerID().value();
    session->selectLayerTarget(id, true);
    QVERIFY(!session->isMaskSelected());
    session->addLayerMask();
    QVERIFY(session->isMaskSelected());
    // Undo takes the mask away and the target with it.
    session->undo();
    QVERIFY(!session->isMaskSelected());
    session->redo();
    QVERIFY(!session->isMaskSelected());
    session->selectLayerTarget(id, true);
    session->toggleLayerMask();
    session->undo();
    // Layer and mask remain: the mask stays the target.
    QVERIFY(session->isMaskSelected());
    session->insert(ImportedImage(QImage(2, 2, QImage::Format_RGBA8888_Premultiplied), QImage(), "Other"));
    QVERIFY(!session->isMaskSelected());
    session->undo();
    QVERIFY(!session->isMaskSelected());
    // Not the target before an undo, not the target after.
    session->selectLayerTarget(id, false);
    session->toggleLayerMask();
    session->undo();
    QVERIFY(!session->isMaskSelected());
    // An undo that brings back another active layer drops it.
    session->insert(ImportedImage(QImage(2, 2, QImage::Format_RGBA8888_Premultiplied), QImage(), "Second"));
    const QUuid other = session->activeLayerID().value();
    session->selectLayerTarget(id, true);
    session->copyMask(id, other);
    QVERIFY(session->isMaskSelected() && session->activeLayerID() == std::optional(other));
    session->undo();
    QCOMPARE(session->activeLayerID(), std::optional(id));
    QVERIFY(session->activeLayer().value().mask.has_value() && !session->isMaskSelected());
    // The same layer keeps the target; another drops it.
    session->selectLayerTarget(id, true);
    session->selectLayer(id);
    QVERIFY(session->isMaskSelected());
    session->selectLayer(std::nullopt);
    QVERIFY(!session->isMaskSelected());
    session->selectLayerTarget(id, true);
    const ProjectSnapshot snapshot = session->projectSnapshot().value();
    session->installProject(snapshot, "fixture.comp");
    QVERIFY(!session->isMaskSelected());
    session->selectLayerTarget(id, true);
    session->clearProject();
    QVERIFY(!session->isMaskSelected());
}

void LayerMaskEditTests::aGroupMoveCarriesLinkedMasksAndLeavesUnlinkedOnes()
{
    EditorSession session;
    session.createDocument(100, 100);
    const auto inserted = [&](QPointF center) {
        session.insert(ImportedImage(QImage(20, 10, QImage::Format_RGBA8888_Premultiplied), QImage(), "Image"), center);
        return session.activeLayerID().value();
    };
    const QUuid linked = inserted({20, 20}), unlinked = inserted({60, 60}), apart = inserted({50, 20});
    QImage gray(20, 10, QImage::Format_Grayscale8);
    gray.fill(90);
    const LayerTransform moved{.origin = {40, 5}, .size = {20, 10}};
    rewrite(session, [&](ProjectSnapshot &snapshot) {
        for (const QUuid id : {linked, unlinked, apart})
            setMask(snapshot, id, LayerMask::assetFrom(gray));
        record(snapshot, linked).maskPlacement = moved;
        record(snapshot, unlinked).maskLinked = false;
        record(snapshot, apart).maskPlacement = LayerTransform{.origin = {3, 70}, .size = {10, 5}};
        // Switched off, a mask keeps its place and rides along.
        record(snapshot, apart).maskEnabled = false;
        record(snapshot, linked).maskEnabled = false;
    });
    const std::optional<LayerTransform> aside = layerWith(session, apart).mask.value().placement;
    // A layer without a mask has no placement to show.
    const QUuid bare = inserted({80, 80});
    QCOMPARE(session.displayedMaskPlacement(layerWith(session, bare)), std::nullopt);
    const LayerTransform stayed = layerWith(session, unlinked).transform;
    session.selectLayers({linked, unlinked}, linked);
    session.beginTransform();
    LayerTransform draft = session.transformEdit().value().draft;
    draft.origin += QPointF(7, -3);
    session.previewTransform(draft);
    // A linked mask rides along; an unlinked one stays.
    LayerTransform ridden = moved;
    ridden.origin += QPointF(7, -3);
    QCOMPARE(session.displayedMaskPlacement(layerWith(session, linked)), std::optional(ridden));
    QCOMPARE(session.displayedMaskPlacement(layerWith(session, unlinked)), std::optional(layerWith(session, unlinked).transform));
    // A mask outside the group keeps the place it has.
    QCOMPARE(session.displayedMaskPlacement(layerWith(session, apart)), aside);
    QVERIFY(aside.has_value());
    session.commitTransform();
    QCOMPARE(layerWith(session, linked).mask.value().placement, std::optional(ridden));
    QCOMPARE(layerWith(session, unlinked).mask.value().placement, std::optional(stayed));
    QCOMPARE(layerWith(session, apart).mask.value().placement, aside);
    QCOMPARE(layerWith(session, unlinked).transform.origin, stayed.origin + QPointF(7, -3));
}

void LayerMaskEditTests::aMaskAloneHasNoPixelSizeAndItsOwnHandles()
{
    const std::unique_ptr<EditorSession> session = redSession();
    const QUuid id = session->activeLayerID().value();
    const LayerTransform placement{.origin = {1, 0}, .size = {4, 3}};
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, id, coverage());
        record(snapshot, id).maskPlacement = placement;
    });
    // Linked, the mask goes with its layer whatever is selected.
    session->selectLayerTarget(id, true);
    QVERIFY(!session->transformTargetsMask());
    QCOMPARE(session->transformPixelSize(), std::optional(QSizeF(2, 2)));
    QCOMPARE(session->editedTransform(layerWith(*session, id)), layerWith(*session, id).transform);
    session->toggleMaskLink(id);
    // Unlinked: only with the mask as the target.
    session->selectLayerTarget(id, false);
    QVERIFY(!session->transformTargetsMask());
    session->selectLayerTarget(id, true);
    QVERIFY(session->transformTargetsMask());
    QCOMPARE(session->transformPixelSize(), std::nullopt);
    QCOMPARE(session->editedTransform(layerWith(*session, id)), placement);
    session->beginTransform();
    QCOMPARE(session->transformEdit().value().draft, placement);
    QVERIFY(session->transformEdit().value().mask);
    // The open edit decides, not what is selected meanwhile.
    session->selectLayerTarget(id, false);
    QVERIFY(session->transformEdit().has_value() && !session->isMaskSelected());
    QVERIFY(session->transformTargetsMask());
    QCOMPARE(session->editedTransform(layerWith(*session, id)), placement);
    QCOMPARE(session->transformPixelSize(), std::nullopt);
    QCOMPARE(session->pendingTransform(layerWith(*session, id)), std::nullopt);
    QCOMPARE(session->displayedMaskPlacement(layerWith(*session, id)), std::optional(placement));
    const int count = session->history.undoCount();
    session->commitTransform();
    QCOMPARE(session->history.undoCount(), count);
    // Another masked layer neither shows nor takes this edit.
    session->insert(ImportedImage(QImage(2, 2, QImage::Format_RGBA8888_Premultiplied), QImage(), "Other"));
    const QUuid other = session->activeLayerID().value();
    const LayerTransform elsewhere{.origin = {0, 1}, .size = {2, 2}};
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, other, coverage());
        record(snapshot, other).maskPlacement = elsewhere;
        record(snapshot, other).maskLinked = false;
    });
    session->selectLayerTarget(id, true);
    // Its handles stay on its own pixels meanwhile.
    QVERIFY(session->transformTargetsMask());
    QCOMPARE(session->editedTransform(layerWith(*session, other)), layerWith(*session, other).transform);
    session->beginTransform();
    session->nudgeLayer(3, 0);
    QCOMPARE(session->displayedMaskPlacement(layerWith(*session, other)), std::optional(elsewhere));
    // A layer's own edit stays one, whatever gets selected.
    session->commitTransform();
    session->selectLayerTarget(id, false);
    session->beginTransform();
    session->selectLayerTarget(id, true);
    QVERIFY(session->isMaskSelected() && !session->transformTargetsMask());
    session->cancelTransform();
    session->selectLayerTarget(id, true);
    session->beginTransform();
    session->nudgeLayer(0, 0);
    session->commitTransform();
    QCOMPARE(layerWith(*session, other).mask.value().placement, std::optional(elsewhere));
    QCOMPARE(layerWith(*session, id).mask.value().placement.value().origin, QPointF(4, 0));
    // Back over its layer the mask has no placement again.
    session->beginTransform();
    session->previewTransform(layerWith(*session, id).transform);
    QCOMPARE(session->displayedMaskPlacement(layerWith(*session, id)), std::nullopt);
    session->commitTransform();
    QCOMPARE(layerWith(*session, id).mask.value().placement, std::nullopt);
    QCOMPARE(session->history.undoCount(), count + 1);
}

void LayerMaskEditTests::maskStepsEndAnOpenOpacityDragFirst()
{
    const std::unique_ptr<EditorSession> session = redSession();
    const int count = session->history.undoCount();
    const std::vector<std::pair<QString, std::function<void()>>> steps{
        {"Add Reveal-All Mask", [&] { session->addLayerMask(); }},
        {"Disable Layer Mask", [&] { session->toggleLayerMask(); }},
        {"Delete Layer Mask", [&] { session->deleteLayerMask(); }}};
    int expected = count;
    for (const auto &[name, step] : steps) {
        // The drag is its own step, under the mask's.
        session->beginOpacityEdit();
        session->setLayerOpacity(0.5 - 0.1 * (expected - count));
        step();
        expected += 2;
        QVERIFY(session->canUndo());
        QCOMPARE(session->history.undoCount(), expected);
        QCOMPARE(session->history.undoName(), name);
        session->undo();
        QCOMPARE(session->history.undoName(), QString("Layer Opacity"));
        session->redo();
    }
}

void LayerMaskEditTests::aTargetIsChosenOnAnyLayer()
{
    const std::unique_ptr<EditorSession> session = redSession();
    const QUuid masked = session->activeLayerID().value();
    session->addLayerMask();
    session->insert(ImportedImage(QImage(2, 2, QImage::Format_RGBA8888_Premultiplied), QImage(), "Bare"));
    const QUuid bare = session->activeLayerID().value();
    // The layer becomes active; its mask, if any, the target.
    session->selectLayerTarget(masked, true);
    QCOMPARE(session->activeLayerID(), std::optional(masked));
    QVERIFY(session->isMaskSelected());
    session->selectLayerTarget(bare, true);
    QCOMPARE(session->activeLayerID(), std::optional(bare));
    QVERIFY(!session->isMaskSelected());
    session->selectLayerTarget(masked, false);
    QCOMPARE(session->activeLayerID(), std::optional(masked));
    QVERIFY(!session->isMaskSelected());
    // The link toggles on the layer named, active or not.
    session->selectLayer(bare);
    session->addLayerMask();
    session->toggleMaskLink(masked);
    QVERIFY(!layerWith(*session, masked).mask.value().isLinked);
    QVERIFY(layerWith(*session, bare).mask.value().isLinked);
    QCOMPARE(session->activeLayerID(), std::optional(bare));
    // An id the document lacks leaves no mask to target.
    session->selectLayerTarget(QUuid::createUuid(), true);
    QVERIFY(!session->isMaskSelected() && !session->activeLayer().has_value());
    // One id selected, yet no layer: no mask to edit.
    QCOMPARE(int(session->selectedLayerIDs().size()), 1);
    QVERIFY(session->canEditLayers() && !session->canEditMask());
    const std::optional<CanvasDocument> kept = session->document();
    session->addLayerMask();
    QCOMPARE(session->document(), kept);
}

void LayerMaskEditTests::whatOnlyNeedsEditableLayersWorksWithSeveralSelected()
{
    const std::unique_ptr<EditorSession> session = redSession();
    const QUuid red = session->activeLayerID().value();
    session->insert(ImportedImage(QImage(2, 2, QImage::Format_RGBA8888_Premultiplied), QImage(), "Target"));
    const QUuid target = session->activeLayerID().value();
    session->selectLayer(std::nullopt);
    session->addGroup();
    const QUuid folder = session->activeLayerID().value();
    const LayerTransform placed{.origin = {5, 6}, .size = {2, 2}};
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, folder, coverage());
        record(snapshot, folder).maskPlacement = placed;
    });
    // Several layers selected: no one mask to edit; these work.
    session->selectLayers({red, target}, red);
    QVERIFY(session->canEditLayers() && !session->canEditMask());
    // A folder's mask is a source like any other.
    QVERIFY(session->canCopyMask(folder, target));
    session->copyMask(folder, target);
    const LayerMask copy = layerWith(*session, target).mask.value();
    QVERIFY(copy.asset.identity() == layerWith(*session, folder).mask.value().asset.identity());
    QCOMPARE(copy.placement, std::optional(placed));
    QCOMPARE(session->history.undoName(), QString("Copy Layer Mask"));
    session->selectLayers({red, target}, red);
    session->toggleMaskLink(target);
    QVERIFY(!layerWith(*session, target).mask.value().isLinked);
    session->toggleMaskLink(folder);
    QVERIFY(!layerWith(*session, folder).mask.value().isLinked);
    QCOMPARE(session->history.undoName(), QString("Unlink Layer Mask"));
    // A target is chosen even while a sheet is up.
    session->setShowsImporter(true);
    QVERIFY(!session->canEditLayers());
    session->selectLayerTarget(target, true);
    QCOMPARE(session->activeLayerID(), std::optional(target));
    QVERIFY(session->isMaskSelected());
}

QTEST_GUILESS_MAIN(LayerMaskEditTests)
#include "LayerMaskEditTests.moc"
