#include "Document/LiveLayerMask.h"
#include "SessionFixtures.h"
#include <QtTest>

// Clipping in the session beyond Swift's own cases.
namespace {
QUuid image(EditorSession &session, const QString &name)
{
    session.insert(ImportedImage(QImage(2, 2, QImage::Format_RGBA8888_Premultiplied), QImage(), name));
    return session.activeLayerID().value();
}

// Each layer's base by name, bottom up; "-" for none.
QStringList bases(const EditorSession &session)
{
    QStringList result;
    for (const ImageLayer &layer : session.document().value().layers) {
        QString base = "-";
        for (const ImageLayer &other : session.document().value().layers) {
            if (layer.maskSourceID == std::optional(other.id))
                base = other.name;
        }
        result << layer.name + ":" + base;
    }
    return result;
}
}

class LiveMaskEditTests : public QObject {
    Q_OBJECT
private slots:
    void linksAreRefusedForFoldersStrangersAndCycles();
    void aLinkIsOneNamedStepAndTheSameLinkIsNone();
    void releasingABaseFreesItsStackAndAChildOnlyWhatIsAbove();
    void clippingStaysAmongSiblings();
    void aLayerDroppedIntoAStackJoinsIt();
    void adoptionNeedsTheStackOnBothSides();
    void aStackBrokenByAMoveLetsGo();
    void deletingABaseFreesItsDependentsUnbakedOrBaked();
    void aFolderWithTheNullIdKeepsItsOwnRuns();
    void clippingWorksWithSeveralLayersSelected();
};

void LiveMaskEditTests::linksAreRefusedForFoldersStrangersAndCycles()
{
    EditorSession empty;
    QVERIFY(!empty.canLinkMask(QUuid::createUuid(), QUuid::createUuid()) && !empty.linkMask(QUuid::createUuid(), QUuid::createUuid()));
    EditorSession session;
    session.createDocument(8, 8);
    const QUuid a = image(session, "A"), b = image(session, "B"), c = image(session, "C");
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    QVERIFY(session.canLinkMask(a, b) && session.canLinkMask(b, a));
    QVERIFY(!session.canLinkMask(a, a));
    QVERIFY(!session.canLinkMask(folder, a) && !session.canLinkMask(a, folder));
    QVERIFY(!session.canLinkMask(QUuid::createUuid(), a) && !session.canLinkMask(a, QUuid::createUuid()));
    // A chain is fine; closing it into a ring fails.
    QVERIFY(session.linkMask(a, b));
    QVERIFY(session.linkMask(b, c));
    QVERIFY(!session.canLinkMask(c, a) && !session.linkMask(c, a));
    QCOMPARE(bases(session), (QStringList{"A:-", "B:A", "C:B", "Folder 1:-"}));
    session.setShowsImporter(true);
    QVERIFY(!session.canLinkMask(a, c) && !session.canToggleClippingMask(b));
    const std::optional<CanvasDocument> kept = session.document();
    session.toggleClippingMask(b);
    session.removeLiveMask(b);
    QCOMPARE(session.document(), kept);
}

void LiveMaskEditTests::aLinkIsOneNamedStepAndTheSameLinkIsNone()
{
    EditorSession session;
    session.createDocument(8, 8);
    const QUuid a = image(session, "A"), b = image(session, "B"), c = image(session, "C");
    const int count = session.history.undoCount();
    QVERIFY(session.linkMask(a, c));
    QCOMPARE(session.history.undoCount(), count + 1);
    QCOMPARE(session.history.undoName(), QString("Create Clipping Mask"));
    // Linked already: true, and no step.
    QVERIFY(session.linkMask(a, c));
    QCOMPARE(session.history.undoCount(), count + 1);
    // Another source replaces the first.
    QVERIFY(session.linkMask(b, c));
    QCOMPARE(bases(session), (QStringList{"A:-", "B:-", "C:B"}));
    session.removeLiveMask(c);
    QCOMPARE(session.history.undoName(), QString("Release Clipping Mask"));
    QCOMPARE(bases(session), (QStringList{"A:-", "B:-", "C:-"}));
    // Nothing clipped, nothing to release.
    const int steps = session.history.undoCount();
    session.removeLiveMask(c);
    session.removeLiveMask(QUuid::createUuid());
    QCOMPARE(session.history.undoCount(), steps);
}

void LiveMaskEditTests::releasingABaseFreesItsStackAndAChildOnlyWhatIsAbove()
{
    EditorSession session;
    session.createDocument(8, 8);
    const QUuid a = image(session, "A"), b = image(session, "B"), c = image(session, "C"), d = image(session, "D");
    for (const QUuid id : {b, c, d})
        session.toggleClippingMask(id);
    QCOMPARE(bases(session), (QStringList{"A:-", "B:A", "C:A", "D:A"}));
    // From C up: B below stays clipped.
    session.toggleClippingMask(c);
    QCOMPARE(bases(session), (QStringList{"A:-", "B:A", "C:-", "D:-"}));
    session.undo();
    // A layer clipped to another base above ends the run.
    QVERIFY(session.linkMask(b, d));
    QCOMPARE(bases(session), (QStringList{"A:-", "B:A", "C:A", "D:B"}));
    session.removeLiveMask(b);
    QCOMPARE(bases(session), (QStringList{"A:-", "B:-", "C:-", "D:B"}));
    Q_UNUSED(a);
}

void LiveMaskEditTests::clippingStaysAmongSiblings()
{
    EditorSession session;
    session.createDocument(8, 8);
    const QUuid below = image(session, "Below");
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    const QUuid first = image(session, "First"), second = image(session, "Second");
    session.selectLayer(std::nullopt);
    const QUuid above = image(session, "Above");
    // The lowest in a folder has nothing to clip to.
    QVERIFY(!session.canToggleClippingMask(first) && session.canToggleClippingMask(second));
    session.toggleClippingMask(first);
    QCOMPARE(layerWith(session, first).maskSourceID, std::nullopt);
    session.toggleClippingMask(second);
    QCOMPARE(layerWith(session, second).maskSourceID, std::optional(first));
    // Nothing clips to a folder below; no folder clips.
    QVERIFY(!session.canToggleClippingMask(above) && !session.canToggleClippingMask(folder));
    const int steps = session.history.undoCount();
    session.toggleClippingMask(above);
    session.toggleClippingMask(folder);
    QCOMPARE(layerWith(session, above).maskSourceID, std::nullopt);
    QCOMPARE(layerWith(session, folder).maskSourceID, std::nullopt);
    QCOMPARE(session.history.undoCount(), steps);
    QVERIFY(!session.canToggleClippingMask(below) && !session.canToggleClippingMask(QUuid::createUuid()));
    // A clipped layer can always be freed.
    QVERIFY(session.canToggleClippingMask(second));
    // Releasing in the folder leaves the root's stacks alone.
    session.selectLayer(std::nullopt);
    const QUuid top = image(session, "Top");
    session.toggleClippingMask(top);
    QCOMPARE(layerWith(session, top).maskSourceID, std::optional(above));
    session.toggleClippingMask(second);
    QCOMPARE(layerWith(session, second).maskSourceID, std::nullopt);
    QCOMPARE(layerWith(session, top).maskSourceID, std::optional(above));
    // A folder's child lying between breaks no root run.
    EditorSession mixed;
    mixed.createDocument(8, 8);
    mixed.addGroup();
    const QUuid holder = mixed.activeLayerID().value();
    mixed.selectLayer(std::nullopt);
    const QUuid a = image(mixed, "A"), b = image(mixed, "B");
    mixed.selectLayer(holder);
    const QUuid x = image(mixed, "X");
    mixed.selectLayer(b);
    const QUuid c = image(mixed, "C");
    mixed.toggleClippingMask(b);
    mixed.toggleClippingMask(c);
    QCOMPARE(bases(mixed), (QStringList{"Folder 1:-", "A:-", "B:A", "X:-", "C:A"}));
    mixed.removeLiveMask(b);
    QCOMPARE(bases(mixed), (QStringList{"Folder 1:-", "A:-", "B:-", "X:-", "C:-"}));
    // Nor is it freed when it shares the base.
    mixed.undo();
    QVERIFY(mixed.linkMask(a, x));
    mixed.removeLiveMask(b);
    QCOMPARE(bases(mixed), (QStringList{"Folder 1:-", "A:-", "B:-", "X:A", "C:-"}));
}

void LiveMaskEditTests::aLayerDroppedIntoAStackJoinsIt()
{
    EditorSession session;
    session.createDocument(8, 8);
    const QUuid a = image(session, "A"), b = image(session, "B"), c = image(session, "C"), loose = image(session, "Loose");
    session.toggleClippingMask(b);
    session.toggleClippingMask(c);
    // Between B and C, both clipped to A.
    QVERIFY(session.placeLayer(loose, std::nullopt, b));
    QCOMPARE(bases(session), (QStringList{"A:-", "B:A", "Loose:A", "C:A"}));
    session.undo();
    // Between the base itself and its first clipped layer.
    QVERIFY(session.placeLayer(loose, std::nullopt, a));
    QCOMPARE(bases(session), (QStringList{"A:-", "Loose:A", "B:A", "C:A"}));
    session.undo();
    // On top of the stack there is nothing to join.
    QVERIFY(session.placeLayer(loose, std::nullopt, c));
    QCOMPARE(bases(session), (QStringList{"A:-", "B:A", "C:A", "Loose:-"}));
    // A folder never joins.
    session.selectLayer(std::nullopt);
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    QVERIFY(session.placeLayer(folder, std::nullopt, b));
    QCOMPARE(bases(session), (QStringList{"A:-", "B:A", "Folder 1:-", "C:-", "Loose:-"}));
}

void LiveMaskEditTests::adoptionNeedsTheStackOnBothSides()
{
    // The rule by itself: placeLayer's release would hide it.
    const auto layer = [](const QString &name) { return ImageLayer(name, QSizeF(2, 2)); };
    std::vector<ImageLayer> layers{layer("S"), layer("X"), layer("Loose"), layer("C")};
    const QUuid base = layers[0].id, loose = layers[2].id;
    layers[3].maskSourceID = base;
    // Below sits a stranger to the stack: no joining.
    EditorSession::adoptClipping(loose, layers);
    QCOMPARE(layers[2].maskSourceID, std::nullopt);
    // Below sits a member of the stack: it joins.
    layers[1].maskSourceID = base;
    EditorSession::adoptClipping(loose, layers);
    QCOMPARE(layers[2].maskSourceID, std::optional(base));
    // The base itself never clips to itself.
    std::vector<ImageLayer> moved{layer("B"), layer("A"), layer("C")};
    const QUuid own = moved[1].id;
    moved[0].maskSourceID = own;
    moved[2].maskSourceID = own;
    EditorSession::adoptClipping(own, moved);
    QCOMPARE(moved[1].maskSourceID, std::nullopt);
    // An id the list lacks changes nothing.
    EditorSession::adoptClipping(QUuid::createUuid(), moved);
    QCOMPARE(moved[1].maskSourceID, std::nullopt);
}

void LiveMaskEditTests::aStackBrokenByAMoveLetsGo()
{
    EditorSession session;
    session.createDocument(8, 8);
    const QUuid a = image(session, "A"), b = image(session, "B"), c = image(session, "C");
    session.toggleClippingMask(b);
    session.toggleClippingMask(c);
    // The base moved to the top: nothing sits above it.
    QVERIFY(session.placeLayer(a, std::nullopt));
    QCOMPARE(bases(session), (QStringList{"B:-", "C:-", "A:-"}));
    session.undo();
    QCOMPARE(bases(session), (QStringList{"A:-", "B:A", "C:A"}));
    // Moved into a folder, a clipped layer leaves its base.
    session.selectLayer(std::nullopt);
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    QVERIFY(session.placeLayer(c, folder));
    QCOMPARE(layerWith(session, c).maskSourceID, std::nullopt);
    QCOMPARE(layerWith(session, b).maskSourceID, std::optional(a));
    // A freed layer starts its own run: D keeps C.
    EditorSession chained;
    chained.createDocument(8, 8);
    image(chained, "A");
    const QUuid second = image(chained, "B"), third = image(chained, "C"), fourth = image(chained, "D");
    chained.toggleClippingMask(second);
    chained.toggleClippingMask(third);
    QVERIFY(chained.linkMask(third, fourth));
    chained.selectLayer(std::nullopt);
    chained.addGroup();
    QVERIFY(chained.placeLayer(chained.activeLayerID().value(), std::nullopt, second));
    QCOMPARE(bases(chained), (QStringList{"A:-", "B:A", "Folder 1:-", "C:-", "D:C"}));
    // Runs count among siblings: a folder's child between breaks none.
    EditorSession mixed;
    mixed.createDocument(8, 8);
    mixed.addGroup();
    const QUuid holder = mixed.activeLayerID().value();
    mixed.selectLayer(std::nullopt);
    const QUuid lower = image(mixed, "A");
    mixed.selectLayer(holder);
    image(mixed, "X");
    mixed.selectLayer(lower);
    const QUuid upper = image(mixed, "B");
    mixed.toggleClippingMask(upper);
    QCOMPARE(bases(mixed), (QStringList{"Folder 1:-", "A:-", "X:-", "B:A"}));
    mixed.selectLayer(std::nullopt);
    const QUuid extra = image(mixed, "C");
    QVERIFY(mixed.placeLayer(extra, std::nullopt, std::nullopt, true));
    QCOMPARE(bases(mixed), (QStringList{"C:-", "Folder 1:-", "A:-", "X:-", "B:A"}));
}

void LiveMaskEditTests::deletingABaseFreesItsDependentsUnbakedOrBaked()
{
    EditorSession session;
    session.createDocument(8, 8);
    const QUuid a = image(session, "A"), b = image(session, "B"), c = image(session, "C");
    session.toggleClippingMask(b);
    session.toggleClippingMask(c);
    const ImageIdentity pixelsB = layerWith(session, b).asset.value().identity(), pixelsC = layerWith(session, c).asset.value().identity();
    const ImportedImage baked(QImage(2, 2, QImage::Format_RGBA8888_Premultiplied), QImage(), "Baked");
    // Only C was baked: B keeps its own pixels.
    session.finishDeletingLayer(a, {{c, baked}});
    QCOMPARE(bases(session), (QStringList{"B:-", "C:-"}));
    QVERIFY(layerWith(session, b).asset.value().identity() == pixelsB);
    QVERIFY(layerWith(session, c).asset.value().identity() == baked.identity());
    QCOMPARE(session.history.undoName(), QString("Delete Layer"));
    session.undo();
    QCOMPARE(bases(session), (QStringList{"A:-", "B:A", "C:A"}));
    QVERIFY(layerWith(session, c).asset.value().identity() == pixelsC);
    // A baked image for an unclipped layer is ignored.
    session.finishDeletingLayers({b, QUuid::createUuid()}, {{c, baked}});
    QCOMPARE(bases(session), (QStringList{"A:-", "C:A"}));
    QVERIFY(layerWith(session, c).asset.value().identity() == pixelsC);
    QCOMPARE(session.history.undoName(), QString("Delete Layers"));
}

void LiveMaskEditTests::aFolderWithTheNullIdKeepsItsOwnRuns()
{
    // All zeros is a valid id, not the root.
    const auto layer = [](const QString &name) { return ImageLayer(name, QSizeF(2, 2)); };
    std::vector<ImageLayer> layers{layer("Folder"), layer("A"), layer("X"), layer("B")};
    layers[0].id = QUuid();
    layers[0].isGroup = true;
    layers[2].parentID = QUuid();
    layers[3].maskSourceID = layers[1].id;
    EditorSession::releaseDetachedClipping(layers);
    QCOMPARE(layers[3].maskSourceID, std::optional(layers[1].id));
    // A folder's child clipped across parents lets go.
    layers[2].maskSourceID = layers[1].id;
    EditorSession::releaseDetachedClipping(layers);
    QCOMPARE(layers[2].maskSourceID, std::nullopt);
    QCOMPARE(layers[3].maskSourceID, std::optional(layers[1].id));
}

void LiveMaskEditTests::clippingWorksWithSeveralLayersSelected()
{
    EditorSession session;
    session.createDocument(8, 8);
    const QUuid a = image(session, "A"), b = image(session, "B"), c = image(session, "C");
    // Two selected: no one mask to edit, yet clipping works.
    session.selectLayers({a, b}, a);
    QVERIFY(session.canEditLayers() && !session.canEditMask());
    const int steps = session.history.undoCount();
    QVERIFY(session.canLinkMask(a, c) && session.linkMask(a, c));
    QCOMPARE(bases(session), (QStringList{"A:-", "B:-", "C:A"}));
    QVERIFY(session.canToggleClippingMask(b));
    session.toggleClippingMask(b);
    QCOMPARE(bases(session), (QStringList{"A:-", "B:A", "C:A"}));
    // The toggle frees B and what shares its base above.
    QVERIFY(session.canToggleClippingMask(b));
    session.toggleClippingMask(b);
    QCOMPARE(bases(session), (QStringList{"A:-", "B:-", "C:-"}));
    QVERIFY(session.linkMask(a, b));
    session.removeLiveMask(b);
    QCOMPARE(bases(session), (QStringList{"A:-", "B:-", "C:-"}));
    QCOMPARE(session.history.undoCount(), steps + 5);
    QCOMPARE(session.history.undoName(), QString("Release Clipping Mask"));
    QVERIFY(!session.canEditMask());
    QCOMPARE(session.selectedLayerIDs(), (QSet<QUuid>{a, b}));
}

QTEST_GUILESS_MAIN(LiveMaskEditTests)
#include "LiveMaskEditTests.moc"
