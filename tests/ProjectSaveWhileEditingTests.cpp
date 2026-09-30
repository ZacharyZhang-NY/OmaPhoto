#include "ExternalChangeFixtures.h"

// Swift's 940e188: editing goes on while a save writes.
class ProjectSaveWhileEditingTests : public QObject {
    Q_OBJECT
private slots:
    void editsMadeWhileWritingStayUnsaved();
    void aSecondSaveWaitsForTheFirst();
    void aNewCanvasWaitsForTheWriteBeforeAsking();
    void onlyTheCaptureAndPanelHoldTheTools();
    void aFailedWriteLeavesTheEditUnsaved();
    void theLastSignalShowsTheSavedState();
};

void ProjectSaveWhileEditingTests::editsMadeWhileWritingStayUnsaved()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    const QUuid base = session.document().value().layers[0].id;
    session.renameLayer(base, QStringLiteral("Captured"));
    std::optional<bool> saved;
    controller->save(false, [&](bool value) { saved = value; });
    // Captured: the tools are free while it writes.
    QVERIFY(!session.isProjectBusy());
    QVERIFY(controller->externalChanges.saving);
    session.renameLayer(base, QStringLiteral("After the capture"));
    QTRY_VERIFY(saved.has_value());
    QVERIFY(saved.value());
    QCOMPARE(ProjectStore::load(path).manifest.layers[0].name, QString("Captured"));
    QVERIFY(session.isModified());
    // Undoing back to the saved version is unmodified again.
    session.undo();
    QCOMPARE(firstName(session), QString("Captured"));
    QVERIFY(!session.isModified());
}

void ProjectSaveWhileEditingTests::aSecondSaveWaitsForTheFirst()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    const QUuid base = session.document().value().layers[0].id;
    session.renameLayer(base, QStringLiteral("First"));
    QStringList order;
    controller->save(false, [&](bool saved) { order << QStringLiteral("first=%1").arg(saved); });
    session.renameLayer(base, QStringLiteral("Second"));
    controller->save(false, [&](bool saved) {
        order << QStringLiteral("second=%1 name=%2").arg(saved).arg(ProjectStore::load(path).manifest.layers[0].name);
    });
    // Waiting, the second save holds nothing.
    QVERIFY(!session.isProjectBusy());
    QTRY_COMPARE(order.size(), 2);
    QCOMPARE(order, (QStringList{"first=1", "second=1 name=Second"}));
    QVERIFY(!session.isModified());
}

void ProjectSaveWhileEditingTests::aNewCanvasWaitsForTheWriteBeforeAsking()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    QWidget window;
    window.show();
    controller->window = &window;
    const QUuid base = session.document().value().layers[0].id;
    session.renameLayer(base, QStringLiteral("Written"));
    DialogDesk desk;
    desk.replies = {"Don’t Save"};
    desk.note = [&] { return QStringLiteral("saving=%1").arg(controller->externalChanges.saving); };
    std::optional<bool> saved;
    controller->save(false, [&](bool value) { saved = value; });
    session.renameLayer(base, QStringLiteral("Unsaved"));
    controller->newCanvas();
    // The question comes once the file is whole.
    QTRY_VERIFY(!session.document());
    QCOMPARE(desk.seen.size(), 1);
    QVERIFY2(desk.seen[0].startsWith("alert|2|Save changes to Watched.comp?") && desk.seen[0].endsWith("|saving=0"), qPrintable(desk.seen[0]));
    QVERIFY(saved.value());
    QCOMPARE(ProjectStore::load(path).manifest.layers[0].name, QString("Written"));
}

void ProjectSaveWhileEditingTests::onlyTheCaptureAndPanelHoldTheTools()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    DialogDesk desk;
    const QString copy = root.filePath(QStringLiteral("Copy.comp"));
    desk.replies = {copy};
    desk.note = [&] { return session.isProjectBusy() ? QStringLiteral("busy") : QStringLiteral("free"); };
    std::optional<bool> saved;
    bool busyWhileWriting = false;
    QObject::connect(&session, &EditorSession::changed, [&] {
        busyWhileWriting = busyWhileWriting || (controller->externalChanges.saving && session.isProjectBusy());
    });
    controller->save(true, [&](bool value) { saved = value; });
    QTRY_VERIFY(saved.has_value());
    QVERIFY(saved.value());
    QVERIFY2(desk.seen.value(0).endsWith("|busy"), qPrintable(desk.seen.value(0)));
    QVERIFY(!busyWhileWriting);
    QCOMPARE(session.projectPath().value(), copy);
}

void ProjectSaveWhileEditingTests::aFailedWriteLeavesTheEditUnsaved()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    const QString locked = root.filePath(QStringLiteral("Locked"));
    QVERIFY(QDir().mkpath(locked));
    QVERIFY(QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    session.renameLayer(session.document().value().layers[0].id, QStringLiteral("Unsaved"));
    DialogDesk desk;
    desk.replies = {locked + QStringLiteral("/Copy.comp"), "OK"};
    QVERIFY(!answered([&](auto done) { controller->save(true, done); }));
    QVERIFY(session.isModified());
    QCOMPARE(session.projectPath().value(), path);
    // Nothing waits on a write that failed.
    QVERIFY(answered([&](auto done) { controller->save(false, done); }));
    QVERIFY(!session.isModified());
    QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

void ProjectSaveWhileEditingTests::theLastSignalShowsTheSavedState()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    session.renameLayer(session.document().value().layers[0].id, QStringLiteral("Saved"));
    std::optional<bool> modifiedAtLastSignal;
    QObject::connect(&session, &EditorSession::changed, [&] { modifiedAtLastSignal = session.isModified(); });
    QVERIFY(answered([&](auto done) { controller->save(false, done); }));
    QCOMPARE(modifiedAtLastSignal, std::optional<bool>(false));
}

QTEST_MAIN(ProjectSaveWhileEditingTests)
#include "ProjectSaveWhileEditingTests.moc"
