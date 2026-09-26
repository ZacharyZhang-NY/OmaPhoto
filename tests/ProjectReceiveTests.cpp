#include "DialogDesk.h"
#include "IO/ProjectController.h"
#include "IO/ProjectStore.h"
#include "SessionRecord.h"
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtTest>

// Files dropped on a project: a queue of requests.
namespace {
const QString question = QStringLiteral("alert|2|Save changes to %1?|Your changes will be lost if you don’t save them.|Save,Cancel,Don’t Save");

// A four by two picture file of that name.
QUrl picture(const QTemporaryDir &folder, const QString &name)
{
    QImage image(4, 2, QImage::Format_RGBA8888);
    image.fill(Qt::green);
    const QString path = folder.filePath(name + ".png");
    if (!image.save(path))
        throw std::runtime_error("the fixture could not be written");
    return QUrl::fromLocalFile(path);
}

// A project of one blank layer on sixteen by twelve.
QUrl project(const QTemporaryDir &folder, const QString &name)
{
    EditorSession session;
    session.createDocument(16, 12);
    session.addBlankLayer();
    ProjectStore::save(session.projectSnapshot().value(), folder.filePath(name));
    return QUrl::fromLocalFile(folder.filePath(name));
}

QStringList names(const EditorSession &session)
{
    QStringList result;
    for (const ImageLayer &layer : session.document().value().layers)
        result << layer.name;
    return result;
}

QPointF centre(const EditorSession &session, const QString &name)
{
    for (const ImageLayer &layer : session.document().value().layers) {
        if (layer.name == name)
            return layer.transform.center();
    }
    throw std::runtime_error("no such layer");
}
}

class ProjectReceiveTests : public QObject {
    Q_OBJECT
private slots:
    void picturesGoInAtThePoint();
    void aProjectOpensFirstAndItsPicturesGoToTheCentre();
    void aProjectIsKnownByItsSuffixInAnyCase();
    void twoProjectsAtOnceAreRefused();
    void aProjectThatDoesNotOpenKeepsItsPicturesOut();
    void requestsWaitForTheGateAndKeepTheirOrder();
    void anEmptyDropAnswersFromTheEventLoop();
    void aControllerThatEndsLeavesTheSessionSafe();
};

void ProjectReceiveTests::picturesGoInAtThePoint()
{
    QTemporaryDir folder;
    EditorSession session;
    session.createDocument(16, 12);
    ProjectController controller(session);
    DialogDesk desk;
    bool done = false;
    controller.receive({picture(folder, "One"), picture(folder, "Two")}, QPointF(10, 5), [&] { done = true; });
    // Nothing runs inside the call, not even the import's start.
    QVERIFY(!done && !session.isImporting() && session.document().value().layers.empty());
    QTRY_VERIFY(done);
    QCOMPARE(names(session), (QStringList{"One", "Two"}));
    QCOMPARE(centre(session, "One"), QPointF(10, 5));
    QCOMPARE(centre(session, "Two"), QPointF(10, 5));
    QVERIFY(desk.seen.isEmpty() && !session.isImporting());
    // No callback is fine too.
    controller.receive({picture(folder, "Three")});
    QTRY_COMPARE(names(session).size(), 3);
    QCOMPARE(centre(session, "Three"), QPointF(8, 6));
}

void ProjectReceiveTests::aProjectOpensFirstAndItsPicturesGoToTheCentre()
{
    QTemporaryDir folder;
    EditorSession session;
    ProjectController controller(session);
    DialogDesk desk;
    bool done = false;
    controller.receive({picture(folder, "Before"), project(folder, "Base.comp"), picture(folder, "After")}, QPointF(1, 1), [&] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(session.projectPath(), std::optional(folder.filePath("Base.comp")));
    QCOMPARE(names(session), (QStringList{"Layer 1", "Before", "After"}));
    // The point belonged to another canvas: the centre it is.
    QCOMPARE(centre(session, "Before"), QPointF(8, 6));
    QCOMPARE(centre(session, "After"), QPointF(8, 6));
    QVERIFY(desk.seen.isEmpty() && !session.isProjectBusy());
    // A project alone, over the unsaved pictures: asked, then opened.
    done = false;
    desk.replies = {"Don’t Save"};
    controller.receive({project(folder, "Alone.comp")}, std::nullopt, [&] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(desk.seen, (QStringList{question.arg("Base.comp")}));
    QCOMPARE(session.projectPath(), std::optional(folder.filePath("Alone.comp")));
    QCOMPARE(names(session), (QStringList{"Layer 1"}));
}

void ProjectReceiveTests::aProjectIsKnownByItsSuffixInAnyCase()
{
    QTemporaryDir folder;
    EditorSession session;
    ProjectController controller(session);
    DialogDesk desk;
    bool done = false;
    // Upper case, and a folder's address ending in a slash.
    const QUrl shouted = project(folder, "Shouted.COMP");
    controller.receive({QUrl(shouted.toString() + "/")}, std::nullopt, [&] { done = true; });
    QTRY_VERIFY(done);
    QVERIFY(session.document().has_value());
    QCOMPARE(session.projectPath(), std::optional(folder.filePath("Shouted.COMP")));
    QCOMPARE(names(session), (QStringList{"Layer 1"}));
    QVERIFY(desk.seen.isEmpty());
}

void ProjectReceiveTests::twoProjectsAtOnceAreRefused()
{
    QTemporaryDir folder;
    EditorSession session;
    session.createDocument(16, 12);
    ProjectController controller(session);
    const QStringList before = described(session);
    DialogDesk desk;
    desk.replies = {"OK"};
    bool done = false;
    QTest::ignoreMessage(QtWarningMsg, "Open one project at a time: This is not a valid OmaPhoto project, or its metadata is damaged.");
    controller.receive({project(folder, "A.comp"), picture(folder, "Picture"), project(folder, "B.comp")}, std::nullopt, [&] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(desk.seen, (QStringList{"alert|2|Open one project at a time|This is not a valid OmaPhoto project, or its metadata is damaged.|OK"}));
    QCOMPARE(described(session), before);
}

void ProjectReceiveTests::aProjectThatDoesNotOpenKeepsItsPicturesOut()
{
    QTemporaryDir folder;
    EditorSession session;
    session.createDocument(16, 12);
    session.addBlankLayer();
    ProjectController controller(session);
    const QStringList before = described(session);
    DialogDesk desk;
    desk.replies = {"Cancel", "OK"};
    bool done = false;
    controller.receive({project(folder, "Refused.comp"), picture(folder, "Picture")}, std::nullopt, [&] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(desk.seen, (QStringList{question.arg("Untitled")}));
    QCOMPARE(described(session), before);
    // An address that is no local file fails to open.
    done = false;
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^Couldn’t open the project: .+"));
    controller.receive({QUrl("https://example.com/Far.comp"), picture(folder, "Other")}, std::nullopt, [&] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(desk.seen.size(), 2);
    QCOMPARE(described(session), before);
}

void ProjectReceiveTests::requestsWaitForTheGateAndKeepTheirOrder()
{
    QTemporaryDir folder;
    EditorSession session;
    session.createDocument(16, 12);
    ProjectController controller(session);
    // A sheet is up: file requests wait.
    session.setShowsNewDocument(true);
    QStringList order;
    // A caller resumes once the queue has moved on.
    controller.receive({picture(folder, "First")}, std::nullopt, [&] { order << (session.isImporting() ? "first" : "first, queue idle"); });
    controller.receive({picture(folder, "Second")}, std::nullopt, [&] { order << (session.isImporting() ? "second, queue busy" : "second"); });
    QTest::qWait(100);
    QVERIFY(order.isEmpty() && session.document().value().layers.empty());
    session.setShowsNewDocument(false);
    QTRY_COMPARE(order, (QStringList{"first", "second"}));
    QCOMPARE(names(session), (QStringList{"First", "Second"}));
    // The queue starts again for a later request.
    controller.receive({picture(folder, "Third")}, std::nullopt, [&] { order << "third"; });
    QTRY_COMPARE(order.size(), 3);
    QCOMPARE(names(session), (QStringList{"First", "Second", "Third"}));
    // A project's own pictures go in before the next request's.
    DialogDesk desk;
    desk.replies = {"Don’t Save"};
    controller.receive({project(folder, "Base.comp"), picture(folder, "Own")}, std::nullopt, [&] { order << "fourth"; });
    controller.receive({picture(folder, "Next")}, std::nullopt, [&] { order << "fifth"; });
    QTRY_COMPARE(order.size(), 5);
    QCOMPARE(order.mid(3), (QStringList{"fourth", "fifth"}));
    QCOMPARE(names(session), (QStringList{"Layer 1", "Own", "Next"}));
}

void ProjectReceiveTests::anEmptyDropAnswersFromTheEventLoop()
{
    EditorSession session;
    ProjectController controller(session);
    QSignalSpy changes(&session, &EditorSession::changed);
    bool done = false;
    controller.receive({}, std::nullopt, [&] { done = true; });
    QVERIFY(!done);
    QTRY_VERIFY(done);
    controller.receive({});
    QTest::qWait(20);
    QCOMPARE(changes.count(), 0);
    // It never queues: a closed gate does not hold it.
    session.setShowsNewDocument(true);
    done = false;
    controller.receive({}, std::nullopt, [&] { done = true; });
    QTRY_VERIFY(done);
}

void ProjectReceiveTests::aControllerThatEndsLeavesTheSessionSafe()
{
    QTemporaryDir folder;
    EditorSession session;
    session.createDocument(16, 12);
    auto controller = std::make_unique<ProjectController>(session);
    // Behind a sheet the session itself holds the waiting request.
    session.setShowsNewDocument(true);
    bool done = false;
    controller->receive({picture(folder, "Waiting")}, std::nullopt, [&] { done = true; });
    QTest::qWait(50);
    controller.reset();
    session.setShowsNewDocument(false);
    QTest::qWait(200);
    QVERIFY(!done && !session.isImporting() && session.document().value().layers.empty());
    // Mid-import: every worker is held, so the decode waits.
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
    controller = std::make_unique<ProjectController>(session);
    controller->receive({picture(folder, "Landing")}, std::nullopt, [&] { done = true; });
    QTRY_VERIFY(session.isImporting());
    controller.reset();
    gate.release(workers);
    // The picture still lands; its request has nobody to tell.
    QTRY_COMPARE(names(session), (QStringList{"Landing"}));
    QTRY_VERIFY(!session.isImporting());
    QTest::qWait(50);
    QVERIFY(!done);
}

QTEST_MAIN(ProjectReceiveTests)
#include "ProjectReceiveTests.moc"
