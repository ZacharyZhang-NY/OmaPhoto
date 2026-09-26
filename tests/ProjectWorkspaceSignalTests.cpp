#include "DialogDesk.h"
#include "Document/ProjectWorkspace.h"
#include "IO/ProjectStore.h"
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtTest>

// What observers of the workspace see, and what outlives it.
namespace {
// Everything an observer can read from the workspace.
QStringList described(const ProjectWorkspace &workspace)
{
    QStringList result{workspace.isManaging() ? "managing" : "idle", workspace.canSwitch() ? "free" : "held"};
    for (const std::shared_ptr<ProjectTab> &tab : workspace.tabs())
        result << tab->title() + (tab->id == workspace.selectedID() ? "*" : "") + (tab->session.document() ? " drawn" : "");
    return result;
}

void store(const QString &path)
{
    EditorSession session;
    session.createDocument(8, 6);
    session.addBlankLayer();
    ProjectStore::save(session.projectSnapshot().value(), path);
}

QUrl picture(const QTemporaryDir &folder, const QString &name)
{
    QImage image(4, 2, QImage::Format_RGBA8888);
    image.fill(Qt::green);
    if (!image.save(folder.filePath(name + ".png")))
        throw std::runtime_error("the fixture could not be written");
    return QUrl::fromLocalFile(folder.filePath(name + ".png"));
}
}

class ProjectWorkspaceSignalTests : public QObject {
    Q_OBJECT
private slots:
    void theLastSignalOfEveryFlowSeesWhatItLeaves();
    void anEmptyDropManagesNothingForLong();
    void aWorkspaceThatEndsUnderAQuestionLeavesItHarmless();
};

void ProjectWorkspaceSignalTests::theLastSignalOfEveryFlowSeesWhatItLeaves()
{
    QTemporaryDir folder;
    store(folder.filePath("Trip.comp"));
    ProjectWorkspace workspace;
    QStringList seen;
    connect(&workspace, &ProjectWorkspace::changed, this, [&] { seen = described(workspace); });
    // Runs a flow to its end; names what went stale.
    const auto stale = [&](const std::function<void(std::function<void()>)> &flow) {
        bool done = false;
        seen.clear();
        flow([&] { done = true; });
        if (!QTest::qWaitFor([&] { return done; }, 10'000))
            return QStringLiteral("never finished");
        return seen == described(workspace) ? QString() : seen.join("; ") + " != " + described(workspace).join("; ");
    };
    DialogDesk desk;
    QCOMPARE(stale([&](auto done) { workspace.open(folder.filePath("Trip.comp"), [done](bool) { done(); }); }), QString());
    QCOMPARE(stale([&](auto done) { workspace.receive({picture(folder, "One")}, std::nullopt, std::nullopt, done); }), QString());
    const QUuid trip = workspace.tabs()[0]->id;
    const QUuid layer = workspace.tabs()[0]->session.document().value().layers.front().id;
    QCOMPARE(stale([&](auto done) { workspace.copyLayer(layer, workspace.selectedID(), std::nullopt, done); }), QString());
    desk.replies = {"Don’t Save"};
    QCOMPARE(stale([&](auto done) { workspace.close(workspace.selectedID(), done); }), QString());
    desk.replies = {"Cancel"};
    workspace.tabs()[0]->session.addBlankLayer();
    QCOMPARE(stale([&](auto done) { workspace.confirmQuit([done](bool) { done(); }); }), QString());
    QCOMPARE(described(workspace), (QStringList{"idle", "free", "Trip* drawn"}));
    QCOMPARE(workspace.selectedID(), trip);
}

void ProjectWorkspaceSignalTests::anEmptyDropManagesNothingForLong()
{
    ProjectWorkspace workspace;
    const QStringList before = described(workspace);
    bool done = false;
    workspace.receive({}, std::nullopt, std::nullopt, [&] { done = true; });
    QVERIFY(!done);
    QTRY_VERIFY(done);
    QCOMPARE(described(workspace), before);
}

void ProjectWorkspaceSignalTests::aWorkspaceThatEndsUnderAQuestionLeavesItHarmless()
{
    auto workspace = std::make_unique<ProjectWorkspace>();
    workspace->current().session.createDocument(8, 6);
    const std::weak_ptr<ProjectTab> tab = workspace->tabs()[0];
    int calls = 0;
    workspace->close(workspace->selectedID(), [&] { calls += 1; });
    // The question is up; the workspace goes; then the answer.
    QTRY_VERIFY(qobject_cast<QMessageBox *>(QApplication::activeModalWidget()) || !QApplication::topLevelWidgets().isEmpty());
    workspace.reset();
    QVERIFY(!tab.expired());
    DialogDesk desk;
    desk.replies = {"Don’t Save"};
    QTRY_COMPARE(desk.seen.size(), 1);
    // The answer frees the tab it held; nobody is called.
    QTRY_VERIFY(tab.expired());
    QCOMPARE(calls, 0);
    // A copy in flight ends with its workspace as well.
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
    auto copying = std::make_unique<ProjectWorkspace>();
    copying->current().session.createDocument(8, 6);
    copying->current().session.addBlankLayer();
    copying->copyLayer(copying->current().session.activeLayerID().value(), std::nullopt, std::nullopt, [&] { calls += 1; });
    QVERIFY(copying->isManaging());
    copying.reset();
    gate.release(workers);
    QVERIFY(pool->waitForDone(10'000));
    QTest::qWait(50);
    QCOMPARE(calls, 0);
}

QTEST_MAIN(ProjectWorkspaceSignalTests)
#include "ProjectWorkspaceSignalTests.moc"
