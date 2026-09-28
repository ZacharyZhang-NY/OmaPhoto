#include "Document/ProjectWorkspace.h"
#include "Rendering/RasterSnapshot.h"
#include "SessionFixtures.h"
#include <QFutureWatcher>
#include <QMimeData>
#include <QSemaphore>
#include <QThreadPool>
#include <QtTest>

// Layers copied from one open project into another.
namespace {
ImportedImage solid(QColor colour, int alpha, const QString &name)
{
    QImage image(2, 2, QImage::Format_RGBA8888_Premultiplied);
    colour.setAlpha(alpha);
    image.fill(colour);
    return ImportedImage(image, QImage(), name);
}

// A layer that claims this many pixels and holds one.
ImportedImage vast(int width, int height)
{
    QImage dot(1, 1, QImage::Format_RGBA8888_Premultiplied);
    dot.fill(Qt::red);
    return ImportedImage(std::make_shared<const RasterSnapshot>(width, height, dot, QRectF(0, 0, 1, 1), std::vector<BrushPatch>{}), QImage(), "Vast");
}

bool copied(ProjectWorkspace &workspace, QUuid id, std::optional<QUuid> destination, std::optional<QPointF> point = std::nullopt)
{
    bool done = false;
    workspace.copyLayer(id, destination, point, [&] { done = true; });
    return QTest::qWaitFor([&] { return done; }, 10'000);
}

QStringList names(const EditorSession &session)
{
    QStringList result;
    for (const ImageLayer &layer : session.document().value().layers)
        result << layer.name;
    return result;
}
}

class ProjectCopyTests : public QObject {
    Q_OBJECT
private slots:
    void whatMayNotBeCopiedChangesNothing();
    void theHundredMegapixelsCountAcrossBothProjects();
    void aFolderTravelsWithItsContentsAndItsLinks();
    void aMaskFromOutsideTheCopyIsBakedIn();
    void anAdjustmentLetsGoOfItsBaseUnbaked();
    void effectsTravelWithTheCopy();
    void theCopyLandsAtThePointOrTheCentre();
    void aNewTabGetsTheSourcesCanvasInOneStep();
    void bothProjectsAreBusyWhileTheCopyBakes();
    void aBakeThatFailsCopiesNothingAndSaysWhy();
    void aDroppedRowMayCarryTheNullId();
    void ofTwoTabsWithTheLayerTheFirstIsTheSource();
};

void ProjectCopyTests::whatMayNotBeCopiedChangesNothing()
{
    ProjectWorkspace workspace;
    ProjectTab &from = workspace.current();
    from.session.createDocument(8, 8);
    from.session.insert(solid(Qt::red, 255, "Red"));
    const QUuid id = from.session.activeLayerID().value();
    ProjectTab &target = workspace.addTab();
    target.session.createDocument(8, 8);
    target.session.insert(solid(Qt::blue, 255, "Blue"));
    QSignalSpy changes(&workspace, &ProjectWorkspace::changed);
    QSignalSpy edits(&target.session, &EditorSession::changed);
    // A stranger, the layer's own tab, a missing tab.
    QVERIFY(copied(workspace, QUuid::createUuid(), target.id));
    QVERIFY(copied(workspace, id, from.id));
    QVERIFY(copied(workspace, id, QUuid::createUuid()));
    // A source or a target amid a transform.
    target.session.beginTransform();
    QVERIFY(copied(workspace, id, target.id));
    target.session.cancelTransform();
    from.session.selectLayer(id);
    from.session.beginTransform();
    QVERIFY(copied(workspace, id, target.id));
    from.session.cancelTransform();
    QCOMPARE(names(target.session), (QStringList{"Blue"}));
    QCOMPARE(int(workspace.tabs().size()), 2);
    QCOMPARE(changes.count(), 0);
    // A target under a sheet or error; a busy front.
    const qsizetype before = edits.count();
    target.session.setShowsImporter(true);
    QVERIFY(copied(workspace, id, target.id));
    target.session.setShowsImporter(false);
    workspace.select(from.id);
    target.session.setImportError(QString("earlier"));
    QVERIFY(target.session.canEditLayers() && copied(workspace, id, target.id));
    target.session.setImportError(std::nullopt);
    workspace.select(target.id);
    workspace.current().session.setIsProjectBusy(true);
    QVERIFY(copied(workspace, id, std::nullopt));
    workspace.current().session.setIsProjectBusy(false);
    QCOMPARE(names(target.session), (QStringList{"Blue"}));
    QCOMPARE(int(workspace.tabs().size()), 2);
    QCOMPARE(changes.count(), 2);
    QCOMPARE(edits.count(), before + 6);
}

void ProjectCopyTests::theHundredMegapixelsCountAcrossBothProjects()
{
    ProjectWorkspace workspace;
    ProjectTab &from = workspace.current();
    from.session.createDocument(8, 8);
    from.session.insert(vast(10'000, 10'000));
    const QUuid id = from.session.activeLayerID().value();
    ProjectTab &target = workspace.addTab();
    target.session.createDocument(8, 8);
    // Exactly the limit fits.
    QVERIFY(copied(workspace, id, target.id));
    QCOMPARE(names(target.session), (QStringList{"Vast"}));
    QCOMPARE(target.session.importError(), std::nullopt);
    // One pixel more, counted across both, does not.
    target.session.undo();
    target.session.insert(solid(Qt::blue, 255, "Dot"));
    target.session.setRenamingLayerID(std::nullopt);
    QVERIFY(copied(workspace, id, target.id));
    QCOMPARE(names(target.session), (QStringList{"Dot"}));
    QCOMPARE(target.session.importError(), std::optional(QString("The copied layers exceed this project’s 100-megapixel limit.")));
    QVERIFY(!workspace.isManaging() && !from.session.isProjectBusy() && !target.session.isProjectBusy());
    // While that error shows, the front tab lets nothing start.
    QVERIFY(!workspace.canSwitch() && copied(workspace, id, std::nullopt));
    QCOMPARE(int(workspace.tabs().size()), 2);
    target.session.setImportError(std::nullopt);
    // Refused for a new tab, that tab stays.
    from.session.insert(vast(1, 1));
    const QUuid extra = from.session.activeLayerID().value();
    from.session.addGroup();
    const QUuid folder = from.session.activeLayerID().value();
    QVERIFY(from.session.placeLayer(id, folder));
    QVERIFY(from.session.placeLayer(extra, folder));
    QVERIFY(copied(workspace, folder, std::nullopt));
    QCOMPARE(int(workspace.tabs().size()), 3);
    QVERIFY(!workspace.current().session.document().has_value());
    QVERIFY(workspace.current().session.importError().has_value());
}

void ProjectCopyTests::aFolderTravelsWithItsContentsAndItsLinks()
{
    ProjectWorkspace workspace;
    ProjectTab &from = workspace.current();
    from.session.createDocument(8, 8);
    from.session.insert(solid(Qt::green, 255, "Stays"));
    from.session.addGroup();
    const QUuid folder = from.session.activeLayerID().value();
    from.session.insert(solid(Qt::red, 255, "Base"));
    const QUuid base = from.session.activeLayerID().value();
    from.session.insert(solid(Qt::blue, 255, "Clipped"));
    const QUuid clipped = from.session.activeLayerID().value();
    from.session.toggleClippingMask(clipped);
    const std::optional<CanvasDocument> before = from.session.document();
    ProjectTab &target = workspace.addTab();
    target.session.createDocument(8, 8);
    target.session.insert(solid(Qt::white, 255, "Own"));
    // Copied from the tab in front into one behind it.
    workspace.select(from.id);
    bool switchable = false;
    connect(&workspace, &ProjectWorkspace::changed, this, [&] { switchable = workspace.canSwitch(); });
    QVERIFY(copied(workspace, folder, target.id));
    QVERIFY(switchable && workspace.canSwitch());
    // The source is untouched; the copy is whole and renumbered.
    QCOMPARE(from.session.document(), before);
    QCOMPARE(names(target.session), (QStringList{"Own", "Folder 1", "Base", "Clipped"}));
    const std::vector<ImageLayer> layers = target.session.document().value().layers;
    QVERIFY(layers[1].id != folder && layers[2].id != base && layers[3].id != clipped);
    QVERIFY(layers[1].isGroup && layers[1].parentID == std::nullopt);
    QCOMPARE(layers[2].parentID, std::optional(layers[1].id));
    QCOMPARE(layers[3].parentID, std::optional(layers[1].id));
    QCOMPARE(layers[3].maskSourceID, std::optional(layers[2].id));
    // The same pixels: nothing inside the copy needed baking.
    QVERIFY(layers[3].asset.value().identity() == before.value().layers[3].asset.value().identity());
    QCOMPARE(target.session.activeLayerID(), std::optional(layers[1].id));
    QCOMPARE(workspace.selectedID(), target.id);
    QCOMPARE(target.session.history.undoName(), QString("Copy Layers from Project"));
    // A child copied alone leaves its folder behind.
    QVERIFY(copied(workspace, base, target.id));
    QCOMPARE(target.session.document().value().layers.back().parentID, std::nullopt);
    // Each copy's watcher goes once it has answered.
    QTRY_VERIFY(workspace.findChildren<QFutureWatcherBase *>().isEmpty());
}

void ProjectCopyTests::aMaskFromOutsideTheCopyIsBakedIn()
{
    ProjectWorkspace workspace;
    ProjectTab &from = workspace.current();
    from.session.createDocument(2, 2);
    from.session.insert(solid(Qt::red, 128, "Base"));
    from.session.insert(solid(Qt::blue, 255, "Top"));
    const QUuid top = from.session.activeLayerID().value();
    from.session.toggleClippingMask(top);
    const ImageIdentity pixels = from.session.activeLayer().value().asset.value().identity();
    ProjectTab &target = workspace.addTab();
    target.session.createDocument(2, 2);
    QVERIFY(copied(workspace, top, target.id));
    const ImageLayer copy = target.session.document().value().layers.front();
    QCOMPARE(copy.maskSourceID, std::nullopt);
    QVERIFY(copy.asset.value().identity() != pixels);
    const QColor pixel = copy.asset.value().image().pixelColor(0, 0);
    QVERIFY2(std::abs(pixel.alpha() - 128) <= 1, qPrintable(QString::number(pixel.alpha())));
    QCOMPARE(pixel.blue(), 255);
    // The source keeps its link and its own pixels.
    const ImageLayer kept = layerWith(from.session, top);
    QVERIFY(kept.maskSourceID.has_value() && kept.asset.value().identity() == pixels);
}

void ProjectCopyTests::anAdjustmentLetsGoOfItsBaseUnbaked()
{
    ProjectWorkspace workspace;
    ProjectTab &from = workspace.current();
    from.session.createDocument(2, 2);
    from.session.insert(solid(Qt::red, 255, "Base"));
    from.session.addAdjustment(AdjustmentKind::curves);
    from.session.setAdjustmentEditingID(std::nullopt);
    const QUuid adjustment = from.session.activeLayerID().value();
    from.session.toggleClippingMask(adjustment);
    QVERIFY(from.session.activeLayer().value().maskSourceID.has_value());
    ProjectTab &target = workspace.addTab();
    target.session.createDocument(2, 2);
    QVERIFY(copied(workspace, adjustment, target.id));
    // Settings travel; no pixels to bake the base into.
    const ImageLayer copy = target.session.document().value().layers.front();
    QVERIFY(!copy.maskSourceID && !copy.asset && copy.adjustment == LayerAdjustment{AdjustmentKind::curves});
}

void ProjectCopyTests::effectsTravelWithTheCopy()
{
    ProjectWorkspace workspace;
    ProjectTab &from = workspace.current();
    from.session.createDocument(2, 2);
    from.session.insert(solid(Qt::red, 255, "Framed"));
    LayerEffects effects;
    effects.stroke = StrokeEffect();
    from.session.setEffects(effects);
    const QUuid framed = from.session.activeLayerID().value();
    ProjectTab &target = workspace.addTab();
    target.session.createDocument(2, 2);
    QVERIFY(copied(workspace, framed, target.id));
    const ImageLayer copy = target.session.document().value().layers.front();
    QVERIFY(copy.name == QString("Framed") && copy.effects == effects);
    QVERIFY(layerWith(from.session, framed).effects == effects);
}

void ProjectCopyTests::theCopyLandsAtThePointOrTheCentre()
{
    ProjectWorkspace workspace;
    ProjectTab &from = workspace.current();
    from.session.createDocument(20, 10);
    from.session.insert(solid(Qt::red, 255, "Red"), QPointF(5, 5));
    const QUuid id = from.session.activeLayerID().value();
    // A mask placed two to the right rides along.
    rewrite(from.session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, id, coverage());
        record(snapshot, id).maskPlacement = LayerTransform{.origin = {6, 4}, .size = {2, 2}};
    });
    ProjectTab &target = workspace.addTab();
    target.session.createDocument(40, 30);
    QVERIFY(copied(workspace, id, target.id));
    QVERIFY(copied(workspace, id, target.id, QPointF(7, 9)));
    const std::vector<ImageLayer> layers = target.session.document().value().layers;
    QCOMPARE(layers[0].transform.center(), QPointF(20, 15));
    QCOMPARE(layers[0].mask.value().placement.value().origin, QPointF(21, 14));
    QCOMPARE(layers[1].transform.center(), QPointF(7, 9));
    QCOMPARE(layers[1].mask.value().placement.value().origin, QPointF(8, 8));
    QCOMPARE(layers[1].mask.value().placement.value().size, QSizeF(2, 2));
    // The layer asked for is the anchor, first or not.
    from.session.addGroup();
    const QUuid folder = from.session.activeLayerID().value();
    QVERIFY(from.session.placeLayer(id, folder));
    rewrite(from.session, [&](ProjectSnapshot &snapshot) { std::reverse(snapshot.manifest.layers.begin(), snapshot.manifest.layers.end()); });
    QCOMPARE(from.session.document().value().layers.front().id, id);
    const QPointF offset = layerWith(from.session, id).transform.center() - layerWith(from.session, folder).transform.center();
    QVERIFY(copied(workspace, folder, target.id, QPointF(30, 20)));
    const std::vector<ImageLayer> placed = target.session.document().value().layers;
    QCOMPARE(placed[placed.size() - 1].transform.center(), QPointF(30, 20));
    QCOMPARE(placed[placed.size() - 2].transform.center(), QPointF(30, 20) + offset);
    QVERIFY(!offset.isNull());
}

void ProjectCopyTests::aNewTabGetsTheSourcesCanvasInOneStep()
{
    ProjectWorkspace workspace;
    ProjectTab &from = workspace.current();
    from.session.createDocument(20, 10);
    from.session.insert(solid(Qt::red, 255, "Red"), QPointF(5, 5));
    const QUuid id = from.session.activeLayerID().value();
    QVERIFY(copied(workspace, id, std::nullopt));
    QCOMPARE(int(workspace.tabs().size()), 2);
    EditorSession &made = workspace.current().session;
    QVERIFY(&made != &from.session);
    QCOMPARE(made.document().value().size(), QSizeF(20, 10));
    QCOMPARE(names(made), (QStringList{"Red"}));
    QCOMPARE(made.document().value().layers.front().transform.center(), QPointF(10, 5));
    QCOMPARE(made.history.undoCount(), 1);
    QCOMPARE(made.history.undoName(), QString("Copy Layers from Project"));
    made.undo();
    QVERIFY(!made.document().has_value());
}

void ProjectCopyTests::bothProjectsAreBusyWhileTheCopyBakes()
{
    ProjectWorkspace workspace;
    ProjectTab &from = workspace.current();
    from.session.createDocument(2, 2);
    // A painted base: its pixels flatten on first use.
    const auto raster = std::make_shared<const RasterSnapshot>(2, 2, solid(Qt::red, 128, "").image(), QRectF(0, 0, 2, 2), std::vector<BrushPatch>{});
    from.session.insert(ImportedImage(raster, QImage(), "Base"));
    from.session.insert(solid(Qt::blue, 255, "Top"));
    const QUuid top = from.session.activeLayerID().value();
    from.session.toggleClippingMask(top);
    ProjectTab &target = workspace.addTab();
    target.session.createDocument(2, 2);
    // Every worker is held: the bake has to queue.
    QThreadPool *pool = QThreadPool::globalInstance();
    const int workers = pool->maxThreadCount();
    QSemaphore held, gate;
    const auto letGo = qScopeGuard([&] {
        gate.release(workers);
        pool->waitForDone();
    });
    for (int index = 0; index < workers; ++index) {
        pool->start([&] {
            held.release();
            gate.acquire();
        });
    }
    QVERIFY(held.tryAcquire(workers, 10'000));
    bool done = false;
    workspace.copyLayer(top, target.id, std::nullopt, [&] { done = true; });
    // Back at once: both busy, nothing baked, nothing placed.
    QVERIFY(workspace.isManaging() && from.session.isProjectBusy() && target.session.isProjectBusy());
    QVERIFY(target.session.document().value().layers.empty() && !done && !raster->hasMaterializedPixels());
    gate.release(workers);
    // No event runs in this wait: a worker flattens.
    QElapsedTimer clock;
    clock.start();
    while (!raster->hasMaterializedPixels() && clock.elapsed() < 10'000)
        QTest::qSleep(5);
    QVERIFY(raster->hasMaterializedPixels() && target.session.isProjectBusy());
    QTRY_VERIFY(done);
    QVERIFY(!workspace.isManaging() && !from.session.isProjectBusy() && !target.session.isProjectBusy());
    QCOMPARE(names(target.session), (QStringList{"Top"}));
}

void ProjectCopyTests::aBakeThatFailsCopiesNothingAndSaysWhy()
{
    ProjectWorkspace workspace;
    ProjectTab &from = workspace.current();
    from.session.createDocument(2, 2);
    from.session.insert(solid(Qt::red, 128, "Base"));
    const QUuid base = from.session.activeLayerID().value();
    // An asset without pixels: no surface can hold it.
    from.session.insert(ImportedImage(QImage(), QImage(), "Void"));
    const QUuid hollow = from.session.activeLayerID().value();
    QVERIFY(from.session.linkMask(base, hollow));
    ProjectTab &target = workspace.addTab();
    target.session.createDocument(2, 2);
    // From the front tab: the last signal sees it free.
    workspace.select(from.id);
    bool switchable = false;
    connect(&workspace, &ProjectWorkspace::changed, this, [&] { switchable = workspace.canSwitch(); });
    QTest::ignoreMessage(QtWarningMsg, "cannot copy the layers: The canvas could not be rendered. Try a smaller canvas.");
    QVERIFY(copied(workspace, hollow, target.id));
    QVERIFY(switchable && workspace.canSwitch());
    QVERIFY(target.session.document().value().layers.empty());
    QCOMPARE(target.session.importError(), std::optional(QString("The canvas could not be rendered. Try a smaller canvas.")));
    QCOMPARE(target.session.history.undoCount(), 1);
    QVERIFY(!workspace.isManaging() && !from.session.isProjectBusy() && !target.session.isProjectBusy());
}

void ProjectCopyTests::aDroppedRowMayCarryTheNullId()
{
    ProjectWorkspace workspace;
    ProjectTab &from = workspace.current();
    from.session.createDocument(8, 8);
    from.session.insert(solid(Qt::red, 255, "Zero"));
    const QUuid old = from.session.activeLayerID().value();
    // All zeros is an id a project file may hold.
    rewrite(from.session, [&](ProjectSnapshot &snapshot) {
        record(snapshot, old).id = QUuid();
        snapshot.manifest.activeLayerID = QUuid();
        auto pixels = snapshot.images.extract(old);
        pixels.key() = QUuid();
        snapshot.images.insert(std::move(pixels));
    });
    QMimeData provider;
    provider.setData(ProjectWorkspace::layerType, uuidString(QUuid()).toUtf8());
    bool done = false;
    workspace.receiveProviders(provider, std::nullopt, std::nullopt, [&] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(int(workspace.tabs().size()), 2);
    QCOMPARE(names(workspace.current().session), (QStringList{"Zero"}));
    QVERIFY(!workspace.current().session.activeLayerID().value().isNull());
}

void ProjectCopyTests::ofTwoTabsWithTheLayerTheFirstIsTheSource()
{
    ProjectWorkspace workspace;
    ProjectTab &first = workspace.current();
    first.session.createDocument(8, 8);
    first.session.insert(solid(Qt::red, 255, "One"));
    const QUuid id = first.session.activeLayerID().value();
    // A project saved twice and opened twice shares its ids.
    ProjectTab &second = workspace.addTab();
    ProjectSnapshot twin = first.session.projectSnapshot().value();
    record(twin, id).name = "Two";
    second.session.installProject(twin, QString("twin.comp"));
    QVERIFY(copied(workspace, id, std::nullopt));
    QCOMPARE(int(workspace.tabs().size()), 3);
    QCOMPARE(names(workspace.current().session), (QStringList{"One"}));
}

QTEST_MAIN(ProjectCopyTests)
#include "ProjectCopyTests.moc"
