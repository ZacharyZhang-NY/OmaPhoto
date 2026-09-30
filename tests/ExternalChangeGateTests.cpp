#include "ExternalChangeFixtures.h"

// What holds a change back, and what a reload keeps.
class ExternalChangeGateTests : public QObject {
    Q_OBJECT
private slots:
    void aFailedSaveKeepsItsGuardUntilTheAlertIsAnswered();
    void aManagingWorkspaceHoldsTheCheck();
    void theBackOffDoublesToACap();
    void aLoadLandingElsewhereInstallsNothing();
    void aReloadKeepsTheWholeViewAndAnnouncesItLast();
    void anUnreadableManifestAsksNothing();
    void saveAsAndOpenMoveTheWatch();
    void aSecondChangeWhileAskingIsHeard();
};

void ExternalChangeGateTests::aFailedSaveKeepsItsGuardUntilTheAlertIsAnswered()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    QWidget window;
    window.show();
    controller->window = &window;
    session.renameLayer(session.document().value().layers[0].id, QStringLiteral("Unsaved here"));
    // Save As into a folder that takes nothing.
    const QString locked = root.filePath(QStringLiteral("Locked"));
    QVERIFY(QDir().mkpath(locked));
    QVERIFY(QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    DialogDesk desk;
    desk.replies = {locked + QStringLiteral("/Copy.comp"), "OK"};
    // While the alert is up, changes on disk drop.
    desk.note = [&] {
        renameFirstLayerOnDisk(path, QStringLiteral("Renamed elsewhere"));
        QTest::qWait(900);
        return QStringLiteral("saving=%1 reloads=%2").arg(controller->externalChanges.saving).arg(controller->externalChanges.reloadCount);
    };
    QVERIFY(!answered([&](auto done) { controller->save(true, done); }));
    QCOMPARE(desk.seen.size(), 2);
    QVERIFY2(desk.seen[1].endsWith("|saving=1 reloads=0"), qPrintable(desk.seen[1]));
    QVERIFY(!controller->externalChanges.saving);
    settle();
    QCOMPARE(desk.seen.size(), 2);
    QCOMPARE(controller->externalChanges.reloadCount, 0);
    QCOMPARE(firstName(session), QString("Unsaved here"));
    QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

void ExternalChangeGateTests::aManagingWorkspaceHoldsTheCheck()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    ProjectWorkspace workspace;
    QWidget window;
    window.show();
    workspace.window = &window;
    QVERIFY(answered([&](auto done) { workspace.open(path, done); }));
    const std::shared_ptr<ProjectTab> watched = workspace.tabs().back();
    workspace.newCanvas();
    const std::shared_ptr<ProjectTab> other = workspace.tabs().back();
    other->session.createDocument(8, 6);
    other->session.addBlankLayer();
    workspace.select(watched->id);
    // Closing the other tab asks; meanwhile the workspace manages.
    DialogDesk desk;
    desk.replies = {"Cancel"};
    desk.note = [&] {
        renameFirstLayerOnDisk(path, QStringLiteral("Renamed elsewhere"));
        QTest::qWait(900);
        return QStringLiteral("managing=%1 reloads=%2 recheck=%3")
            .arg(workspace.isManaging())
            .arg(watched->controller.externalChanges.reloadCount)
            .arg(watched->controller.externalChanges.recheck->isActive());
    };
    std::optional<bool> closed;
    workspace.close(other->id, [&] { closed = true; });
    QTRY_VERIFY(closed.has_value());
    QCOMPARE(desk.seen.size(), 1);
    QVERIFY2(desk.seen[0].endsWith("|managing=1 reloads=0 recheck=1"), qPrintable(desk.seen[0]));
    QTRY_COMPARE_WITH_TIMEOUT(watched->controller.externalChanges.reloadCount, 1, 4000);
    QCOMPARE(firstName(watched->session), QString("Renamed elsewhere"));
}

void ExternalChangeGateTests::theBackOffDoublesToACap()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    ExternalChangeState &state = controller->externalChanges;
    QCOMPARE(state.recheck->timerType(), Qt::PreciseTimer);
    session.setIsProjectBusy(true);
    renameFirstLayerOnDisk(path, QStringLiteral("Renamed elsewhere"));
    QTRY_VERIFY_WITH_TIMEOUT(state.recheck->isActive(), 4000);
    QCOMPARE(state.recheck->interval(), 250);
    // Each busy check waits twice as long, to 32 seconds.
    for (const int expected : {500, 1000, 2000, 4000, 8000, 16000, 32000, 32000}) {
        state.pending = true;
        controller->resumeExternalChangeCheck();
        QTRY_COMPARE(state.recheck->interval(), expected);
        QVERIFY(state.recheck->isActive());
    }
    QCOMPARE(state.recheckAttempt, 7);
    session.setIsProjectBusy(false);
    state.pending = true;
    controller->resumeExternalChangeCheck();
    QTRY_COMPARE_WITH_TIMEOUT(state.reloadCount, 1, 4000);
    QCOMPARE(state.recheckAttempt, 0);
}

void ExternalChangeGateTests::aLoadLandingElsewhereInstallsNothing()
{
    for (const bool clears : {false, true}) {
        QTemporaryDir root;
        const QString path = savedProject(root);
        EditorSession session;
        const auto controller = opened(session, path);
        // As the reload begins, the project moves or closes.
        bool moved = false;
        QObject::connect(&session, &EditorSession::changed, [&] {
            if (moved || !session.isProjectBusy())
                return;
            moved = true;
            if (clears)
                session.clearProject();
            else
                session.setProjectPath(root.filePath(QStringLiteral("Elsewhere.comp")));
        });
        renameFirstLayerOnDisk(path, QStringLiteral("Renamed elsewhere"));
        QTRY_VERIFY_WITH_TIMEOUT(moved, 4000);
        QTRY_VERIFY(!session.isProjectBusy());
        settle();
        QCOMPARE(controller->externalChanges.reloadCount, 0);
        QCOMPARE(session.document().has_value(), !clears);
        if (!clears)
            QCOMPARE(firstName(session), QString("Base"));
    }
}

void ExternalChangeGateTests::aReloadKeepsTheWholeViewAndAnnouncesItLast()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    session.installProject(ProjectStore::load(path), path);
    session.viewport.resize(QSizeF(800, 600), 1, session.document().value().size());
    session.zoom(3);
    session.viewport.translate(QSizeF(40, -30));
    const CanvasViewport view = session.viewport;
    const QUuid base = session.document().value().layers[0].id;
    const QUuid blank = session.document().value().layers[1].id;
    session.selectLayer(blank);
    session.extendSelection(base);
    // Disk drops the other selected layer; the active one stays.
    ProjectSnapshot fewer = ProjectStore::load(path);
    fewer.manifest.layers.erase(fewer.manifest.layers.begin() + 1);
    std::optional<std::tuple<CanvasViewport, std::optional<QUuid>, QSet<QUuid>>> last;
    QObject::connect(&session, &EditorSession::changed, [&] { last = {session.viewport, session.activeLayerID(), session.selectedLayerIDs()}; });
    session.reloadProject(fewer);
    QVERIFY(session.viewport == view);
    QCOMPARE(session.activeLayerID().value(), base);
    QCOMPARE(session.selectedLayerIDs(), QSet<QUuid>{base});
    QVERIFY(std::get<0>(last.value()) == view);
    QCOMPARE(std::get<1>(last.value()), std::optional<QUuid>(base));
    QCOMPARE(std::get<2>(last.value()), QSet<QUuid>{base});
    // Both layers on disk: the last signal holds both selected.
    session.reloadProject(ProjectStore::load(path));
    session.selectLayer(blank);
    session.extendSelection(base);
    session.reloadProject(ProjectStore::load(path));
    QCOMPARE(session.selectedLayerIDs(), (QSet<QUuid>{base, blank}));
    QCOMPARE(std::get<2>(last.value()), (QSet<QUuid>{base, blank}));
}

void ExternalChangeGateTests::anUnreadableManifestAsksNothing()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    QWidget window;
    window.show();
    controller->window = &window;
    session.renameLayer(session.document().value().layers[0].id, QStringLiteral("Unsaved here"));
    DialogDesk desk;
    // No digest: the package is left alone, no question.
    const QString manifest = path + QStringLiteral("/manifest.json");
    QVERIFY(QFile::remove(manifest));
    QVERIFY(QDir().mkpath(manifest));
    settle();
    QVERIFY(desk.seen.isEmpty());
    QCOMPARE(controller->externalChanges.reloadCount, 0);
    QCOMPARE(firstName(session), QString("Unsaved here"));
}

void ExternalChangeGateTests::saveAsAndOpenMoveTheWatch()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    const QString copy = root.filePath(QStringLiteral("Copy.comp"));
    {
        DialogDesk desk;
        desk.replies = {copy};
        QVERIFY(answered([&](auto done) { controller->save(true, done); }));
    }
    QCOMPARE(controller->externalChanges.watcher->path, copy);
    QCOMPARE(controller->externalChanges.knownDigest.value(), ProjectDigest::compute(copy));
    renameFirstLayerOnDisk(copy, QStringLiteral("Copy renamed"));
    QTRY_COMPARE_WITH_TIMEOUT(controller->externalChanges.reloadCount, 1, 4000);
    QCOMPARE(firstName(session), QString("Copy renamed"));
    // The former package is no longer this project.
    renameFirstLayerOnDisk(path, QStringLiteral("Former renamed"));
    settle();
    QCOMPARE(controller->externalChanges.reloadCount, 1);
    // Opening another package moves the watch there.
    QVERIFY(answered([&](auto done) { controller->open(path, done); }));
    QCOMPARE(controller->externalChanges.watcher->path, path);
    QCOMPARE(firstName(session), QString("Former renamed"));
    renameFirstLayerOnDisk(copy, QStringLiteral("Copy again"));
    settle();
    QCOMPARE(controller->externalChanges.reloadCount, 1);
}

void ExternalChangeGateTests::aSecondChangeWhileAskingIsHeard()
{
    for (const bool revert : {false, true}) {
        QTemporaryDir root;
        const QString path = savedProject(root);
        EditorSession session;
        const auto controller = opened(session, path);
        QWidget window;
        window.show();
        controller->window = &window;
        session.renameLayer(session.document().value().layers[0].id, QStringLiteral("Unsaved here"));
        DialogDesk desk;
        desk.replies = revert ? QStringList{"Revert"} : QStringList{"Keep Mine", "Keep Mine"};
        // A second version arrives while the first question waits.
        desk.note = [&] {
            if (desk.seen.isEmpty()) {
                renameFirstLayerOnDisk(path, QStringLiteral("Second version"));
                QTest::qWait(900);
            }
            return QStringLiteral("pending=%1").arg(controller->externalChanges.pending);
        };
        renameFirstLayerOnDisk(path, QStringLiteral("First version"));
        QTRY_COMPARE_WITH_TIMEOUT(desk.seen.size(), revert ? 1 : 2, 6000);
        QVERIFY2(desk.seen[0].endsWith("|pending=1"), qPrintable(desk.seen[0]));
        QTRY_VERIFY_WITH_TIMEOUT(!controller->externalChanges.checking, 4000);
        settle();
        QCOMPARE(desk.seen.size(), revert ? 1 : 2);
        QVERIFY(!controller->externalChanges.pending);
        QCOMPARE(controller->externalChanges.knownDigest.value(), ProjectDigest::compute(path));
        QCOMPARE(controller->externalChanges.reloadCount, revert ? 1 : 0);
        QCOMPARE(firstName(session), revert ? QString("Second version") : QString("Unsaved here"));
    }
}

QTEST_MAIN(ExternalChangeGateTests)
#include "ExternalChangeGateTests.moc"
