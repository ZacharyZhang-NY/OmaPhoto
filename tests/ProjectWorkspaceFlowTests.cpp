#include "DialogDesk.h"
#include "Document/ProjectWorkspace.h"
#include "IO/ProjectStore.h"
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtTest>

// The workspace's flows that open dialogs or take time.
namespace {
const QString question = QStringLiteral("alert|2|Save changes to %1?|Your changes will be lost if you don’t save them.|Save,Cancel,Don’t Save");

// A project of `layers` blank layers saved at `path`.
void store(const QString &path, int layers)
{
    EditorSession session;
    session.createDocument(8, 6);
    for (int index = 0; index < layers; ++index)
        session.addBlankLayer();
    ProjectStore::save(session.projectSnapshot().value(), path);
}

// Unsaved work in that tab.
void paint(ProjectTab &tab)
{
    tab.session.createDocument(8, 6);
    tab.session.addBlankLayer();
}

QUrl picture(const QTemporaryDir &folder, const QString &name)
{
    QImage image(4, 2, QImage::Format_RGBA8888);
    image.fill(Qt::green);
    if (!image.save(folder.filePath(name + ".png")))
        throw std::runtime_error("the fixture could not be written");
    return QUrl::fromLocalFile(folder.filePath(name + ".png"));
}

QStringList titles(const ProjectWorkspace &workspace)
{
    QStringList result;
    for (const std::shared_ptr<ProjectTab> &tab : workspace.tabs())
        result << tab->title() + (tab->id == workspace.selectedID() ? "*" : "");
    return result;
}

std::optional<bool> answered(const std::function<void(std::function<void(bool)>)> &call)
{
    std::optional<bool> result;
    call([&](bool value) { result = value; });
    // An answer that never comes stays empty and fails.
    return QTest::qWaitFor([&] { return result.has_value(); }, 10'000) ? result : std::nullopt;
}
}

class ProjectWorkspaceFlowTests : public QObject {
    Q_OBJECT
private slots:
    void aProjectOpensInATabOfItsOwn();
    void aProjectThatIsOpenIsBroughtToTheFront();
    void aProjectThatFailsToOpenLeavesNoTab();
    void closingATabAsksAboutItsWork();
    void quittingAsksTabByTabAndStopsAtCancel();
    void closingTheWindowLeavesOneFreshTab();
    void dropsWaitUntilTheWorkspaceIsFree();
    void aControllerInATabHandsOverToTheWorkspace();
    void aWorkspaceThatEndsMidwayCallsNobody();
    void aTabRemovedMidImportStillEndsTheDrop();
    void aProjectThatMovedAwayIsNoMatchForAnotherMissingOne();
    void everyCallIsWholeWithoutACompletion();
};

void ProjectWorkspaceFlowTests::aProjectOpensInATabOfItsOwn()
{
    QTemporaryDir folder;
    store(folder.filePath("Trip.comp"), 2);
    store(folder.filePath("Second.comp"), 1);
    ProjectWorkspace workspace;
    QWidget window;
    workspace.window = &window;
    DialogDesk desk;
    bool managedMeanwhile = false;
    connect(&workspace, &ProjectWorkspace::changed, this, [&] { managedMeanwhile = managedMeanwhile || workspace.isManaging(); });
    // The single empty tab makes way for the project.
    const QUuid empty = workspace.current().id;
    QCOMPARE(answered([&](auto done) { workspace.open(folder.filePath("Trip.comp"), done); }), std::optional(true));
    QCOMPARE(titles(workspace), (QStringList{"Trip*"}));
    QVERIFY(workspace.current().id != empty && managedMeanwhile && !workspace.isManaging());
    QCOMPARE(int(workspace.current().session.document().value().layers.size()), 2);
    QVERIFY(workspace.current().controller.workspace == &workspace && workspace.current().controller.window == &window);
    // Chosen in a panel, a second project gets its tab.
    desk.note = [&] { return QString(workspace.isManaging() ? "managing" : "idle") + (workspace.canSwitch() ? ", free" : ", held"); };
    desk.replies = {"<cancel>", folder.filePath("Second.comp")};
    QCOMPARE(answered([&](auto done) { workspace.open(std::nullopt, done); }), std::optional(false));
    QCOMPARE(titles(workspace), (QStringList{"Trip*"}));
    QVERIFY(!workspace.isManaging());
    QCOMPARE(answered([&](auto done) { workspace.open(std::nullopt, done); }), std::optional(true));
    QCOMPARE(desk.seen, (QStringList{"panel|Open Project|open|folder|||managing, held", "panel|Open Project|open|folder|||managing, held"}));
    QCOMPARE(titles(workspace), (QStringList{"Trip", "Second*"}));
    // An empty tab in front of others is no placeholder.
    workspace.newCanvas();
    store(folder.filePath("Third.comp"), 1);
    QCOMPARE(answered([&](auto done) { workspace.open(folder.filePath("Third.comp"), done); }), std::optional(true));
    QCOMPARE(titles(workspace), (QStringList{"Trip", "Second", "Untitled 2", "Third*"}));
    // While it manages, nothing else begins.
    workspace.current().session.setIsProjectBusy(true);
    QCOMPARE(answered([&](auto done) { workspace.open(folder.filePath("Trip.comp"), done); }), std::optional(false));
    QCOMPARE(titles(workspace), (QStringList{"Trip", "Second", "Untitled 2", "Third*"}));
}

void ProjectWorkspaceFlowTests::aProjectThatIsOpenIsBroughtToTheFront()
{
    QTemporaryDir folder;
    store(folder.filePath("Trip.comp"), 2);
    QVERIFY(QFile::link(folder.filePath("Trip.comp"), folder.filePath("Link.comp")));
    ProjectWorkspace workspace;
    QCOMPARE(answered([&](auto done) { workspace.open(folder.filePath("Trip.comp"), done); }), std::optional(true));
    workspace.newCanvas();
    QCOMPARE(titles(workspace), (QStringList{"Trip", "Untitled 2*"}));
    // By a link, with a slash: still the open project.
    QCOMPARE(answered([&](auto done) { workspace.open(folder.filePath("Link.comp") + "/", done); }), std::optional(true));
    QCOMPARE(titles(workspace), (QStringList{"Trip*", "Untitled 2"}));
    QVERIFY(!workspace.isManaging());
}

void ProjectWorkspaceFlowTests::aProjectThatFailsToOpenLeavesNoTab()
{
    QTemporaryDir folder;
    ProjectWorkspace workspace;
    const QUuid empty = workspace.current().id;
    DialogDesk desk;
    desk.replies = {"OK"};
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^Couldn’t open the project: .+"));
    QCOMPARE(answered([&](auto done) { workspace.open(folder.filePath("Nowhere.comp"), done); }), std::optional(false));
    QCOMPARE(desk.seen.size(), 1);
    QCOMPARE(titles(workspace), (QStringList{"Untitled*"}));
    QVERIFY(workspace.current().id == empty && !workspace.isManaging());
}

void ProjectWorkspaceFlowTests::closingATabAsksAboutItsWork()
{
    ProjectWorkspace workspace;
    QWidget window;
    workspace.window = &window;
    paint(workspace.current());
    const QUuid painted = workspace.current().id;
    const QUuid blank = workspace.addTab().id;
    DialogDesk desk;
    desk.note = [&] { return workspace.isManaging() ? QStringLiteral("managing") : QStringLiteral("idle"); };
    desk.replies = {"Cancel", "Don’t Save"};
    bool done = false;
    // A tab behind the front can be closed too.
    workspace.close(painted, [&] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(desk.seen, (QStringList{question.arg("Untitled") + "|managing"}));
    QCOMPARE(titles(workspace), (QStringList{"Untitled", "Untitled 2*"}));
    QVERIFY(!workspace.isManaging() && workspace.tabs()[0]->controller.window == &window);
    done = false;
    workspace.close(painted, [&] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(titles(workspace), (QStringList{"Untitled 2*"}));
    // Nothing unsaved: no question. A stranger: nothing at all.
    done = false;
    workspace.close(QUuid::createUuid(), [&] { done = true; });
    QTRY_VERIFY(done);
    // A busy project in front holds every tab.
    const QUuid extra = workspace.addTab(false).id;
    workspace.current().session.setIsProjectBusy(true);
    done = false;
    workspace.close(blank, [&] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(int(workspace.tabs().size()), 2);
    workspace.current().session.setIsProjectBusy(false);
    workspace.close(extra);
    QTRY_COMPARE(int(workspace.tabs().size()), 1);
    workspace.close(blank);
    QTRY_COMPARE(titles(workspace), (QStringList{"Untitled 4*"}));
    QCOMPARE(desk.seen.size(), 2);
    QVERIFY(!workspace.isManaging());
}

void ProjectWorkspaceFlowTests::quittingAsksTabByTabAndStopsAtCancel()
{
    ProjectWorkspace workspace;
    paint(workspace.current());
    const QUuid first = workspace.current().id;
    paint(workspace.addTab());
    const QUuid second = workspace.selectedID();
    paint(workspace.addTab());
    workspace.select(second);
    // The window arrives late: each asked controller still gets it.
    QWidget window;
    workspace.window = &window;
    QUuid announced;
    connect(&workspace, &ProjectWorkspace::changed, this, [&] { announced = workspace.selectedID(); });
    DialogDesk desk;
    // Each tab asked about comes forward and says so.
    desk.note = [&] {
        return workspace.current().title() + (workspace.isManaging() ? " managing" : " idle")
            + (announced == workspace.selectedID() && workspace.current().controller.window == &window ? "" : " stale");
    };
    desk.replies = {"Don’t Save", "Cancel"};
    QCOMPARE(answered([&](auto done) { workspace.confirmQuit(done); }), std::optional(false));
    QCOMPARE(desk.seen, (QStringList{question.arg("Untitled") + "|Untitled 2 managing", question.arg("Untitled") + "|Untitled managing"}));
    QCOMPARE(workspace.selectedID(), first);
    QVERIFY(!workspace.isManaging());
    desk.replies = {"Don’t Save", "Don’t Save", "Don’t Save"};
    QCOMPARE(answered([&](auto done) { workspace.confirmQuit(done); }), std::optional(true));
    QCOMPARE(desk.seen.size(), 5);
    QCOMPARE(desk.seen.mid(2).join(";").count("managing"), 3);
    QVERIFY(!workspace.isManaging());
    // A busy project in front refuses the quit outright.
    workspace.current().session.setIsProjectBusy(true);
    QCOMPARE(answered([&](auto done) { workspace.confirmQuit(done); }), std::optional(false));
    QCOMPARE(desk.seen.size(), 5);
}

void ProjectWorkspaceFlowTests::closingTheWindowLeavesOneFreshTab()
{
    ProjectWorkspace workspace;
    paint(workspace.current());
    workspace.addTab();
    QWidget window;
    window.show();
    DialogDesk desk;
    desk.replies = {"Cancel", "Don’t Save"};
    workspace.closeWindow(&window);
    QTRY_COMPARE(desk.seen.size(), 1);
    QTRY_VERIFY(!workspace.isManaging());
    QVERIFY(window.isVisible());
    QCOMPARE(int(workspace.tabs().size()), 2);
    workspace.closeWindow(&window);
    QTRY_VERIFY(!window.isVisible());
    QCOMPARE(titles(workspace), (QStringList{"Untitled 3*"}));
    QVERIFY(!workspace.current().session.document().has_value());
}

void ProjectWorkspaceFlowTests::dropsWaitUntilTheWorkspaceIsFree()
{
    QTemporaryDir folder;
    store(folder.filePath("Dropped.comp"), 1);
    ProjectWorkspace workspace;
    workspace.current().session.setIsProjectBusy(true);
    bool done = false;
    // Picture, project, picture: the project gets its own tab.
    workspace.receive({picture(folder, "One"), QUrl::fromLocalFile(folder.filePath("Dropped.comp")), picture(folder, "Two")},
                      std::nullopt, QPointF(3, 1), [&] { done = true; });
    QTest::qWait(120);
    QVERIFY(!done && !workspace.isManaging());
    QCOMPARE(titles(workspace), (QStringList{"Untitled*"}));
    workspace.current().session.setIsProjectBusy(false);
    QTRY_VERIFY(done);
    QCOMPARE(titles(workspace), (QStringList{"Untitled", "Dropped", "Untitled 2*"}));
    QVERIFY(!workspace.isManaging());
    const ImageLayer one = workspace.tabs()[0]->session.document().value().layers.front();
    QCOMPARE(one.name, QString("One"));
    // The first picture made its canvas: the point is void.
    QCOMPARE(one.transform.center(), QPointF(2, 1));
    QCOMPARE(workspace.tabs()[2]->session.document().value().layers.front().name, QString("Two"));
    // A tab that is gone takes nothing.
    done = false;
    workspace.receive({picture(folder, "Lost")}, QUuid::createUuid(), std::nullopt, [&] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(int(workspace.tabs().size()), 3);
    QVERIFY(!workspace.isManaging());
    // Into a named tab, at the point; it comes forward.
    done = false;
    const std::shared_ptr<ProjectTab> first = workspace.tabs()[0];
    QUuid announced;
    bool staleAtImport = false;
    connect(&workspace, &ProjectWorkspace::changed, this, [&] { announced = workspace.selectedID(); });
    connect(&first->session, &EditorSession::changed, this, [&] { staleAtImport = staleAtImport || (first->session.isImporting() && announced != first->id); });
    workspace.receive({picture(folder, "Placed")}, first->id, QPointF(3, 1), [&] { done = true; });
    QTRY_VERIFY(done);
    QVERIFY(!staleAtImport);
    QCOMPARE(workspace.selectedID(), first->id);
    QCOMPARE(first->session.document().value().layers.back().transform.center(), QPointF(3, 1));
}

void ProjectWorkspaceFlowTests::aControllerInATabHandsOverToTheWorkspace()
{
    QTemporaryDir folder;
    store(folder.filePath("Trip.comp"), 2);
    ProjectWorkspace workspace;
    paint(workspace.current());
    ProjectController &controller = workspace.current().controller;
    // Opening never replaces this tab's work: a new tab, unasked.
    DialogDesk desk;
    QCOMPARE(answered([&](auto done) { controller.open(folder.filePath("Trip.comp"), done); }), std::optional(true));
    QCOMPARE(titles(workspace), (QStringList{"Untitled", "Trip*"}));
    QVERIFY(desk.seen.isEmpty());
    controller.newCanvas();
    QCOMPARE(titles(workspace), (QStringList{"Untitled", "Trip", "Untitled 2*"}));
    // Its drops land in its own tab, wherever that is.
    bool done = false;
    controller.receive({picture(folder, "Mine")}, QPointF(3, 1), [&] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(titles(workspace), (QStringList{"Untitled*", "Trip", "Untitled 2"}));
    QCOMPARE(workspace.tabs()[0]->session.document().value().layers.back().name, QString("Mine"));
    QCOMPARE(workspace.tabs()[0]->session.document().value().layers.back().transform.center(), QPointF(3, 1));
    // So do those of a later tab.
    done = false;
    workspace.tabs()[2]->controller.receive({picture(folder, "Theirs")}, std::nullopt, [&] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(titles(workspace), (QStringList{"Untitled", "Trip", "Untitled 2*"}));
    QCOMPARE(workspace.tabs()[2]->session.document().value().layers.back().name, QString("Theirs"));
    workspace.select(workspace.tabs()[0]->id);
    // Closing its window closes the tab, not the window.
    QWidget window;
    window.show();
    desk.replies = {"Don’t Save"};
    controller.close(&window);
    QTRY_COMPARE(titles(workspace), (QStringList{"Trip*", "Untitled 2"}));
    QVERIFY(window.isVisible());
    // While the workspace manages, no tab's controller may start.
    ProjectController &other = workspace.current().controller;
    QVERIFY(other.canStart());
    desk.replies = {"<cancel>"};
    bool managedAndRefused = false;
    connect(&workspace, &ProjectWorkspace::changed, this, [&] { managedAndRefused = managedAndRefused || (workspace.isManaging() && !other.canStart()); });
    QCOMPARE(answered([&](auto done) { workspace.open(std::nullopt, done); }), std::optional(false));
    QVERIFY(managedAndRefused && other.canStart());
}

void ProjectWorkspaceFlowTests::aWorkspaceThatEndsMidwayCallsNobody()
{
    QTemporaryDir folder;
    store(folder.filePath("Slow.comp"), 1);
    const QUrl dropped = picture(folder, "Late");
    // Every worker is held: loads and decodes have to wait.
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
    int calls = 0;
    auto loading = std::make_unique<ProjectWorkspace>();
    loading->open(folder.filePath("Slow.comp"), [&](bool) { calls += 1; });
    auto importing = std::make_unique<ProjectWorkspace>();
    const std::shared_ptr<ProjectTab> kept = importing->tabs()[0];
    importing->receive({dropped}, std::nullopt, std::nullopt, [&] { calls += 1; });
    QTRY_VERIFY(kept->session.isImporting());
    loading.reset();
    importing.reset();
    gate.release(workers);
    // The tab someone still holds finishes its import alone.
    QTRY_VERIFY(kept->session.document().has_value() && !kept->session.isImporting());
    QVERIFY(pool->waitForDone(10'000));
    QTest::qWait(100);
    QCOMPARE(calls, 0);
}

void ProjectWorkspaceFlowTests::aTabRemovedMidImportStillEndsTheDrop()
{
    QTemporaryDir folder;
    const QUrl dropped = picture(folder, "Late");
    ProjectWorkspace workspace;
    const std::weak_ptr<ProjectTab> first = workspace.tabs()[0];
    // Every worker is held: the decode has to wait.
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
    workspace.receive({dropped}, std::nullopt, std::nullopt, [&] { done = true; });
    QTRY_VERIFY(first.lock()->session.isImporting());
    // Off the strip, the tab lives until its import ends.
    workspace.removeTab(first.lock()->id);
    QVERIFY(!first.expired() && workspace.isManaging());
    gate.release(workers);
    QTRY_VERIFY(done);
    QVERIFY(!workspace.isManaging() && workspace.canSwitch());
    QTRY_VERIFY(first.expired());
}

void ProjectWorkspaceFlowTests::aProjectThatMovedAwayIsNoMatchForAnotherMissingOne()
{
    QTemporaryDir folder;
    store(folder.filePath("Here.comp"), 1);
    ProjectWorkspace workspace;
    QCOMPARE(answered([&](auto done) { workspace.open(folder.filePath("Here.comp"), done); }), std::optional(true));
    workspace.newCanvas();
    // Its folder moved away: neither place resolves any more.
    QVERIFY(QDir().rename(folder.filePath("Here.comp"), folder.filePath("Moved.comp")));
    DialogDesk desk;
    desk.replies = {"OK"};
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^Couldn’t open the project: .+"));
    QCOMPARE(answered([&](auto done) { workspace.open(folder.filePath("Elsewhere.comp"), done); }), std::optional(false));
    QCOMPARE(desk.seen.size(), 1);
    QCOMPARE(titles(workspace), (QStringList{"Here", "Untitled 2*"}));
    // The same missing place is still the same place,
    QCOMPARE(answered([&](auto done) { workspace.open(folder.filePath("sub/../Here.comp"), done); }), std::optional(true));
    QCOMPARE(titles(workspace), (QStringList{"Here*", "Untitled 2"}));
    // also when named from the folder the process stands in.
    workspace.select(workspace.tabs()[1]->id);
    QDir::setCurrent(folder.path());
    QCOMPARE(answered([&](auto done) { workspace.open(QString("Here.comp"), done); }), std::optional(true));
    QCOMPARE(titles(workspace), (QStringList{"Here*", "Untitled 2"}));
}

void ProjectWorkspaceFlowTests::everyCallIsWholeWithoutACompletion()
{
    QTemporaryDir folder;
    store(folder.filePath("Quiet.comp"), 1);
    ProjectWorkspace workspace;
    // Refused, with nobody to tell: each posted answer is nothing.
    workspace.current().session.setIsProjectBusy(true);
    workspace.open(folder.filePath("Quiet.comp"));
    workspace.confirmQuit({});
    workspace.close(workspace.selectedID());
    workspace.copyLayer(QUuid::createUuid(), std::nullopt);
    QTest::qWait(30);
    QCOMPARE(titles(workspace), (QStringList{"Untitled*"}));
    workspace.current().session.setIsProjectBusy(false);
    // Carried out, with nobody to tell.
    workspace.open(folder.filePath("Quiet.comp"));
    QTRY_COMPARE(titles(workspace), (QStringList{"Quiet*"}));
    QTRY_VERIFY(!workspace.isManaging());
    workspace.receive({picture(folder, "Silent")}, workspace.selectedID());
    QTRY_COMPARE(int(workspace.current().session.document().value().layers.size()), 2);
    QTRY_VERIFY(!workspace.isManaging());
    const QUuid id = workspace.current().session.activeLayerID().value();
    workspace.copyLayer(id, std::nullopt);
    QTRY_COMPARE(int(workspace.tabs().size()), 2);
    QTRY_VERIFY(!workspace.isManaging());
    QMimeData row;
    row.setData(ProjectWorkspace::layerType, uuidString(id).toUtf8());
    workspace.select(workspace.tabs()[0]->id);
    workspace.receiveProviders(row);
    QTRY_COMPARE(int(workspace.tabs().size()), 3);
    QTRY_VERIFY(!workspace.isManaging());
    workspace.confirmQuit({});
    DialogDesk desk;
    desk.replies = {"Don’t Save", "Don’t Save", "Don’t Save"};
    QTRY_COMPARE(desk.seen.size(), 3);
    QTRY_VERIFY(!workspace.isManaging());
    QTest::qWait(30);
}

QTEST_MAIN(ProjectWorkspaceFlowTests)
#include "ProjectWorkspaceFlowTests.moc"
