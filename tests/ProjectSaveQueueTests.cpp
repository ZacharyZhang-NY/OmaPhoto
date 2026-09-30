#include "SaveHooks.h"

// Saves queued behind a write: one writer at a time.
class ProjectSaveQueueTests : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void threeSavesWriteOneAfterAnother();
    void aSaveAskedAsTheToolsFreeWaitsItsTurn();
    void aQuitWaitsForTheWriteBeforeAsking();
    void aQueuedSaveWithoutADocumentSavesNothing();
    void aFailedWriteHoldsItsWaitersUntilAnswered();
    void reopeningWhileSavingReadsTheSavedPackage();
    void closingATabWaitsForItsWrite();
    void aControllerGoneMidWriteLeavesNoCallback();
    void anExportDuringAWriteSucceedsBeside();
};

void ProjectSaveQueueTests::init()
{
    resetHooks();
}

void ProjectSaveQueueTests::cleanup()
{
    resetHooks();
}

void ProjectSaveQueueTests::threeSavesWriteOneAfterAnother()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    const QUuid base = session.document().value().layers[0].id;
    QStringList order;
    for (const QString &name : {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}) {
        session.renameLayer(base, name);
        controller->save(false, [&, name](bool saved) {
            order << QStringLiteral("%1=%2 disk=%3").arg(name).arg(saved).arg(ProjectStore::load(path).manifest.layers[0].name);
        });
    }
    session.renameLayer(base, QStringLiteral("Last"));
    QTRY_COMPARE_WITH_TIMEOUT(order.size(), 3, 10000);
    // A captured itself; the rest waited and captured the latest.
    QCOMPARE(order, (QStringList{"A=1 disk=A", "B=1 disk=Last", "C=1 disk=Last"}));
    QCOMPARE(mostAtOnce.load(), 1);
    QVERIFY(!session.isModified());
    QVERIFY(!controller->externalChanges.saving);
}

void ProjectSaveQueueTests::aSaveAskedAsTheToolsFreeWaitsItsTurn()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    session.renameLayer(session.document().value().layers[0].id, QStringLiteral("First"));
    // The moment the tools go free, an observer saves again.
    std::optional<bool> first, second;
    bool asked = false, wasBusy = false, reserved = false;
    QObject::connect(&session, &EditorSession::changed, [&] {
        const bool freed = wasBusy && !session.isProjectBusy();
        wasBusy = session.isProjectBusy();
        if (asked || !freed)
            return;
        asked = true;
        reserved = controller->externalChanges.saving;
        controller->save(false, [&](bool saved) { second = saved; });
    });
    controller->save(false, [&](bool saved) { first = saved; });
    QTRY_VERIFY_WITH_TIMEOUT(first.has_value() && second.has_value(), 10000);
    QVERIFY(asked);
    QVERIFY(reserved);
    QVERIFY(first.value() && second.value());
    QCOMPARE(mostAtOnce.load(), 1);
}

void ProjectSaveQueueTests::aQuitWaitsForTheWriteBeforeAsking()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    QWidget window;
    window.show();
    controller->window = &window;
    const QUuid base = session.document().value().layers[0].id;
    QStringList order;
    session.renameLayer(base, QStringLiteral("One"));
    controller->save(false, [&](bool saved) { order << QStringLiteral("one=%1").arg(saved); });
    session.renameLayer(base, QStringLiteral("Two"));
    // The quit asks only once the write is whole.
    DialogDesk desk;
    desk.replies = {"Don’t Save"};
    desk.note = [&] { return QStringLiteral("disk=%1 saving=%2").arg(ProjectStore::load(path).manifest.layers[0].name).arg(controller->externalChanges.saving); };
    controller->confirmQuit([&](bool quits) { order << QStringLiteral("quit=%1").arg(quits); });
    QTRY_COMPARE_WITH_TIMEOUT(order.size(), 2, 10000);
    QCOMPARE(order, (QStringList{"one=1", "quit=1"}));
    QCOMPARE(desk.seen.size(), 1);
    QVERIFY2(desk.seen[0].endsWith("|disk=One saving=0"), qPrintable(desk.seen[0]));
}

void ProjectSaveQueueTests::aQueuedSaveWithoutADocumentSavesNothing()
{
    QTemporaryDir root;
    EditorSession session;
    ProjectController controller(session);
    session.createDocument(8, 6);
    session.addBlankLayer();
    const QString path = root.filePath(QStringLiteral("New.comp"));
    DialogDesk desk;
    desk.replies = {path};
    std::optional<bool> first, second;
    controller.save(false, [&](bool saved) { first = saved; });
    QTRY_VERIFY_WITH_TIMEOUT(controller.externalChanges.saving, 4000);
    controller.save(false, [&](bool saved) { second = saved; });
    // Undo takes the canvas back while the first write runs.
    while (session.document() && session.canUndo())
        session.undo();
    QVERIFY(!session.document());
    QTRY_VERIFY_WITH_TIMEOUT(first.has_value() && second.has_value(), 10000);
    QVERIFY(first.value());
    QVERIFY(!second.value());
    QVERIFY(!session.isProjectBusy());
    QCOMPARE(desk.seen.size(), 1);
}

void ProjectSaveQueueTests::aFailedWriteHoldsItsWaitersUntilAnswered()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    const QString locked = root.filePath(QStringLiteral("Locked"));
    QVERIFY(QDir().mkpath(locked));
    QVERIFY(QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    session.renameLayer(session.document().value().layers[0].id, QStringLiteral("Unsaved"));
    std::optional<bool> failed, again, quit;
    DialogDesk desk;
    desk.replies = {locked + QStringLiteral("/Copy.comp"), "OK", "Don’t Save"};
    // With the failure's alert up, a save and quit queue.
    desk.note = [&] {
        if (desk.seen.size() != 1)
            return QString();
        controller->save(false, [&](bool saved) { again = saved; });
        controller->confirmQuit([&](bool quits) { quit = quits; });
        QTest::qWait(300);
        return QStringLiteral("again=%1 quit=%2").arg(again.has_value()).arg(quit.has_value());
    };
    controller->save(true, [&](bool saved) { failed = saved; });
    QTRY_VERIFY_WITH_TIMEOUT(failed.has_value() && again.has_value() && quit.has_value(), 10000);
    QVERIFY2(desk.seen.value(1).endsWith("|again=0 quit=0"), qPrintable(desk.seen.value(1)));
    QVERIFY(!failed.value());
    // The quit took the tools first: the save is refused.
    QVERIFY(!again.value());
    QVERIFY(quit.value());
    QCOMPARE(desk.seen.size(), 3);
    QCOMPARE(ProjectStore::load(path).manifest.layers[0].name, QString("Base"));
    QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

void ProjectSaveQueueTests::reopeningWhileSavingReadsTheSavedPackage()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    session.renameLayer(session.document().value().layers[0].id, QStringLiteral("Newest saved version"));
    std::optional<bool> saved, reopened;
    controller->save(false, [&](bool value) { saved = value; });
    controller->open(path, [&](bool value) { reopened = value; });
    QTRY_VERIFY_WITH_TIMEOUT(saved.has_value() && reopened.has_value(), 10000);
    QVERIFY(saved.value() && reopened.value());
    QCOMPARE(firstName(session), QString("Newest saved version"));
    QVERIFY(!session.isModified());
}

void ProjectSaveQueueTests::closingATabWaitsForItsWrite()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    ProjectWorkspace workspace;
    QVERIFY(answered([&](auto done) { workspace.open(path, done); }));
    const std::shared_ptr<ProjectTab> tab = workspace.tabs().back();
    tab->session.renameLayer(tab->session.document().value().layers[0].id, QStringLiteral("Written"));
    std::optional<bool> saved;
    tab->controller.save(false, [&](bool value) { saved = value; });
    QVERIFY(tab->controller.externalChanges.saving);
    bool closed = false;
    workspace.close(tab->id, [&] { closed = true; });
    // The tab stays until its package is whole.
    QTRY_VERIFY_WITH_TIMEOUT(closed, 10000);
    QVERIFY(saved.value());
    QVERIFY(std::none_of(workspace.tabs().begin(), workspace.tabs().end(), [&](const auto &each) { return each->id == tab->id; }));
    QCOMPARE(ProjectStore::load(path).manifest.layers[0].name, QString("Written"));
}

void ProjectSaveQueueTests::aControllerGoneMidWriteLeavesNoCallback()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    auto controller = opened(session, path);
    session.renameLayer(session.document().value().layers[0].id, QStringLiteral("Written"));
    bool first = false, second = false;
    controller->save(false, [&](bool) { first = true; });
    controller->save(false, [&](bool) { second = true; });
    controller.reset();
    QTest::qWait(1500);
    QVERIFY(!first && !second);
    // The worker finished its package all the same.
    QCOMPARE(ProjectStore::load(path).manifest.layers[0].name, QString("Written"));
}

void ProjectSaveQueueTests::anExportDuringAWriteSucceedsBeside()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    session.renameLayer(session.document().value().layers[0].id, QStringLiteral("Written"));
    std::optional<bool> saved;
    controller->save(false, [&](bool value) { saved = value; });
    // The tools are free: an export starts beside the write.
    const QString png = root.filePath(QStringLiteral("Picture.png"));
    DialogDesk desk;
    desk.replies = {png};
    bool exported = false;
    controller->exportPNG([&] { exported = true; });
    QTRY_VERIFY_WITH_TIMEOUT(exported && saved.has_value(), 10000);
    QVERIFY(saved.value());
    QVERIFY(QFile::exists(png));
    QCOMPARE(ProjectStore::load(path).manifest.layers[0].name, QString("Written"));
}

QTEST_MAIN(ProjectSaveQueueTests)
#include "ProjectSaveQueueTests.moc"
