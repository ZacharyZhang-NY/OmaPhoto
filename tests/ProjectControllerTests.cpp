#include "DialogDesk.h"
#include "IO/ProjectController.h"
#include "IO/ProjectStore.h"
#include "SessionRecord.h"
#include <QFutureWatcher>
#include <QTemporaryDir>
#include <QtTest>

// Saving, and the question before unsaved work is dropped.
namespace {
const QString savePanel = QStringLiteral("panel|Save Project|save|file|Untitled.comp|comp");
const QString question = QStringLiteral("alert|2|Save changes to %1?|Your changes will be lost if you don’t save them.|Save,Cancel,Don’t Save");

// Eight by six with one red layer: unsaved work.
void paint(EditorSession &session)
{
    session.createDocument(8, 6);
    QImage image(2, 2, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::red);
    session.insert(ImportedImage(image, QImage(), "Red"));
}

// The answer a call hands its completion, once it comes.
std::optional<bool> answered(const std::function<void(std::function<void(bool)>)> &call)
{
    std::optional<bool> result;
    call([&](bool value) { result = value; });
    // An answer that never comes stays empty and fails.
    return QTest::qWaitFor([&] { return result.has_value(); }, 10'000) ? result : std::nullopt;
}

int layersOnDisk(const QString &path)
{
    return int(ProjectStore::load(path).manifest.layers.size());
}
}

class ProjectControllerTests : public QObject {
    Q_OBJECT
private slots:
    void theFirstSaveAsksWhereAndLaterSavesDoNot();
    void saveAsAsksAgainUnderTheCurrentName();
    void aProjectAlwaysEndsInItsSuffix();
    void aCancelledSaveLeavesEverythingAsItWas();
    void nothingToSaveOrABusySessionIsRefusedUnasked();
    void aFailedSaveSaysWhyAndStaysModified();
    void savingEndsAnOpenTransformFirst();
    void theLastSignalOfASaveSeesItDone();
    void aCompletionNeverRunsInsideTheCall();
    void aCallWithoutACompletionIsWhole();
    void quittingAsksAboutUnsavedWork();
    void aNewCanvasAsksThenClears();
    void closingAsksThenClearsAndClosesTheWindow();
    void aSessionThatEndsMidSaveCallsNobody();
};

void ProjectControllerTests::theFirstSaveAsksWhereAndLaterSavesDoNot()
{
    QTemporaryDir folder;
    EditorSession session;
    paint(session);
    ProjectController controller(session);
    QVERIFY(controller.canStart() && session.isModified());
    DialogDesk desk;
    desk.note = [&] { return session.isProjectBusy() ? QStringLiteral("busy") : QStringLiteral("free"); };
    // Typed without its suffix: the panel adds it.
    desk.replies = {folder.filePath("First")};
    QCOMPARE(answered([&](auto done) { controller.save(false, done); }), std::optional(true));
    const QString path = folder.filePath("First.comp");
    QCOMPARE(desk.seen, (QStringList{savePanel + "|busy"}));
    QCOMPARE(session.projectPath(), std::optional(path));
    QVERIFY(!session.isModified() && !session.isProjectBusy() && controller.canStart());
    QCOMPARE(layersOnDisk(path), 1);
    // The second save goes to the same place unasked.
    session.addBlankLayer();
    QVERIFY(session.isModified());
    QCOMPARE(answered([&](auto done) { controller.save(false, done); }), std::optional(true));
    QCOMPARE(desk.seen.size(), 1);
    QCOMPARE(layersOnDisk(path), 2);
    QVERIFY(!session.isModified() && !session.isProjectBusy());
    // Each save's watcher goes once it has answered.
    QTRY_VERIFY(controller.findChildren<QFutureWatcherBase *>().isEmpty());
}

void ProjectControllerTests::saveAsAsksAgainUnderTheCurrentName()
{
    QTemporaryDir folder;
    EditorSession session;
    paint(session);
    ProjectController controller(session);
    DialogDesk desk;
    desk.replies = {folder.filePath("First.comp"), folder.filePath("Second.comp")};
    QCOMPARE(answered([&](auto done) { controller.save(false, done); }), std::optional(true));
    session.addBlankLayer();
    QCOMPARE(answered([&](auto done) { controller.save(true, done); }), std::optional(true));
    QCOMPARE(desk.seen, (QStringList{savePanel, "panel|Save Project As|save|file|First.comp|comp"}));
    QCOMPARE(session.projectPath(), std::optional(folder.filePath("Second.comp")));
    QVERIFY(!session.isModified());
    // The first project stays as it was saved.
    QCOMPARE(layersOnDisk(folder.filePath("First.comp")), 1);
    QCOMPARE(layersOnDisk(folder.filePath("Second.comp")), 2);
}

void ProjectControllerTests::aProjectAlwaysEndsInItsSuffix()
{
    QTemporaryDir folder;
    EditorSession session;
    paint(session);
    ProjectController controller(session);
    DialogDesk desk;
    // A foreign suffix stays in the name; the project's follows.
    desk.replies = {folder.filePath("Misnamed.png"), folder.filePath("Shouted.COMP")};
    QCOMPARE(answered([&](auto done) { controller.save(false, done); }), std::optional(true));
    QCOMPARE(session.projectPath(), std::optional(folder.filePath("Misnamed.png.comp")));
    QVERIFY(!QFileInfo::exists(folder.filePath("Misnamed.png")));
    QCOMPARE(layersOnDisk(folder.filePath("Misnamed.png.comp")), 1);
    // The suffix counts in any case, as drops read it.
    QCOMPARE(answered([&](auto done) { controller.save(true, done); }), std::optional(true));
    QCOMPARE(session.projectPath(), std::optional(folder.filePath("Shouted.COMP")));
    QCOMPARE(layersOnDisk(folder.filePath("Shouted.COMP")), 1);
    // The name as completed meets a project: that one stays.
    session.addBlankLayer();
    desk.seen.clear();
    desk.replies = {folder.filePath("Misnamed.png"), "OK"};
    QTest::ignoreMessage(QtWarningMsg, "Couldn’t save the project: “Misnamed.png.comp” already exists. Choose another name.");
    QCOMPARE(answered([&](auto done) { controller.save(true, done); }), std::optional(false));
    QCOMPARE(desk.seen.value(1), QString("alert|2|Couldn’t save the project|“Misnamed.png.comp” already exists. Choose another name.|OK"));
    QCOMPARE(layersOnDisk(folder.filePath("Misnamed.png.comp")), 1);
    QCOMPARE(session.projectPath(), std::optional(folder.filePath("Shouted.COMP")));
    QVERIFY(session.isModified() && !session.isProjectBusy());
    // A link that leads nowhere is in the way too.
    QVERIFY(QFile::link(folder.filePath("nowhere"), folder.filePath("Dangling.png.comp")));
    desk.replies = {folder.filePath("Dangling.png"), "OK"};
    QTest::ignoreMessage(QtWarningMsg, "Couldn’t save the project: “Dangling.png.comp” already exists. Choose another name.");
    QCOMPARE(answered([&](auto done) { controller.save(true, done); }), std::optional(false));
    QVERIFY(QFileInfo(folder.filePath("Dangling.png.comp")).isSymLink());
}

void ProjectControllerTests::aCancelledSaveLeavesEverythingAsItWas()
{
    EditorSession session;
    paint(session);
    ProjectController controller(session);
    const QStringList before = described(session);
    DialogDesk desk;
    desk.replies = {"<cancel>"};
    QCOMPARE(answered([&](auto done) { controller.save(false, done); }), std::optional(false));
    QCOMPARE(desk.seen, (QStringList{savePanel}));
    QCOMPARE(described(session), before);
    QVERIFY(session.isModified() && !session.isProjectBusy());
}

void ProjectControllerTests::nothingToSaveOrABusySessionIsRefusedUnasked()
{
    EditorSession session;
    ProjectController controller(session);
    DialogDesk desk;
    QSignalSpy changes(&session, &EditorSession::changed);
    // No canvas: nothing to save, and nothing turns busy.
    QCOMPARE(answered([&](auto done) { controller.save(false, done); }), std::optional(false));
    QCOMPARE(changes.count(), 0);
    paint(session);
    session.setIsProjectBusy(true);
    QVERIFY(!controller.canStart());
    // Once dimmed, the record holds however long the rest takes.
    QTRY_VERIFY(session.showsBusy());
    const QStringList before = described(session);
    QCOMPARE(answered([&](auto done) { controller.save(false, done); }), std::optional(false));
    QCOMPARE(answered([&](auto done) { controller.confirmQuit(done); }), std::optional(false));
    QTemporaryDir folder;
    ProjectStore::save(session.projectSnapshot().value(), folder.filePath("Ready.comp"));
    QCOMPARE(answered([&](auto done) { controller.open(folder.filePath("Ready.comp"), done); }), std::optional(false));
    controller.newCanvas();
    QWidget window;
    window.show();
    controller.close(&window);
    QTest::qWait(50);
    QCOMPARE(described(session), before);
    QVERIFY(window.isVisible() && desk.seen.isEmpty());
}

void ProjectControllerTests::aFailedSaveSaysWhyAndStaysModified()
{
    QTemporaryDir folder;
    QFile plain(folder.filePath("plain.txt"));
    QVERIFY(plain.open(QIODevice::WriteOnly));
    plain.close();
    EditorSession session;
    paint(session);
    ProjectController controller(session);
    // No folder can be made under a plain file.
    const QString path = folder.filePath("plain.txt/Lost.comp");
    session.setProjectPath(path);
    DialogDesk desk;
    desk.replies = {"OK"};
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^could not create .+"));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^Couldn’t save the project: .+"));
    QCOMPARE(answered([&](auto done) { controller.save(false, done); }), std::optional(false));
    QCOMPARE(desk.seen.size(), 1);
    const QStringList alert = desk.seen.first().split('|');
    QCOMPARE(alert.mid(0, 3), (QStringList{"alert", "2", "Couldn’t save the project"}));
    QVERIFY(!alert.value(3).isEmpty());
    QCOMPARE(alert.value(4), QString("OK"));
    QVERIFY(session.isModified() && !session.isProjectBusy());
    QCOMPARE(session.projectPath(), std::optional(path));
}

void ProjectControllerTests::savingEndsAnOpenTransformFirst()
{
    QTemporaryDir folder;
    EditorSession session;
    paint(session);
    ProjectController controller(session);
    session.beginTransform();
    LayerTransform moved = session.transformEdit().value().draft;
    moved.origin += QPointF(2, 1);
    session.previewTransform(moved);
    DialogDesk desk;
    desk.replies = {folder.filePath("Moved.comp")};
    QCOMPARE(answered([&](auto done) { controller.save(false, done); }), std::optional(true));
    QVERIFY(!session.transformEdit().has_value());
    QCOMPARE(ProjectStore::load(folder.filePath("Moved.comp")).manifest.layers.front().transform.origin, moved.origin);
    QVERIFY(!session.isModified());
}

void ProjectControllerTests::theLastSignalOfASaveSeesItDone()
{
    QTemporaryDir folder;
    EditorSession session;
    paint(session);
    ProjectController controller(session);
    QStringList seen;
    connect(&session, &EditorSession::changed, this, [&] { seen = described(session); });
    DialogDesk desk;
    desk.replies = {folder.filePath("Seen.comp")};
    QCOMPARE(answered([&](auto done) { controller.save(false, done); }), std::optional(true));
    QCOMPARE(seen, described(session));
    QVERIFY(seen.contains("path " + folder.filePath("Seen.comp")));
}

void ProjectControllerTests::aCompletionNeverRunsInsideTheCall()
{
    EditorSession session;
    ProjectController controller(session);
    // Even a refusal answers from the event loop.
    int calls = 0;
    controller.save(false, [&](bool) { calls += 1; });
    controller.confirmQuit([&](bool) { calls += 1; });
    DialogDesk desk;
    desk.replies = {"OK"};
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^Couldn’t open the project: .+"));
    controller.open(QString("/nowhere.comp"), [&](bool) { calls += 1; });
    QCOMPARE(calls, 0);
    QTRY_COMPARE(calls, 3);
}

void ProjectControllerTests::aCallWithoutACompletionIsWhole()
{
    QTemporaryDir folder;
    EditorSession session;
    ProjectController controller(session);
    // Refused, with nobody to tell: the posted answer is nothing.
    controller.save();
    controller.confirmQuit({});
    QTest::qWait(20);
    paint(session);
    DialogDesk desk;
    desk.replies = {folder.filePath("Quiet.comp")};
    controller.save();
    QTRY_VERIFY(!session.isModified() && !session.isProjectBusy());
    QCOMPARE(layersOnDisk(folder.filePath("Quiet.comp")), 1);
    EditorSession other;
    ProjectController opener(other);
    opener.open(folder.filePath("Quiet.comp"));
    QTRY_VERIFY(other.document().has_value() && !other.isProjectBusy());
    QTest::qWait(20);
    QCOMPARE(other.projectPath(), std::optional(folder.filePath("Quiet.comp")));
}

void ProjectControllerTests::quittingAsksAboutUnsavedWork()
{
    QTemporaryDir folder;
    EditorSession session;
    ProjectController controller(session);
    DialogDesk desk;
    // Nothing unsaved: no question.
    QCOMPARE(answered([&](auto done) { controller.confirmQuit(done); }), std::optional(true));
    // A saved canvas undone: changed, yet nothing to save.
    session.createDocument(8, 6);
    session.history.markSaved();
    session.undo();
    QVERIFY(session.isModified() && !session.document().has_value());
    QCOMPARE(answered([&](auto done) { controller.confirmQuit(done); }), std::optional(true));
    QVERIFY(desk.seen.isEmpty());
    paint(session);
    desk.note = [&] { return session.isProjectBusy() ? QStringLiteral("busy") : QStringLiteral("free"); };
    desk.replies = {"Cancel", "Don’t Save", "Save", folder.filePath("Kept.comp")};
    QCOMPARE(answered([&](auto done) { controller.confirmQuit(done); }), std::optional(false));
    QVERIFY(!session.isProjectBusy());
    QCOMPARE(answered([&](auto done) { controller.confirmQuit(done); }), std::optional(true));
    QVERIFY(session.isModified() && !session.isProjectBusy());
    // Save asks where, since this work has no place yet.
    QCOMPARE(answered([&](auto done) { controller.confirmQuit(done); }), std::optional(true));
    QCOMPARE(desk.seen, (QStringList{question.arg("Untitled") + "|busy", question.arg("Untitled") + "|busy",
                                     question.arg("Untitled") + "|busy", savePanel + "|busy"}));
    QVERIFY(!session.isModified() && !session.isProjectBusy());
    QCOMPARE(layersOnDisk(folder.filePath("Kept.comp")), 1);
    // A saved project is asked about by its name.
    session.addBlankLayer();
    desk.replies = {"Save"};
    QCOMPARE(answered([&](auto done) { controller.confirmQuit(done); }), std::optional(true));
    QCOMPARE(desk.seen.last(), question.arg("Kept.comp") + "|busy");
    QCOMPARE(layersOnDisk(folder.filePath("Kept.comp")), 2);
    // A save that is called off calls the quit off.
    session.addBlankLayer();
    session.setProjectPath(std::nullopt);
    desk.replies = {"Save", "<cancel>"};
    QCOMPARE(answered([&](auto done) { controller.confirmQuit(done); }), std::optional(false));
    QVERIFY(session.isModified() && !session.isProjectBusy());
    // Escape means Cancel; Return means Save.
    desk.replies = {"<escape>", "<return>", "<cancel>"};
    const qsizetype asked = desk.seen.size();
    QCOMPARE(answered([&](auto done) { controller.confirmQuit(done); }), std::optional(false));
    QCOMPARE(desk.seen.size(), asked + 1);
    QCOMPARE(answered([&](auto done) { controller.confirmQuit(done); }), std::optional(false));
    QCOMPARE(desk.seen.size(), asked + 3);
    QVERIFY(desk.seen.last().startsWith("panel|Save Project|"));
}

void ProjectControllerTests::aNewCanvasAsksThenClears()
{
    EditorSession session;
    paint(session);
    ProjectController controller(session);
    DialogDesk desk;
    desk.replies = {"Cancel", "Don’t Save"};
    controller.newCanvas();
    QTRY_COMPARE(desk.seen.size(), 1);
    QTRY_VERIFY(!session.isProjectBusy());
    QVERIFY(session.document().has_value());
    controller.newCanvas();
    QTRY_VERIFY(!session.document().has_value());
    QVERIFY(!session.isProjectBusy() && !session.isModified());
    QCOMPARE(desk.seen.size(), 2);
}

void ProjectControllerTests::closingAsksThenClearsAndClosesTheWindow()
{
    EditorSession session;
    paint(session);
    ProjectController controller(session);
    QWidget window;
    window.show();
    DialogDesk desk;
    desk.replies = {"Cancel", "Don’t Save"};
    controller.close(&window);
    QTRY_COMPARE(desk.seen.size(), 1);
    QTRY_VERIFY(!session.isProjectBusy());
    QVERIFY(window.isVisible() && session.document().has_value());
    controller.close(&window);
    QTRY_VERIFY(!window.isVisible());
    QVERIFY(!session.document().has_value() && !session.isProjectBusy());
    // A window that is gone by then is left alone.
    paint(session);
    auto gone = std::make_unique<QWidget>();
    desk.replies = {"Don’t Save"};
    controller.close(gone.get());
    gone.reset();
    QTRY_VERIFY(!session.document().has_value());
}

void ProjectControllerTests::aSessionThatEndsMidSaveCallsNobody()
{
    QTemporaryDir folder;
    auto session = std::make_unique<EditorSession>();
    paint(*session);
    session->setProjectPath(folder.filePath("Orphan.comp"));
    auto controller = std::make_unique<ProjectController>(*session);
    int calls = 0;
    controller->save(false, [&](bool) { calls += 1; });
    controller.reset();
    session.reset();
    // The worker still ends; its answer has nowhere to go.
    QVERIFY(QThreadPool::globalInstance()->waitForDone(10'000));
    QTest::qWait(50);
    QCOMPARE(calls, 0);
}

QTEST_MAIN(ProjectControllerTests)
#include "ProjectControllerTests.moc"
