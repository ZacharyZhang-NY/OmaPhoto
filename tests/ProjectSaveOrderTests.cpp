#include "SaveHooks.h"
#include "UI/ColorPickerSheet.h"

// A write held mid-save: what waits, what goes beside.
class ProjectSaveOrderTests : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void aReopenReadsNothingWhileTheWriteIsHeld();
    void aRevertAnsweredMidSaveLoadsWhatWeWrote();
    void theGuardLastsThroughTheSavesDigest();
    void canvasSizeWorksBesideAHeldWrite();
    void imageSizeWorksBesideAHeldWrite();
    void aDigestOutWhenOurSaveBeginsIsDropped();
    void aFailedSaveKeepsAnExternalChangeInLine();
    void aSecondSaveWaitsThroughTheFirstDigest();
    void canvasSizeCommittedBeforeTheSaveLands();
    void imageSizeCommittedAfterTheSaveLands();
    void keepMineAfterASaveKeepsTheSavedDigest();
};

void ProjectSaveOrderTests::init()
{
    resetHooks();
}

void ProjectSaveOrderTests::cleanup()
{
    resetHooks();
}

void ProjectSaveOrderTests::aReopenReadsNothingWhileTheWriteIsHeld()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    session.renameLayer(session.document().value().layers[0].id, QStringLiteral("Latest saved version"));
    holdSwap = true;
    std::optional<bool> saved, reopened;
    controller->save(false, [&](bool value) { saved = value; });
    const int before = loads;
    controller->open(path, [&](bool value) { reopened = value; });
    QTest::qWait(400);
    // Held mid-save: the open has not read the package.
    QCOMPARE(loads.load(), before);
    QVERIFY(!reopened.has_value());
    holdSwap = false;
    QTRY_VERIFY_WITH_TIMEOUT(saved.has_value() && reopened.has_value(), 10000);
    QCOMPARE(firstName(session), QString("Latest saved version"));
    QVERIFY(!session.isModified());
}

void ProjectSaveOrderTests::aRevertAnsweredMidSaveLoadsWhatWeWrote()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    QWidget window;
    window.show();
    controller->window = &window;
    session.renameLayer(session.document().value().layers[0].id, QStringLiteral("Ours"));
    std::optional<bool> saved;
    int loadsWhileHeld = -1;
    DialogDesk desk;
    desk.replies = {"Revert"};
    // With the question up, we save; the write is held.
    desk.note = [&] {
        holdSwap = true;
        controller->save(false, [&](bool value) { saved = value; });
        const int before = loads;
        QTimer::singleShot(400, [&, before] {
            loadsWhileHeld = loads - before;
            holdSwap = false;
        });
        return QString();
    };
    renameFirstLayerOnDisk(path, QStringLiteral("Theirs"));
    QTRY_VERIFY_WITH_TIMEOUT(saved.has_value(), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(controller->externalChanges.reloadCount, 1, 10000);
    QCOMPARE(loadsWhileHeld, 0);
    QCOMPARE(firstName(session), QString("Ours"));
    QVERIFY(!session.isModified());
    QCOMPARE(controller->externalChanges.knownDigest.value(), ProjectDigest::compute(path));
}

void ProjectSaveOrderTests::theGuardLastsThroughTheSavesDigest()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    session.renameLayer(session.document().value().layers[0].id, QStringLiteral("Saved"));
    // Just after the path is announced, the digest is out.
    std::optional<bool> guarded;
    QObject::connect(&session, &EditorSession::changed, [&] {
        if (guarded.has_value() || session.isModified())
            return;
        QMetaObject::invokeMethod(controller.get(), [&] { guarded = controller->externalChanges.saving; }, Qt::QueuedConnection);
    });
    QVERIFY(answered([&](auto done) { controller->save(false, done); }));
    QCOMPARE(guarded, std::optional<bool>(true));
    QVERIFY(!controller->externalChanges.saving);
}

void ProjectSaveOrderTests::canvasSizeWorksBesideAHeldWrite()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    QWidget window;
    window.show();
    controller->window = &window;
    session.renameLayer(session.document().value().layers[0].id, QStringLiteral("Captured"));
    holdSwap = true;
    std::optional<bool> saved;
    controller->save(false, [&](bool value) { saved = value; });
    // The sheet opens beside the held write, holding the tools.
    bool resized = false;
    controller->canvasSize([&] { resized = true; });
    QTRY_VERIFY(window.findChild<QDialog *>());
    QVERIFY(session.isProjectBusy());
    holdSwap = false;
    QTRY_VERIFY_WITH_TIMEOUT(saved.has_value(), 10000);
    // Saved with the sheet still up: the tools stay held.
    QVERIFY(session.isProjectBusy());
    auto *width = window.findChild<QDialog *>()->findChild<PickerField *>(QStringLiteral("canvasWidth"));
    width->setFocus();
    width->selectAll();
    QTest::keyClicks(width, QStringLiteral("20"));
    QTest::keyClick(width, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(resized, 10000);
    QCOMPARE(session.document().value().width, 20);
    QCOMPARE(ProjectStore::load(path).manifest.width, qint64(8));
    QVERIFY(session.isModified());
    session.undo();
    QVERIFY(!session.isModified());
    QCOMPARE(session.document().value().width, 8);
}

void ProjectSaveOrderTests::imageSizeWorksBesideAHeldWrite()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    QWidget window;
    window.show();
    controller->window = &window;
    session.renameLayer(session.document().value().layers[0].id, QStringLiteral("Captured"));
    holdSwap = true;
    std::optional<bool> saved;
    controller->save(false, [&](bool value) { saved = value; });
    bool resized = false;
    controller->imageSize([&] { resized = true; });
    QTRY_VERIFY(window.findChild<QDialog *>());
    // Resized before the write lands: the capture stays.
    auto *width = window.findChild<QDialog *>()->findChild<PickerField *>(QStringLiteral("imageWidth"));
    width->setFocus();
    width->selectAll();
    QTest::keyClicks(width, QStringLiteral("16"));
    QTest::keyClick(width, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(resized, 10000);
    QCOMPARE(session.document().value().width, 16);
    QCOMPARE(session.document().value().height, 12);
    QVERIFY(!saved.has_value());
    holdSwap = false;
    QTRY_VERIFY_WITH_TIMEOUT(saved.has_value(), 10000);
    QVERIFY(saved.value());
    QCOMPARE(ProjectStore::load(path).manifest.width, qint64(8));
    QVERIFY(session.isModified());
    session.undo();
    QVERIFY(!session.isModified());
}

void ProjectSaveOrderTests::aDigestOutWhenOurSaveBeginsIsDropped()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    // The check's digest waits at the images listing.
    heldListing = QFile::encodeName(QFileInfo(path + QStringLiteral("/images")).canonicalFilePath());
    holdListing = true;
    renameFirstLayerOnDisk(path, QStringLiteral("Theirs"));
    QTRY_VERIFY_WITH_TIMEOUT(listingsHeld > 0, 4000);
    // Our save starts, held at its swap; the digest returns.
    holdSwap = true;
    std::optional<bool> saved;
    controller->save(false, [&](bool value) { saved = value; });
    QVERIFY(controller->externalChanges.saving);
    holdListing = false;
    QTest::qWait(400);
    QVERIFY(!controller->externalChanges.checking);
    holdSwap = false;
    QTRY_VERIFY_WITH_TIMEOUT(saved.has_value(), 10000);
    settle();
    // What it read is ours now: no reload, nothing asked.
    QCOMPARE(controller->externalChanges.reloadCount, 0);
    QCOMPARE(firstName(session), QString("Base"));
    QCOMPARE(ProjectStore::load(path).manifest.layers[0].name, QString("Base"));
    QCOMPARE(controller->externalChanges.knownDigest.value(), ProjectDigest::compute(path));
}

void ProjectSaveOrderTests::keepMineAfterASaveKeepsTheSavedDigest()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    QWidget window;
    window.show();
    controller->window = &window;
    session.renameLayer(session.document().value().layers[0].id, QStringLiteral("Ours"));
    DialogDesk desk;
    desk.replies = {"Keep Mine"};
    // With the question up, our save lands first.
    desk.note = [&] {
        std::optional<bool> saved;
        controller->save(false, [&](bool value) { saved = value; });
        if (!QTest::qWaitFor([&] { return saved.has_value(); }, 10000))
            return QStringLiteral("saved=never");
        return QStringLiteral("saved=%1").arg(saved.value_or(false));
    };
    renameFirstLayerOnDisk(path, QStringLiteral("Theirs"));
    QTRY_COMPARE_WITH_TIMEOUT(desk.seen.size(), 1, 6000);
    QVERIFY2(desk.seen[0].endsWith("|saved=1"), qPrintable(desk.seen[0]));
    settle();
    QCOMPARE(controller->externalChanges.knownDigest.value(), ProjectDigest::compute(path));
    // A touch of our own package changes nothing.
    const QString manifest = path + QStringLiteral("/manifest.json");
    rewrite(manifest, contents(manifest));
    settle();
    QCOMPARE(controller->externalChanges.reloadCount, 0);
    QCOMPARE(desk.seen.size(), 1);
    QCOMPARE(firstName(session), QString("Ours"));
}

void ProjectSaveOrderTests::aFailedSaveKeepsAnExternalChangeInLine()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    heldListing = QFile::encodeName(QFileInfo(path + QStringLiteral("/images")).canonicalFilePath());
    holdListing = true;
    renameFirstLayerOnDisk(path, QStringLiteral("External version"));
    QTRY_VERIFY_WITH_TIMEOUT(listingsHeld > 0, 4000);
    // Our save fails; the digest returns under its alert.
    QVERIFY(QFile::setPermissions(root.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    DialogDesk desk;
    desk.replies = {"OK"};
    desk.note = [&] {
        holdListing = false;
        QTest::qWait(400);
        return QStringLiteral("saving=%1").arg(controller->externalChanges.saving);
    };
    std::optional<bool> saved;
    controller->save(false, [&](bool value) { saved = value; });
    QTRY_VERIFY_WITH_TIMEOUT(saved.has_value(), 10000);
    QVERIFY(!saved.value());
    QVERIFY2(desk.seen.value(0).endsWith("|saving=1"), qPrintable(desk.seen.value(0)));
    QFile::setPermissions(root.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    // After the failure, the change it saw is taken up.
    QTRY_COMPARE_WITH_TIMEOUT(controller->externalChanges.reloadCount, 1, 6000);
    QCOMPARE(firstName(session), QString("External version"));
    QVERIFY(!controller->externalChanges.pending);
}

void ProjectSaveOrderTests::aSecondSaveWaitsThroughTheFirstDigest()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    const QUuid base = session.document().value().layers[0].id;
    // The first save's digest waits at the images listing.
    heldListing = QFile::encodeName(QFileInfo(path + QStringLiteral("/images")).canonicalFilePath());
    holdListing = true;
    session.renameLayer(base, QStringLiteral("First"));
    std::optional<bool> first, second;
    controller->save(false, [&](bool value) { first = value; });
    QTRY_VERIFY_WITH_TIMEOUT(swaps == 1 && listingsHeld > 0, 6000);
    session.renameLayer(base, QStringLiteral("Second"));
    controller->save(false, [&](bool value) { second = value; });
    QTest::qWait(400);
    QCOMPARE(swaps.load(), 1);
    QVERIFY(!first.has_value() && !second.has_value());
    holdListing = false;
    QTRY_VERIFY_WITH_TIMEOUT(first.has_value() && second.has_value(), 10000);
    QVERIFY(first.value() && second.value());
    QCOMPARE(swaps.load(), 2);
    QCOMPARE(ProjectStore::load(path).manifest.layers[0].name, QString("Second"));
}

void ProjectSaveOrderTests::canvasSizeCommittedBeforeTheSaveLands()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    QWidget window;
    window.show();
    controller->window = &window;
    session.renameLayer(session.document().value().layers[0].id, QStringLiteral("Captured"));
    holdSwap = true;
    std::optional<bool> saved;
    controller->save(false, [&](bool value) { saved = value; });
    bool resized = false;
    controller->canvasSize([&] { resized = true; });
    QTRY_VERIFY(window.findChild<QDialog *>());
    auto *width = window.findChild<QDialog *>()->findChild<PickerField *>(QStringLiteral("canvasWidth"));
    width->setFocus();
    width->selectAll();
    QTest::keyClicks(width, QStringLiteral("16"));
    QTest::keyClick(width, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(resized, 10000);
    QCOMPARE(session.document().value().width, 16);
    QVERIFY(!saved.has_value());
    holdSwap = false;
    QTRY_VERIFY_WITH_TIMEOUT(saved.has_value(), 10000);
    QVERIFY(saved.value());
    QCOMPARE(ProjectStore::load(path).manifest.width, qint64(8));
    QVERIFY(session.isModified());
    session.undo();
    QVERIFY(!session.isModified());
    QCOMPARE(session.document().value().width, 8);
}

void ProjectSaveOrderTests::imageSizeCommittedAfterTheSaveLands()
{
    QTemporaryDir root;
    const QString path = savedProject(root);
    EditorSession session;
    const auto controller = opened(session, path);
    QWidget window;
    window.show();
    controller->window = &window;
    session.renameLayer(session.document().value().layers[0].id, QStringLiteral("Captured"));
    holdSwap = true;
    std::optional<bool> saved;
    controller->save(false, [&](bool value) { saved = value; });
    bool resized = false;
    controller->imageSize([&] { resized = true; });
    QTRY_VERIFY(window.findChild<QDialog *>());
    holdSwap = false;
    // Saved with the sheet still up: the tools stay held.
    QTRY_VERIFY_WITH_TIMEOUT(saved.has_value(), 10000);
    QVERIFY(saved.value());
    QVERIFY(session.isProjectBusy());
    QVERIFY(!session.isModified());
    auto *width = window.findChild<QDialog *>()->findChild<PickerField *>(QStringLiteral("imageWidth"));
    width->setFocus();
    width->selectAll();
    QTest::keyClicks(width, QStringLiteral("16"));
    QTest::keyClick(width, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(resized, 10000);
    QCOMPARE(session.document().value().width, 16);
    QCOMPARE(ProjectStore::load(path).manifest.width, qint64(8));
    QVERIFY(session.isModified());
    session.undo();
    QVERIFY(!session.isModified());
    QCOMPARE(session.document().value().width, 8);
}

QTEST_MAIN(ProjectSaveOrderTests)
#include "ProjectSaveOrderTests.moc"
