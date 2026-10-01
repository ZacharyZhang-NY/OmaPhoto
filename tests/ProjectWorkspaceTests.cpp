#include "Document/ProjectWorkspace.h"
#include "SelectionFixtures.h"
#include "SessionFixtures.h"
#include <QTemporaryDir>
#include <QtTest>

// Swift's ProjectWorkspaceTests, then the tabs' own rules.
namespace {
ImportedImage white()
{
    QImage image(1, 1, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::white);
    return ImportedImage(image, QImage(), "White");
}

QList<QUuid> ids(const std::vector<std::shared_ptr<ProjectTab>> &tabs)
{
    QList<QUuid> result;
    for (const std::shared_ptr<ProjectTab> &tab : tabs)
        result << tab->id;
    return result;
}

int layerCount(const ProjectTab &tab)
{
    return int(tab.session.document().value().layers.size());
}
}

class ProjectWorkspaceTests : public QObject {
    Q_OBJECT
private slots:
    void newCanvasOpensAnEmptyTabWithoutAModal();
    void layerDropProviderCopiesIntoANewProject();
    void tabsKeepIndependentDocumentsAndUndo();
    void crossProjectCopyRemapsIdentityAndHasIndependentUndo();
    void dockCreatesTabsAndTargetedImportUsesExistingTab();
    void quitAsksAboutTheActiveTabFirst();
    void tabsAreNamedAndOnlyTheFirstSkipsTheClipboard();
    void selectingEndsATransformAndRefusesWhatItMust();
    void removingATabSelectsItsNeighbour();
    void everyChangeIsAnnouncedAndRefusalsAreSilent();
    void aDropThatIsNoLayerIdIsIgnored();
    void anOpenFilterHoldsTheSwitch();
    void moveTabReordersWithoutTouchingSelectionOrDocuments();
};

void ProjectWorkspaceTests::newCanvasOpensAnEmptyTabWithoutAModal()
{
    ProjectWorkspace workspace;
    ProjectTab &original = workspace.current();
    original.session.createDocument(4000, 3000);
    workspace.newCanvas();
    QCOMPARE(int(workspace.tabs().size()), 2);
    QVERIFY(&workspace.current() != &original);
    QVERIFY(!workspace.current().session.document().has_value());
    QVERIFY(!workspace.current().session.showsNewDocument());
    QVERIFY(workspace.canSwitch());
    QCOMPARE(original.session.document().value().width, 4000);
    workspace.newCanvas();
    QCOMPARE(int(workspace.tabs().size()), 3);
    QVERIFY(!workspace.current().session.showsNewDocument());
    // Beside a single empty tab too, a new tab.
    ProjectWorkspace fresh;
    fresh.newCanvas();
    QCOMPARE(int(fresh.tabs().size()), 2);
    // The tab that is left ends its transform.
    ProjectTab &drawn = fresh.current();
    drawn.session.createDocument(8, 8);
    drawn.session.insert(white());
    drawn.session.beginTransform();
    LayerTransform moved = drawn.session.transformEdit().value().draft;
    moved.origin += QPointF(0, 2);
    drawn.session.previewTransform(moved);
    fresh.newCanvas();
    QCOMPARE(int(fresh.tabs().size()), 3);
    QVERIFY(!drawn.session.transformEdit().has_value());
    QCOMPARE(drawn.session.document().value().layers.front().transform.origin, moved.origin);
}

void ProjectWorkspaceTests::layerDropProviderCopiesIntoANewProject()
{
    ProjectWorkspace workspace;
    ProjectTab &source = workspace.current();
    source.session.createDocument(100, 100);
    source.session.insert(white());
    const QUuid id = source.session.activeLayerID().value();
    QMimeData provider;
    provider.setData(ProjectWorkspace::layerType, uuidString(id).toUtf8());
    bool done = false;
    workspace.receiveProviders(provider, std::nullopt, std::nullopt, [&] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(int(workspace.tabs().size()), 2);
    QVERIFY(workspace.current().id != source.id);
    QCOMPARE(layerCount(workspace.current()), 1);
    QVERIFY(workspace.current().session.activeLayerID().value() != id);
    QCOMPARE(source.session.document().value().layers.front().id, id);
}

void ProjectWorkspaceTests::tabsKeepIndependentDocumentsAndUndo()
{
    ProjectWorkspace workspace;
    ProjectTab &first = workspace.current();
    first.session.createDocument(4000, 4000);
    first.session.addBlankLayer();
    ProjectTab &second = workspace.addTab();
    second.session.createDocument(640, 480);
    second.session.addBlankLayer();
    second.session.undo();
    QCOMPARE(layerCount(first), 1);
    QCOMPARE(layerCount(second), 0);
    workspace.select(first.id);
    QCOMPARE(&workspace.current(), &first);
    QCOMPARE(first.session.document().value().width, 4000);
    const QUuid firstID = first.id;
    workspace.removeTab(second.id);
    QCOMPARE(&workspace.current(), &first);
    workspace.removeTab(firstID);
    QVERIFY(workspace.tabs().size() == 1 && !workspace.current().session.document().has_value());
}

void ProjectWorkspaceTests::crossProjectCopyRemapsIdentityAndHasIndependentUndo()
{
    ProjectWorkspace workspace;
    ProjectTab &first = workspace.current();
    first.session.createDocument(100, 100);
    first.session.insert(white());
    const QUuid original = first.session.activeLayerID().value();
    ProjectTab &second = workspace.addTab();
    second.session.createDocument(200, 200);
    bool done = false;
    workspace.copyLayer(original, second.id, std::nullopt, [&] { done = true; });
    QTRY_VERIFY(done);
    const ImageLayer copied = second.session.document().value().layers.front();
    QVERIFY(copied.id != original);
    QCOMPARE(copied.transform.center(), QPointF(100, 100));
    QCOMPARE(layerCount(first), 1);
    second.session.undo();
    QCOMPARE(layerCount(second), 0);
    QCOMPARE(layerCount(first), 1);
    second.session.redo();
    QCOMPARE(second.session.document().value().layers.front().id, copied.id);
}

void ProjectWorkspaceTests::dockCreatesTabsAndTargetedImportUsesExistingTab()
{
    QTemporaryDir folder;
    QImage picture(4, 2, QImage::Format_RGBA8888);
    picture.fill(Qt::green);
    QVERIFY(picture.save(folder.filePath("Picture.png")));
    const QUrl url = QUrl::fromLocalFile(folder.filePath("Picture.png"));
    ProjectWorkspace workspace;
    bool done = false;
    workspace.receive({url, url}, std::nullopt, std::nullopt, [&] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(int(workspace.tabs().size()), 2);
    const std::shared_ptr<ProjectTab> first = workspace.tabs()[0];
    done = false;
    workspace.receive({url}, first->id, std::nullopt, [&] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(int(workspace.tabs().size()), 2);
    QCOMPARE(&workspace.current(), first.get());
    QCOMPARE(layerCount(*first), 2);
    QCOMPARE(layerCount(*workspace.tabs()[1]), 1);
}

void ProjectWorkspaceTests::quitAsksAboutTheActiveTabFirst()
{
    ProjectWorkspace workspace;
    const QUuid first = workspace.current().id;
    const QUuid second = workspace.addTab(false).id, third = workspace.addTab(false).id;
    workspace.select(second);
    QCOMPARE(ids(workspace.quitOrder()), (QList<QUuid>{second, first, third}));
    workspace.select(third);
    QCOMPARE(ids(workspace.quitOrder()), (QList<QUuid>{third, first, second}));
    workspace.select(first);
    QCOMPARE(ids(workspace.quitOrder()), (QList<QUuid>{first, second, third}));
}

void ProjectWorkspaceTests::tabsAreNamedAndOnlyTheFirstSkipsTheClipboard()
{
    ProjectWorkspace workspace;
    ProjectTab &first = workspace.current();
    QCOMPARE(first.title(), QString("Untitled"));
    QVERIFY(first.session.skipsInitialClipboardCanvasSize);
    QVERIFY(first.controller.workspace == &workspace);
    // A single empty tab is reused unless told otherwise.
    QCOMPARE(&workspace.addTab(), &first);
    QWidget window;
    workspace.window = &window;
    ProjectTab &second = workspace.addTab(false);
    ProjectTab &third = workspace.addTab();
    QCOMPARE(second.title(), QString("Untitled 2"));
    QCOMPARE(third.title(), QString("Untitled 3"));
    QVERIFY(!second.session.skipsInitialClipboardCanvasSize && !third.session.skipsInitialClipboardCanvasSize);
    QVERIFY(second.controller.workspace == &workspace && second.controller.window == &window);
    QCOMPARE(workspace.selectedID(), third.id);
    // A saved project goes by its file's name without suffix.
    third.session.setProjectPath(QString("/somewhere/My.Trip.comp"));
    QCOMPARE(third.title(), QString("My.Trip"));
    // Like Swift: a first or last dot cuts nothing.
    const auto titled = [&](const QString &path) {
        third.session.setProjectPath(path);
        return third.title();
    };
    QCOMPARE(titled("/somewhere/.comp"), QString(".comp"));
    QCOMPARE(titled("/somewhere/.hidden.comp"), QString(".hidden"));
    QCOMPARE(titled("/somewhere/Plain"), QString("Plain"));
    QCOMPARE(titled("/somewhere/Ends."), QString("Ends."));
    QCOMPARE(ProjectTab::nameWithoutSuffix("/somewhere/.comp"), QString(".comp"));
    // One tab with a canvas is not reused.
    ProjectWorkspace drawn;
    drawn.current().session.createDocument(8, 8);
    QVERIFY(&drawn.addTab() != drawn.tabs()[0].get());
    QCOMPARE(int(drawn.tabs().size()), 2);
}

void ProjectWorkspaceTests::selectingEndsATransformAndRefusesWhatItMust()
{
    ProjectWorkspace workspace;
    ProjectTab &first = workspace.current();
    first.session.createDocument(8, 8);
    first.session.insert(white());
    ProjectTab &second = workspace.addTab();
    QWidget window;
    workspace.window = &window;
    QSignalSpy changes(&workspace, &ProjectWorkspace::changed);
    // The tab in front, a stranger: nothing to do.
    workspace.select(second.id);
    workspace.select(QUuid::createUuid());
    QCOMPARE(changes.count(), 0);
    // The tab that is left ends its transform.
    second.session.createDocument(8, 8);
    second.session.insert(white());
    second.session.beginTransform();
    LayerTransform moved = second.session.transformEdit().value().draft;
    moved.origin += QPointF(3, 0);
    second.session.previewTransform(moved);
    workspace.select(first.id);
    QCOMPARE(changes.count(), 1);
    QCOMPARE(workspace.selectedID(), first.id);
    QVERIFY(!second.session.transformEdit().has_value());
    QCOMPARE(second.session.document().value().layers.front().transform.origin, moved.origin);
    // The window goes to the tab that comes forward.
    QVERIFY(first.controller.window == &window && second.controller.window == nullptr);
    // A busy project in front holds the switch.
    first.session.setIsProjectBusy(true);
    QVERIFY(!workspace.canSwitch());
    workspace.select(second.id);
    workspace.newCanvas();
    QCOMPARE(workspace.selectedID(), first.id);
    QCOMPARE(int(workspace.tabs().size()), 2);
    QCOMPARE(changes.count(), 1);
}

void ProjectWorkspaceTests::removingATabSelectsItsNeighbour()
{
    ProjectWorkspace workspace;
    const QUuid a = workspace.current().id;
    const QUuid b = workspace.addTab(false).id, c = workspace.addTab(false).id, d = workspace.addTab(false).id;
    // Not in front: the selection stays.
    workspace.removeTab(b);
    QCOMPARE(ids(workspace.tabs()), (QList<QUuid>{a, c, d}));
    QCOMPARE(workspace.selectedID(), d);
    // The last one in front: the new last takes over.
    workspace.removeTab(d);
    QCOMPARE(workspace.selectedID(), c);
    // The first in front: what slides into its place.
    workspace.select(a);
    workspace.removeTab(a);
    QCOMPARE(workspace.selectedID(), c);
    workspace.removeTab(QUuid::createUuid());
    QCOMPARE(ids(workspace.tabs()), (QList<QUuid>{c}));
    // The very last tab leaves a fresh one behind.
    workspace.removeTab(c);
    QCOMPARE(int(workspace.tabs().size()), 1);
    QVERIFY(workspace.current().id != c);
    QCOMPARE(workspace.current().title(), QString("Untitled 5"));
}

void ProjectWorkspaceTests::everyChangeIsAnnouncedAndRefusalsAreSilent()
{
    ProjectWorkspace workspace;
    QSignalSpy changes(&workspace, &ProjectWorkspace::changed);
    const auto announced = [&](const std::function<void()> &change) {
        const qsizetype before = changes.count();
        change();
        return changes.count() > before;
    };
    QVERIFY(!announced([&] { workspace.addTab(); }));
    QVERIFY(announced([&] { workspace.addTab(false); }));
    QVERIFY(announced([&] { workspace.newCanvas(); }));
    const QUuid first = workspace.tabs()[0]->id;
    QVERIFY(announced([&] { workspace.select(first); }));
    QVERIFY(!announced([&] { workspace.select(first); }));
    QVERIFY(!announced([&] { workspace.removeTab(QUuid::createUuid()); }));
    QVERIFY(announced([&] { workspace.removeTab(first); }));
    // What the last signal sees is what the call leaves.
    QList<QUuid> seen;
    connect(&workspace, &ProjectWorkspace::changed, this, [&] { seen = ids(workspace.tabs()) << workspace.selectedID(); });
    workspace.addTab(false);
    QCOMPARE(seen, ids(workspace.tabs()) << workspace.selectedID());
    workspace.removeTab(workspace.selectedID());
    QCOMPARE(seen, ids(workspace.tabs()) << workspace.selectedID());
}

void ProjectWorkspaceTests::aDropThatIsNoLayerIdIsIgnored()
{
    ProjectWorkspace workspace;
    workspace.current().session.createDocument(8, 8);
    workspace.current().session.insert(white());
    QSignalSpy changes(&workspace, &ProjectWorkspace::changed);
    const auto dropped = [&](const QString &type, const QByteArray &payload) {
        QMimeData provider;
        provider.setData(type, payload);
        bool done = false;
        workspace.receiveProviders(provider, std::nullopt, std::nullopt, [&] { done = true; });
        return QTest::qWaitFor([&] { return done; }, 10'000);
    };
    const QByteArray id = uuidString(workspace.current().session.activeLayerID().value()).toUtf8();
    QVERIFY(dropped(ProjectWorkspace::layerType, "not an id"));
    QVERIFY(dropped(ProjectWorkspace::layerType, id.left(20)));
    QVERIFY(dropped(ProjectWorkspace::layerType, uuidString(QUuid::createUuid()).toUtf8()));
    QCOMPARE(int(workspace.tabs().size()), 1);
    QCOMPARE(changes.count(), 0);
    // Under another type it is no row: nothing there reads.
    QVERIFY(dropped("text/plain", id));
    QVERIFY(workspace.current().session.importError().value().startsWith("Some dropped items couldn’t be read."));
    workspace.current().session.setImportError(std::nullopt);
    QCOMPARE(int(workspace.tabs().size()), 1);
    // Neither white space around the id nor lower case matters.
    QVERIFY(dropped(ProjectWorkspace::layerType, "  " + id.toLower() + "\n"));
    QCOMPARE(int(workspace.tabs().size()), 2);
}

void ProjectWorkspaceTests::anOpenFilterHoldsTheSwitch()
{
    ProjectWorkspace workspace;
    EditorSession &session = workspace.current().session;
    session.createDocument(8, 8, true);
    QImage image(8, 8, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::white);
    session.insert(ImportedImage(image, image, "White"));
    session.applySelection(rectPath(QRectF(2, 2, 2, 2)), SelectionMode::replace, "Select");
    QVERIFY(workspace.canSwitch());
    session.beginFilter(FilterKind::contentAwareFill);
    QVERIFY(!workspace.canSwitch());
    session.cancelFilter();
    QVERIFY(workspace.canSwitch());
    // So does a pixel move under way.
    QVERIFY(session.beginPixelMove());
    QVERIFY(!workspace.canSwitch());
    session.cancelPixelMove();
    QVERIFY(workspace.canSwitch());
    // And an open colour picker.
    session.openColorPicker(false);
    QVERIFY(!workspace.canSwitch());
    session.closeColorPicker(false);
    QVERIFY(workspace.canSwitch());
}

void ProjectWorkspaceTests::moveTabReordersWithoutTouchingSelectionOrDocuments()
{
    ProjectWorkspace workspace;
    const QUuid a = workspace.current().id, b = workspace.addTab(false).id, c = workspace.addTab(false).id;
    const auto order = [&] {
        std::vector<QUuid> ids;
        for (const std::shared_ptr<ProjectTab> &tab : workspace.tabs())
            ids.push_back(tab->id);
        return ids;
    };
    QSignalSpy changed(&workspace, &ProjectWorkspace::changed);
    workspace.moveTab(c, 0);
    QVERIFY((order() == std::vector<QUuid>{c, a, b}));
    workspace.moveTab(a, 2);
    QVERIFY((order() == std::vector<QUuid>{c, b, a}));
    QCOMPARE(changed.size(), 2);
    // Out of range clamps to the ends.
    workspace.moveTab(c, 99);
    QVERIFY((order() == std::vector<QUuid>{b, a, c}));
    workspace.moveTab(c, -4);
    QVERIFY((order() == std::vector<QUuid>{c, b, a}));
    QCOMPARE(changed.size(), 4);
    // Where it is, or no tab: nothing, silently.
    workspace.moveTab(a, 2);
    workspace.moveTab(QUuid::createUuid(), 0);
    QVERIFY((order() == std::vector<QUuid>{c, b, a}));
    QCOMPARE(changed.size(), 4);
    // Chrome: the selection stays.
    QCOMPARE(workspace.selectedID(), c);
}

QTEST_MAIN(ProjectWorkspaceTests)
#include "ProjectWorkspaceTests.moc"
