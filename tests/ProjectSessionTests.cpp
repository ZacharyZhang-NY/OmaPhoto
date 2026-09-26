#include "Document/EditorSession.h"
#include <QSignalSpy>
#include <QtTest>

// `EditorSession+Projects`: snapshots out, projects in.
namespace {
ImportedImage pixels(int width, int height, const QString &name)
{
    QImage image(width, height, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::red);
    return ImportedImage(image, image, name);
}

ImportedImage gray(int width, int height, int level)
{
    QImage image(width, height, QImage::Format_Grayscale8);
    image.fill(level);
    return LayerMask::assetFrom(image);
}

// Two layers in a folder, no field at its default.
ProjectSnapshot fixture()
{
    const QUuid folder = QUuid::createUuid(), lower = QUuid::createUuid(), upper = QUuid::createUuid();
    const QString lowerFile = uuidString(lower), upperFile = uuidString(upper);
    ProjectSnapshot snapshot{.manifest = {.resolution = 300, .documentID = QUuid::createUuid(), .width = 320, .height = 200,
                                          .activeLayerID = upper, .layers = {}},
                             .images = {}};
    snapshot.manifest.layers = {
        {.id = folder, .name = "Artwork", .isVisible = false, .transform = {.origin = {0, 0}, .size = {320, 200}}, .imageFile = std::nullopt,
         .isGroup = true, .opacity = 1, .blendMode = LayerBlendMode::normal},
        {.id = lower, .name = "Lower", .isVisible = true, .transform = {.origin = {10, 20}, .size = {40, 30}, .rotation = 15, .flipX = true,
                                                                       .sampling = LayerSampling::nearest},
         .imageFile = lowerFile + ".png", .parentID = folder, .isGroup = false, .opacity = 0.25, .blendMode = LayerBlendMode::multiply,
         .maskFile = lowerFile + ".mask.png", .maskEnabled = false,
         .maskPlacement = LayerTransform{.origin = {1, 2}, .size = {8, 6}}, .maskLinked = false},
        {.id = upper, .name = "Upper", .isVisible = false, .transform = {.origin = {-5, 7}, .size = {16, 8}, .flipY = true,
                                                                        .sampling = LayerSampling::smooth},
         .imageFile = upperFile + ".png", .parentID = folder, .isGroup = false, .opacity = 0.75, .blendMode = LayerBlendMode::screen,
         .maskFile = upperFile + ".mask.png", .maskEnabled = true, .maskSourceID = lower, .maskLinked = true},
    };
    // Files keep the names they came with; layers get renamed.
    snapshot.images.insert({lower, pixels(40, 30, "IMG_0001")});
    snapshot.images.insert({upper, pixels(16, 8, "IMG_0002")});
    snapshot.masks.insert({lower, gray(8, 6, 64)});
    snapshot.masks.insert({upper, gray(16, 8, 200)});
    return snapshot;
}
}

class ProjectSessionTests : public QObject {
    Q_OBJECT
private slots:
    void installingRebuildsEveryField();
    void aSnapshotGivesBackWhatWasInstalled();
    void installingStartsAFreshSession();
    void clearingLeavesNothingBehind();
    void aNewProjectIsOneEmptyLayerAndNoHistory();
    void applyingASizeIsOneNamedStepOnTheSameCanvas();
    void installingAndClearingFreeFileRequests();
};

void ProjectSessionTests::installingRebuildsEveryField()
{
    const ProjectSnapshot snapshot = fixture();
    EditorSession session;
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression("^installed a project of 3 layers from \\S*art\\.comp$"));
    session.installProject(snapshot, "/projects/art.comp");
    const CanvasDocument document = session.document().value();
    QCOMPARE(document.id, snapshot.manifest.documentID);
    QCOMPARE(document.size(), QSizeF(320, 200));
    QCOMPARE(document.resolution, 300.0);
    QCOMPARE(session.projectPath(), std::optional(QString("/projects/art.comp")));
    QCOMPARE(session.activeLayerID(), snapshot.manifest.activeLayerID);
    QCOMPARE(int(document.layers.size()), 3);
    for (size_t index = 0; index < 3; ++index) {
        const ProjectLayerRecord &record = snapshot.manifest.layers[index];
        const ImageLayer &layer = document.layers[index];
        QCOMPARE(layer.id, record.id);
        QCOMPARE(layer.name, record.name);
        QCOMPARE(layer.isVisible, record.isVisible);
        QCOMPARE(layer.transform, record.transform);
        QCOMPARE(layer.parentID, record.parentID);
        QCOMPARE(layer.isGroup, record.isGroup.value());
        QCOMPARE(layer.opacity, record.opacity.value());
        QCOMPARE(layer.blendMode, record.blendMode.value());
        QCOMPARE(layer.maskSourceID, record.maskSourceID);
        QCOMPARE(layer.asset.has_value(), record.imageFile.has_value());
        QCOMPARE(layer.mask.has_value(), record.maskFile.has_value());
    }
    // Pixels and masks are shared, not copied.
    const ImageLayer &lower = document.layers[1], &upper = document.layers[2];
    QVERIFY(lower.asset.value().identity() == snapshot.images.at(lower.id).identity());
    QVERIFY(upper.asset.value().identity() == snapshot.images.at(upper.id).identity());
    QCOMPARE(lower.mask.value(), LayerMask(snapshot.masks.at(lower.id), false, LayerTransform{.origin = {1, 2}, .size = {8, 6}}, false));
    QCOMPARE(upper.mask.value(), LayerMask(snapshot.masks.at(upper.id), true, std::nullopt, true));
    // What a record leaves out takes Swift's defaults.
    ProjectSnapshot bare = snapshot;
    bare.manifest.resolution = std::nullopt;
    for (ProjectLayerRecord &record : bare.manifest.layers) {
        record.isGroup = std::nullopt;
        record.opacity = std::nullopt;
        record.blendMode = std::nullopt;
    }
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression("^installed a project of 3 layers .*"));
    session.installProject(bare, "bare.comp");
    QCOMPARE(session.projectPath(), std::optional(QString("bare.comp")));
    QCOMPARE(session.document().value().resolution, 72.0);
    for (const ImageLayer &layer : session.document().value().layers) {
        QVERIFY(!layer.isGroup);
        QCOMPARE(layer.opacity, 1.0);
        QCOMPARE(layer.blendMode, LayerBlendMode::normal);
    }
}

void ProjectSessionTests::aSnapshotGivesBackWhatWasInstalled()
{
    QVERIFY(!EditorSession().projectSnapshot().has_value());
    const ProjectSnapshot snapshot = fixture();
    EditorSession session;
    session.installProject(snapshot, "art.comp");
    const ProjectSnapshot again = session.projectSnapshot().value();
    // The same manifest to the byte; pixels by identity.
    QCOMPARE(again.manifest.encoded(), snapshot.manifest.encoded());
    QCOMPARE(again.manifest.version, qint64(8));
    QCOMPARE(int(again.images.size()), 2);
    QCOMPARE(int(again.masks.size()), 2);
    for (const auto &[id, image] : snapshot.images)
        QVERIFY(again.images.at(id).identity() == image.identity());
    for (const auto &[id, mask] : snapshot.masks)
        QVERIFY(again.masks.at(id).identity() == mask.identity());
    // The active layer of the moment goes into the manifest.
    session.selectLayer(snapshot.manifest.layers[1].id);
    QCOMPARE(session.projectSnapshot().value().manifest.activeLayerID, std::optional(snapshot.manifest.layers[1].id));
    session.selectLayer(std::nullopt);
    QCOMPARE(session.projectSnapshot().value().manifest.activeLayerID, std::nullopt);
    // A canvas without layers still has a snapshot.
    EditorSession bare;
    bare.createDocument(7, 5);
    const ProjectSnapshot blank = bare.projectSnapshot().value();
    QCOMPARE(blank.manifest.documentID, bare.document().value().id);
    QCOMPARE(blank.manifest.width, qint64(7));
    QCOMPARE(blank.manifest.height, qint64(5));
    QCOMPARE(blank.manifest.resolution, std::optional(72.0));
    QVERIFY(blank.manifest.layers.empty() && blank.images.empty() && blank.masks.empty());
    QCOMPARE(blank.manifest.activeLayerID, std::nullopt);
}

void ProjectSessionTests::installingStartsAFreshSession()
{
    EditorSession session;
    session.viewport.resize(QSizeF(400, 300), 1, std::nullopt);
    session.createDocument(64, 64);
    session.addGroup();
    const QUuid old = session.activeLayerID().value();
    session.insert(pixels(8, 8, "Old"));
    session.selectLayer(old);
    session.toggleGroupExpansion(old);
    session.selectLayer(session.document().value().layers.back().id);
    session.beginTransform();
    session.setRenamingLayerID(old);
    session.zoom(3);
    QVERIFY(session.transformEdit().has_value() && !session.collapsedGroupIDs().isEmpty() && session.history.undoCount() > 0);
    const ProjectSnapshot snapshot = fixture();
    session.installProject(snapshot, "art.comp");
    QVERIFY(!session.transformEdit().has_value());
    QVERIFY(session.collapsedGroupIDs().isEmpty());
    QVERIFY(!session.renamingLayerID().has_value());
    QCOMPARE(session.history.undoCount(), 0);
    QVERIFY(!session.canUndo() && !session.canRedo() && !session.isModified());
    QCOMPARE(session.selectedLayerIDs(), QSet<QUuid>{snapshot.manifest.activeLayerID.value()});
    // The view fits the canvas that came in.
    QVERIFY(session.viewport.followsFit());
    EditorSession fitted;
    fitted.viewport.resize(QSizeF(400, 300), 1, std::nullopt);
    fitted.createDocument(320, 200);
    QCOMPARE(session.viewport.zoom(), fitted.viewport.zoom());
    // The first edit after installing is the first step.
    session.addBlankLayer();
    QCOMPARE(session.history.undoCount(), 1);
    QVERIFY(session.isModified());
    // A project saved with nothing selected opens that way.
    ProjectSnapshot unselected = fixture();
    unselected.manifest.activeLayerID = std::nullopt;
    QVERIFY(session.activeLayerID().has_value());
    session.installProject(unselected, "unselected.comp");
    QVERIFY(!session.activeLayerID().has_value() && session.selectedLayerIDs().isEmpty());
    QCOMPARE(int(session.document().value().layers.size()), 3);
    // An empty canvas is a project too.
    const ProjectSnapshot empty{.manifest = {.documentID = QUuid::createUuid(), .width = 7, .height = 5, .activeLayerID = std::nullopt, .layers = {}},
                                .images = {}};
    session.installProject(empty, "empty.comp");
    QCOMPARE(session.document().value().id, empty.manifest.documentID);
    QCOMPARE(session.document().value().size(), QSizeF(7, 5));
    QVERIFY(session.document().value().layers.empty());
}

void ProjectSessionTests::clearingLeavesNothingBehind()
{
    EditorSession session;
    session.installProject(fixture(), "art.comp");
    session.toggleGroupExpansion(session.document().value().layers.front().id);
    session.selectLayer(session.document().value().layers[1].id);
    session.toggleLayerVisibility(session.document().value().layers.front().id);
    session.beginTransform();
    session.setRenamingLayerID(QUuid::createUuid());
    session.clearProject();
    QVERIFY(!session.document().has_value() && !session.activeLayerID().has_value() && session.selectedLayerIDs().isEmpty());
    QVERIFY(!session.projectPath().has_value() && !session.renamingLayerID().has_value());
    QVERIFY(!session.transformEdit().has_value() && session.collapsedGroupIDs().isEmpty());
    QCOMPARE(session.history.undoCount(), 0);
    QVERIFY(!session.isModified() && !session.projectSnapshot().has_value());
}

void ProjectSessionTests::aNewProjectIsOneEmptyLayerAndNoHistory()
{
    EditorSession session;
    session.installProject(fixture(), "art.comp");
    session.createNewProject(640, 480);
    QCOMPARE(session.document().value().size(), QSizeF(640, 480));
    QCOMPARE(int(session.document().value().layers.size()), 1);
    QVERIFY(!session.projectPath().has_value());
    // The canvas itself is the one step, as in Swift.
    QCOMPARE(session.history.undoCount(), 1);
    const std::optional<CanvasDocument> kept = session.document();
    // Refused sizes and busy sessions keep what they have.
    for (const auto &[width, height] : {std::pair(0, 10), std::pair(10, 0), std::pair(30'001, 10), std::pair(10, 30'001)}) {
        session.createNewProject(width, height);
        QCOMPARE(session.document(), kept);
    }
    session.setIsImporting(true);
    session.createNewProject(10, 10);
    session.setIsImporting(false);
    session.setIsProjectBusy(true);
    session.createNewProject(10, 10);
    QCOMPARE(session.document(), kept);
    session.setIsProjectBusy(false);
    // Both ends of both sides are taken.
    session.createNewProject(30'000, 1);
    QCOMPARE(session.document().value().size(), QSizeF(30'000, 1));
    session.createNewProject(1, 30'000);
    QCOMPARE(session.document().value().size(), QSizeF(1, 30'000));
}

void ProjectSessionTests::applyingASizeIsOneNamedStepOnTheSameCanvas()
{
    EditorSession session;
    session.viewport.resize(QSizeF(400, 300), 1, std::nullopt);
    const ProjectSnapshot snapshot = fixture();
    session.applyImageSize(snapshot);
    QVERIFY(!session.document().has_value());
    session.installProject(snapshot, "art.comp");
    const std::optional<CanvasDocument> before = session.document();
    // Another canvas's pixels are refused in silence.
    ProjectSnapshot other = fixture();
    QSignalSpy changes(&session, &EditorSession::changed);
    session.applyDocumentSize(other, "Canvas Size");
    QCOMPARE(session.document(), before);
    QCOMPARE(int(changes.count()), 0);
    QCOMPARE(session.history.undoCount(), 0);
    // The same canvas, half as large, with one layer gone.
    ProjectSnapshot smaller = snapshot;
    smaller.manifest.width = 160;
    smaller.manifest.height = 100;
    smaller.manifest.resolution = 150;
    smaller.manifest.layers.pop_back();
    // What the snapshot calls active is not the session's affair.
    smaller.manifest.activeLayerID = smaller.manifest.layers.front().id;
    session.zoom(3);
    session.applyDocumentSize(smaller, "Canvas Size");
    QCOMPARE(session.activeLayerID(), snapshot.manifest.activeLayerID);
    QCOMPARE(session.projectPath(), std::optional(QString("art.comp")));
    QCOMPARE(session.document().value().size(), QSizeF(160, 100));
    QCOMPARE(session.document().value().resolution, 150.0);
    QCOMPARE(int(session.document().value().layers.size()), 2);
    QCOMPARE(session.history.undoCount(), 1);
    QCOMPARE(session.history.undoName(), QString("Canvas Size"));
    QVERIFY(session.viewport.followsFit());
    session.undo();
    QCOMPARE(session.document(), before);
    session.redo();
    QCOMPARE(session.document().value().size(), QSizeF(160, 100));
    session.undo();
    session.applyImageSize(smaller);
    QCOMPARE(session.history.undoName(), QString("Image Size"));
}

void ProjectSessionTests::installingAndClearingFreeFileRequests()
{
    for (const bool clears : {false, true}) {
        EditorSession session;
        session.setRenamingLayerID(QUuid::createUuid());
        bool ran = false;
        session.waitForFileRequest([&] { ran = true; });
        if (clears)
            session.clearProject();
        else
            session.installProject(fixture(), "art.comp");
        QVERIFY(QTest::qWaitFor([&] { return ran; }, 5'000));
    }
}

QTEST_GUILESS_MAIN(ProjectSessionTests)
#include "ProjectSessionTests.moc"
