#include "ExternalChangeFixtures.h"
#include <QBuffer>
#include <QFutureWatcher>

// A project something else writes while it is open.
class ExternalChangeTests : public QObject {
    Q_OBJECT
private slots:
    void digestFollowsContentNotMetadata();
    void reloadKeepsViewportSelectionAndFoldersButNotHistory();
    void writingThePackageElsewhereReloadsTheOpenProject();
    void ourOwnSaveAndMetadataTouchesDoNotReload();
    void halfWrittenPackagesAreIgnoredUntilTheyLoad();
    void unsavedWorkIsNeverReplacedWithoutAsking();
    void unsavedWorkAsksToRevertOrKeep();
    void aBackgroundTabAsksWhenItComesToTheFront();
    void aBusyProjectWaitsThenReloads();
    void closingOrANewCanvasStopsTheWatch();
    void theGuidesSameSizeRecipeReloads();
    void aNewCanvasDropsAWaitingRecheck();
    void oneCheckRunsAtATime();
    void aCheckWithoutAProjectOrDuringOurSaveEnds();
};

void ExternalChangeTests::digestFollowsContentNotMetadata()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    const QString manifest = path + QStringLiteral("/manifest.json");
    const QByteArray before = ProjectDigest::compute(path);
    // Touched, and rewritten with the same bytes: sync clients.
    QFile touched(manifest);
    QVERIFY(touched.open(QIODevice::ReadWrite));
    QVERIFY(touched.setFileTime(QDateTime::currentDateTime().addSecs(60), QFileDevice::FileModificationTime));
    touched.close();
    rewrite(manifest, contents(manifest));
    QCOMPARE(ProjectDigest::compute(path), before);
    renameFirstLayerOnDisk(path, QStringLiteral("Renamed elsewhere"));
    const QByteArray renamed = ProjectDigest::compute(path);
    QVERIFY(renamed != before);
    // Another image's bytes under a layer's name.
    const QDir images(path + QStringLiteral("/images"));
    const QString png = images.entryList({QStringLiteral("*.png")}).value(0);
    rewrite(images.filePath(png), contents(images.filePath(png)) + QByteArray(64, '\0'));
    QVERIFY(ProjectDigest::compute(path) != renamed);
    // No manifest, no digest.
    QVERIFY(QFile::remove(manifest));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, ProjectDigest::compute(path));
}

void ExternalChangeTests::reloadKeepsViewportSelectionAndFoldersButNotHistory()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    session.installProject(ProjectStore::load(path), path);
    session.viewport.resize(QSizeF(800, 600), 1, session.document().value().size());
    session.zoom(3);
    const QUuid base = session.document().value().layers[0].id;
    const QUuid blank = session.document().value().layers[1].id;
    session.selectLayer(blank);
    session.extendSelection(base);
    QCOMPARE(session.activeLayerID().value(), base);
    session.renameLayer(base, QStringLiteral("Edited here"));
    QVERIFY(session.isModified() && session.canUndo());
    renameFirstLayerOnDisk(path, QStringLiteral("Renamed elsewhere"));
    session.reloadProject(ProjectStore::load(path));
    QCOMPARE(firstName(session), QString("Renamed elsewhere"));
    QCOMPARE(session.viewport.zoom(), 3.0);
    QCOMPARE(session.activeLayerID().value(), base);
    QCOMPARE(session.selectedLayerIDs(), (QSet<QUuid>{base, blank}));
    QVERIFY(!session.isModified());
    QVERIFY(!session.canUndo());
    QCOMPARE(session.projectPath().value(), path);
    // A collapsed folder stays collapsed while it exists.
    session.groupSelectedLayers();
    const QUuid folder = session.activeLayerID().value();
    session.toggleGroupExpansion(folder);
    QCOMPARE(session.collapsedGroupIDs(), QSet<QUuid>{folder});
    const ProjectSnapshot folded = session.projectSnapshot().value();
    session.reloadProject(folded);
    QCOMPARE(session.collapsedGroupIDs(), QSet<QUuid>{folder});
    session.reloadProject(ProjectStore::load(path));
    QVERIFY(session.collapsedGroupIDs().isEmpty());
    QCOMPARE(session.activeLayerID().value(), ProjectStore::load(path).manifest.activeLayerID.value());
    // A layer gone from disk: the loaded active one stands.
    ProjectSnapshot fewer = ProjectStore::load(path);
    fewer.manifest.layers.erase(fewer.manifest.layers.begin());
    fewer.images.erase(base);
    fewer.manifest.activeLayerID = blank;
    session.reloadProject(fewer);
    QCOMPARE(session.activeLayerID().value(), blank);
    QCOMPARE(session.selectedLayerIDs(), QSet<QUuid>{blank});
}

void ExternalChangeTests::writingThePackageElsewhereReloadsTheOpenProject()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    // The reload holds the project busy, then frees it.
    bool busyMeanwhile = false;
    QObject::connect(&session, &EditorSession::changed, [&] { busyMeanwhile = busyMeanwhile || session.isProjectBusy(); });
    renameFirstLayerOnDisk(path, QStringLiteral("Renamed elsewhere"));
    QTRY_COMPARE_WITH_TIMEOUT(controller->externalChanges.reloadCount, 1, 4000);
    QCOMPARE(firstName(session), QString("Renamed elsewhere"));
    QVERIFY(!session.isModified());
    QVERIFY(busyMeanwhile);
    QVERIFY(!session.isProjectBusy());
    // Every digest's watcher is gone once it answered.
    QTRY_VERIFY(controller->findChildren<QFutureWatcherBase *>().isEmpty());
    // The manifest rewritten in place, as a script would.
    const QString manifest = path + QStringLiteral("/manifest.json");
    rewrite(manifest, contents(manifest).replace("Renamed elsewhere", "Renamed again"));
    QTRY_COMPARE_WITH_TIMEOUT(controller->externalChanges.reloadCount, 2, 4000);
    QCOMPARE(firstName(session), QString("Renamed again"));
}

void ExternalChangeTests::ourOwnSaveAndMetadataTouchesDoNotReload()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    session.renameLayer(session.document().value().layers[0].id, QStringLiteral("Saved by us"));
    QVERIFY(answered([&](auto done) { controller->save(false, done); }));
    settle();
    QCOMPARE(controller->externalChanges.reloadCount, 0);
    QCOMPARE(firstName(session), QString("Saved by us"));
    QVERIFY(!session.isModified());
    const QString manifest = path + QStringLiteral("/manifest.json");
    rewrite(manifest, contents(manifest));
    settle();
    QCOMPARE(controller->externalChanges.reloadCount, 0);
    // Our save swapped the package in; the watch follows it.
    renameFirstLayerOnDisk(path, QStringLiteral("Renamed after our save"));
    QTRY_COMPARE_WITH_TIMEOUT(controller->externalChanges.reloadCount, 1, 4000);
    QCOMPARE(firstName(session), QString("Renamed after our save"));
}

void ExternalChangeTests::halfWrittenPackagesAreIgnoredUntilTheyLoad()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    const QString manifest = path + QStringLiteral("/manifest.json");
    const QByteArray good = contents(manifest);
    rewrite(manifest, "{ \"format\": \"com.compositor.project\", \"version\": 9, \"layers\": [");
    settle();
    QCOMPARE(controller->externalChanges.reloadCount, 0);
    QCOMPARE(session.document().value().layers.size(), size_t(2));
    QVERIFY(!session.isProjectBusy());
    rewrite(manifest, QByteArray(good).replace("\"Base\"", "\"Finished\""));
    QTRY_COMPARE_WITH_TIMEOUT(controller->externalChanges.reloadCount, 1, 4000);
    QCOMPARE(firstName(session), QString("Finished"));
}

void ExternalChangeTests::unsavedWorkIsNeverReplacedWithoutAsking()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    session.renameLayer(session.document().value().layers[0].id, QStringLiteral("Unsaved here"));
    renameFirstLayerOnDisk(path, QStringLiteral("Renamed elsewhere"));
    settle();
    // No window to ask in: the question waits.
    QCOMPARE(controller->externalChanges.reloadCount, 0);
    QVERIFY(controller->externalChanges.pending);
    QCOMPARE(firstName(session), QString("Unsaved here"));
    QVERIFY(session.isModified());
}

void ExternalChangeTests::unsavedWorkAsksToRevertOrKeep()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    QWidget window;
    window.show();
    controller->window = &window;
    DialogDesk desk;
    desk.replies = {"Keep Mine", "Revert"};
    session.renameLayer(session.document().value().layers[0].id, QStringLiteral("Unsaved here"));
    renameFirstLayerOnDisk(path, QStringLiteral("Renamed elsewhere"));
    QTRY_COMPARE_WITH_TIMEOUT(desk.seen.size(), 1, 4000);
    QCOMPARE(desk.seen[0], QString("alert|2|“Watched.comp” was changed on disk.|Another app changed this project. You can revert to the version on "
                                   "disk, losing your unsaved changes, or keep what you have.|Revert,Keep Mine"));
    settle();
    // Kept: the same package on disk asks nothing again.
    QCOMPARE(firstName(session), QString("Unsaved here"));
    QVERIFY(!controller->externalChanges.pending);
    QCOMPARE(controller->externalChanges.reloadCount, 0);
    QTRY_VERIFY(!window.findChild<QMessageBox *>());
    const QString manifest = path + QStringLiteral("/manifest.json");
    rewrite(manifest, contents(manifest));
    settle();
    QCOMPARE(desk.seen.size(), 1);
    renameFirstLayerOnDisk(path, QStringLiteral("Renamed twice"));
    QTRY_COMPARE_WITH_TIMEOUT(controller->externalChanges.reloadCount, 1, 4000);
    QCOMPARE(desk.seen.size(), 2);
    QCOMPARE(firstName(session), QString("Renamed twice"));
    QVERIFY(!session.isModified());
}

void ExternalChangeTests::aBackgroundTabAsksWhenItComesToTheFront()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    ProjectWorkspace workspace;
    QWidget window;
    window.show();
    workspace.window = &window;
    QVERIFY(answered([&](auto done) { workspace.open(path, done); }));
    const std::shared_ptr<ProjectTab> watched = workspace.tabs().back();
    watched->session.renameLayer(watched->session.document().value().layers[0].id, QStringLiteral("Unsaved here"));
    workspace.newCanvas();
    QVERIFY(&workspace.current() != watched.get());
    DialogDesk desk;
    desk.replies = {"Revert"};
    renameFirstLayerOnDisk(path, QStringLiteral("Renamed elsewhere"));
    settle();
    QVERIFY(desk.seen.isEmpty());
    QVERIFY(watched->controller.externalChanges.pending);
    workspace.select(watched->id);
    QTRY_COMPARE_WITH_TIMEOUT(watched->controller.externalChanges.reloadCount, 1, 4000);
    QCOMPARE(desk.seen.size(), 1);
    QCOMPARE(firstName(watched->session), QString("Renamed elsewhere"));
}

void ExternalChangeTests::aBusyProjectWaitsThenReloads()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    session.setIsProjectBusy(true);
    renameFirstLayerOnDisk(path, QStringLiteral("Renamed elsewhere"));
    settle();
    QCOMPARE(controller->externalChanges.reloadCount, 0);
    QVERIFY(controller->externalChanges.pending);
    QVERIFY(controller->externalChanges.recheckAttempt > 0);
    session.setIsProjectBusy(false);
    QTRY_COMPARE_WITH_TIMEOUT(controller->externalChanges.reloadCount, 1, 8000);
    QCOMPARE(firstName(session), QString("Renamed elsewhere"));
    QCOMPARE(controller->externalChanges.recheckAttempt, 0);
}

void ExternalChangeTests::closingOrANewCanvasStopsTheWatch()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    QCOMPARE(controller->externalChanges.watcher->path, path);
    QVERIFY(controller->externalChanges.knownDigest.has_value());
    controller->newCanvas();
    QTRY_VERIFY(!session.document());
    QVERIFY(!controller->externalChanges.watcher);
    QVERIFY(!controller->externalChanges.knownDigest);
    renameFirstLayerOnDisk(path, QStringLiteral("Renamed elsewhere"));
    settle();
    QCOMPARE(controller->externalChanges.reloadCount, 0);
    // Opened again and closed: the watch goes with the window.
    const auto reopened = opened(session, path);
    QWidget closing;
    closing.show();
    reopened->close(&closing);
    QTRY_VERIFY(!session.document());
    QVERIFY(!reopened->externalChanges.watcher);
}

void ExternalChangeTests::aNewCanvasDropsAWaitingRecheck()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    // An open transform holds the check back on a timer.
    session.selectTool(NavigationTool::move);
    session.selectLayer(session.document().value().layers[0].id);
    session.beginTransform();
    QVERIFY(session.transformEdit().has_value());
    renameFirstLayerOnDisk(path, QStringLiteral("Renamed elsewhere"));
    QTRY_VERIFY_WITH_TIMEOUT(controller->externalChanges.recheck->isActive(), 4000);
    QVERIFY(controller->externalChanges.pending);
    controller->newCanvas();
    QTRY_VERIFY(!session.document());
    QVERIFY(!controller->externalChanges.recheck->isActive());
    QVERIFY(!controller->externalChanges.pending);
}

void ExternalChangeTests::oneCheckRunsAtATime()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    renameFirstLayerOnDisk(path, QStringLiteral("Renamed elsewhere"));
    // Three asks before the watcher speaks: one reload between them.
    for (int ask = 0; ask < 3; ++ask) {
        controller->externalChanges.pending = true;
        controller->resumeExternalChangeCheck();
    }
    QVERIFY(controller->externalChanges.checking);
    QTRY_COMPARE_WITH_TIMEOUT(controller->externalChanges.reloadCount, 1, 4000);
    settle();
    QCOMPARE(controller->externalChanges.reloadCount, 1);
    QVERIFY(!controller->externalChanges.checking);
}

void ExternalChangeTests::aCheckWithoutAProjectOrDuringOurSaveEnds()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    // During our own save a change is ours: dropped.
    controller->externalChanges.saving = true;
    controller->externalChanges.pending = true;
    controller->resumeExternalChangeCheck();
    QVERIFY(!controller->externalChanges.checking);
    QVERIFY(!controller->externalChanges.pending);
    controller->externalChanges.saving = false;
    renameFirstLayerOnDisk(path, QStringLiteral("Renamed elsewhere"));
    QTRY_COMPARE_WITH_TIMEOUT(controller->externalChanges.reloadCount, 1, 4000);
    // Without a document nothing is checked, and checking ends.
    EditorSession empty;
    ProjectController idle(empty);
    idle.externalChanges.pending = true;
    idle.resumeExternalChangeCheck();
    QVERIFY(!idle.externalChanges.checking);
    QVERIFY(!idle.externalChanges.pending);
}

// docs/writing-comp-files.md: a same-size image needs new manifest bytes.
void ExternalChangeTests::theGuidesSameSizeRecipeReloads()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    const ImageLayer base = session.document().value().layers[0];
    const QString file = path + QStringLiteral("/images/") + base.id.toString(QUuid::WithoutBraces).toUpper() + QStringLiteral(".png");
    const qint64 size = QFileInfo(file).size();
    QImage blue(8, 6, QImage::Format_RGBA8888);
    blue.fill(QColor(60, 120, 200));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    QVERIFY(buffer.open(QIODevice::WriteOnly) && blue.save(&buffer, "PNG"));
    QCOMPARE(qint64(bytes.size()), size);
    rewrite(file, bytes);
    // The same manifest bytes again: nothing is noticed.
    const QString manifest = path + QStringLiteral("/manifest.json");
    rewrite(manifest, contents(manifest));
    settle();
    QCOMPARE(controller->externalChanges.reloadCount, 0);
    // Any change to its bytes, even whitespace: reloaded.
    rewrite(manifest, contents(manifest) + "\n");
    QTRY_COMPARE_WITH_TIMEOUT(controller->externalChanges.reloadCount, 1, 4000);
    QCOMPARE(session.document().value().layers[0].asset.value().image().pixelColor(0, 0), QColor(60, 120, 200));
}

QTEST_MAIN(ExternalChangeTests)
#include "ExternalChangeTests.moc"
