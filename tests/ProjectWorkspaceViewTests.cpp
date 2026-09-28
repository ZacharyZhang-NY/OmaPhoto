#include "DialogDesk.h"
#include "IO/ProjectStore.h"
#include "UI/ProjectWorkspaceView.h"
#include <QLineEdit>
#include <QProcess>
#include <QScrollBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

// The window: tabs, the toolbar, closing and quitting.
namespace {
template <typename Widget> Widget &find(QWidget &root, const char *name)
{
    Widget *found = root.findChild<Widget *>(QString::fromLatin1(name));
    if (!found)
        throw std::runtime_error(std::string("no widget named ") + name);
    return *found;
}

QAction &action(QWidget &root, const char *name)
{
    QAction *found = root.findChild<QAction *>(QString::fromLatin1(name));
    if (!found)
        throw std::runtime_error(std::string("no action named ") + name);
    return *found;
}

QStringList titles(const ProjectTabStrip &strip)
{
    QStringList result;
    for (const ProjectTabButton *button : strip.buttons())
        result << button->findChild<QToolButton *>("selectTab")->text();
    return result;
}

void store(const QString &path, int layers)
{
    EditorSession session;
    session.createDocument(8, 6);
    for (int index = 0; index < layers; ++index)
        session.addBlankLayer();
    ProjectStore::save(session.projectSnapshot().value(), path);
}
}

class ProjectWorkspaceViewTests : public QObject {
    Q_OBJECT
private slots:
    void testCreateCanvasAndNavigation();
    void theFrontTabHasTheEditorAndTheWindow();
    void tabsShowTheirTitlesDotsAndState();
    void tabButtonsSelectAndClose();
    void theToolbarFollowsTheFrontSession();
    void closingTheWindowAsksThroughTheWorkspace();
    void aReplacedTabOutlivesItsEditor();
    void filesAtLaunchGoToTheWorkspace();
};

void ProjectWorkspaceViewTests::testCreateCanvasAndNavigation()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto &width = find<QLineEdit>(window, "widthInput");
    width.setFocus();
    width.selectAll();
    QTest::keyClicks(&width, "0");
    QVERIFY(!find<QPushButton>(window, "createCanvas").isEnabled());
    width.selectAll();
    QTest::keyClicks(&width, "1200");
    auto &height = find<QLineEdit>(window, "heightInput");
    height.setFocus();
    height.selectAll();
    QTest::keyClicks(&height, "800");
    QTest::mouseClick(&find<QPushButton>(window, "createCanvas"), Qt::LeftButton);
    QCOMPARE(find<QLabel>(window, "canvasDimensions").text(), QString("1,200 × 800 px"));
    workspace.current().session.zoom(0.5);
    action(window, "actualPixels").trigger();
    QCOMPARE(find<QLabel>(window, "zoomStatus").text(), QString("100%"));
    action(window, "zoomIn").trigger();
    QCOMPARE(find<QLabel>(window, "zoomStatus").text(), QString("125%"));
    action(window, "fitCanvas").trigger();
    QVERIFY(workspace.current().session.viewport.followsFit());
    QCoreApplication::processEvents();
    QVERIFY(window.grab().save(QCoreApplication::applicationDirPath() + QStringLiteral("/EditorFoundation.png")));
    // New canvas opens a tab unasked; the canvas stays behind.
    action(window, "newCanvasToolbar").trigger();
    QCOMPARE(int(workspace.tabs().size()), 2);
    QVERIFY(find<QLineEdit>(window, "widthInput").isVisible());
    workspace.select(workspace.tabs()[0]->id);
    QCOMPARE(find<QLabel>(window, "canvasDimensions").text(), QString("1,200 × 800 px"));
}

void ProjectWorkspaceViewTests::theFrontTabHasTheEditorAndTheWindow()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    window.show();
    ProjectTab &first = workspace.current();
    QVERIFY(workspace.window == &window && first.controller.window == &window);
    QVERIFY(window.content() && window.centralWidget() == window.content());
    window.content()->setProperty("first", true);
    // A second tab in front: a fresh editor for it.
    first.session.createDocument(8, 8);
    workspace.newCanvas();
    ProjectTab &second = workspace.current();
    QVERIFY(!window.content()->property("first").toBool() && window.centralWidget() == window.content());
    QVERIFY(second.controller.window == &window);
    QVERIFY(find<QLineEdit>(window, "widthInput").isVisible());
    // Back to the first: its canvas, not the welcome.
    workspace.select(first.id);
    QCOMPARE(find<QLabel>(window, "canvasDimensions").text(), QString("8 × 8 px"));
    QVERIFY(window.findChildren<QLineEdit *>("widthInput").isEmpty());
    // Managing dims the editor; the tabs stay live.
    QVERIFY(window.content()->isEnabled());
    DialogDesk desk;
    desk.note = [&] { return window.content()->isEnabled() ? QStringLiteral("enabled") : QStringLiteral("dimmed"); };
    desk.replies = {"<cancel>"};
    bool done = false;
    workspace.open(std::nullopt, [&](bool) { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(desk.seen, (QStringList{"panel|Open Project|open|folder|||dimmed"}));
    QVERIFY(window.content()->isEnabled());
}

void ProjectWorkspaceViewTests::tabsShowTheirTitlesDotsAndState()
{
    QTemporaryDir folder;
    store(folder.filePath("Trip.comp"), 1);
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    window.show();
    ProjectTabStrip &strip = *window.tabs();
    QCOMPARE(strip.height(), 34);
    QCOMPARE(strip.accessibleName(), QString("Project tabs"));
    // The strip paints no stripe over the toolbar.
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    const QImage bar = window.grab().toImage();
    const QPoint inStrip = strip.mapTo(&window, QPoint(strip.width() - 4, 3)), beside = strip.mapTo(&window, QPoint(strip.width() + 8, 3));
    QCOMPARE(bar.pixelColor(inStrip), bar.pixelColor(beside));
    QCOMPARE(titles(strip), (QStringList{"Untitled"}));
    workspace.current().session.createDocument(8, 8);
    // Unsaved work shows as a dot before the title.
    QCOMPARE(titles(strip), (QStringList{"● Untitled"}));
    bool done = false;
    workspace.open(folder.filePath("Trip.comp"), [&](bool) { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(titles(strip), (QStringList{"● Untitled", "Trip"}));
    const QList<ProjectTabButton *> buttons = strip.buttons();
    QCOMPARE(buttons.size(), 2);
    QVERIFY(!buttons[0]->isActive() && buttons[1]->isActive());
    // Swift's capsule: 28 high, title 35 to 155, cross 16.
    QCOMPARE(buttons[0]->height(), 28);
    QCOMPARE(buttons[0]->sizeHint().height(), 28);
    QCOMPARE(buttons[0]->findChild<QToolButton *>("closeTab")->size(), QSize(16, 28));
    // Titles span 35 to 155, plus 8 and the dot.
    QToolButton &title = *buttons[0]->findChild<QToolButton *>("selectTab");
    const int dot = QFontMetrics(title.font()).horizontalAdvance(QStringLiteral("● ")) - 10;
    workspace.tabs()[0]->session.setProjectPath(QString("/x/") + QString(80, QLatin1Char('W')) + ".comp");
    QTRY_COMPARE(title.width(), 155 + 8 + dot);
    QCOMPARE(buttons[0]->height(), 28);
    workspace.tabs()[0]->session.setProjectPath(QString("/x/I.comp"));
    QTRY_COMPARE(title.width(), 35 + 8 + dot);
    workspace.tabs()[0]->session.setProjectPath(std::nullopt);
    QCOMPARE(buttons[1]->findChild<QToolButton *>("selectTab")->font().weight(), QFont::DemiBold);
    QCOMPARE(buttons[0]->findChild<QToolButton *>("selectTab")->font().weight(), QFont::Medium);
    QCOMPARE(buttons[1]->findChild<QToolButton *>("closeTab")->toolTip(), QString("Close Trip"));
    QCOMPARE(buttons[1]->findChild<QToolButton *>("closeTab")->accessibleName(), QString("Close Trip"));
    // The front tab is painted brighter, at once.
    const QImage front = buttons[1]->grab().toImage(), behind = buttons[0]->grab().toImage();
    QVERIFY(front.pixelColor(3, 14) != behind.pixelColor(3, 14));
    workspace.select(workspace.tabs()[0]->id);
    QCOMPARE(buttons[0]->grab().toImage().pixelColor(3, 14), front.pixelColor(3, 14));
    QCOMPARE(buttons[1]->grab().toImage().pixelColor(3, 14), behind.pixelColor(3, 14));
    workspace.select(workspace.tabs()[1]->id);
    // A dot in a tab behind shows there at once.
    workspace.tabs()[0]->session.history.markSaved();
    workspace.tabs()[0]->session.notify();
    QCOMPARE(titles(strip), (QStringList{"Untitled", "Trip"}));
    workspace.tabs()[0]->session.addBlankLayer();
    QCOMPARE(titles(strip), (QStringList{"● Untitled", "Trip"}));
    // A busy front holds every button but the front's own.
    workspace.current().session.setIsProjectBusy(true);
    QVERIFY(!buttons[0]->findChild<QToolButton *>("selectTab")->isEnabled());
    QVERIFY(buttons[1]->findChild<QToolButton *>("selectTab")->isEnabled());
    QVERIFY(!buttons[0]->findChild<QToolButton *>("closeTab")->isEnabled() && !buttons[1]->findChild<QToolButton *>("closeTab")->isEnabled());
    workspace.current().session.setIsProjectBusy(false);
    QVERIFY(buttons[0]->findChild<QToolButton *>("selectTab")->isEnabled() && buttons[1]->findChild<QToolButton *>("closeTab")->isEnabled());
    // Buttons stay: the same widget for the same tab.
    buttons[0]->setProperty("kept", true);
    workspace.addTab(false);
    QCOMPARE(titles(strip), (QStringList{"● Untitled", "Trip", "Untitled 2"}));
    QVERIFY(strip.buttons()[0]->property("kept").toBool());
    workspace.removeTab(workspace.tabs()[1]->id);
    QCOMPARE(titles(strip), (QStringList{"● Untitled", "Untitled 2"}));
    QVERIFY(strip.buttons()[0]->property("kept").toBool());
    QCOMPARE(strip.buttons().size(), 2);
    // A tab made behind shows its title without a nudge.
    ProjectTab &late = workspace.addTab(false);
    late.session.setProjectPath(QString("/x/Late.comp"));
    workspace.select(workspace.tabs()[0]->id);
    QCOMPARE(titles(strip), (QStringList{"● Untitled", "Untitled 2", "Late"}));
    // More tabs than fit: the front one scrolls into view.
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    for (int index = 0; index < 8; ++index)
        workspace.addTab(false).session.setProjectPath(QString("/x/Wide project number %1.comp").arg(index));
    const ProjectTabButton *last = strip.buttons().last();
    const auto wholeInView = [&](const ProjectTabButton *button) {
        return strip.viewport()->rect().contains(QRect(button->mapTo(strip.viewport(), QPoint(0, 0)), button->size()));
    };
    // In view once laid out, as Swift posts its reveal.
    QVERIFY(last->tab->id == workspace.selectedID());
    QTRY_VERIFY(wholeInView(last));
    QVERIFY(strip.widget()->width() > strip.viewport()->width());
    QVERIFY(last->findChild<QToolButton *>("selectTab")->width() >= 35 && strip.horizontalScrollBar()->maximum() > 0);
    // Since 1.2.4 a growing title keeps the offset.
    const int offset = strip.horizontalScrollBar()->value();
    workspace.tabs().back()->session.setProjectPath(QString("/x/") + QString(60, QLatin1Char('W')) + ".comp");
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    QVERIFY(last->width() > 180);
    QCOMPARE(strip.horizontalScrollBar()->value(), offset);
    workspace.select(workspace.tabs()[0]->id);
    const ProjectTabButton *head = strip.buttons().first();
    QTRY_VERIFY(wholeInView(head));
}

void ProjectWorkspaceViewTests::tabButtonsSelectAndClose()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    const QUuid first = workspace.current().id;
    workspace.current().session.createDocument(8, 8);
    workspace.newCanvas();
    const QUuid second = workspace.current().id;
    const QList<ProjectTabButton *> buttons = window.tabs()->buttons();
    QTest::mouseClick(buttons[0]->findChild<QToolButton *>("selectTab"), Qt::LeftButton);
    QCOMPARE(workspace.selectedID(), first);
    QTest::mouseClick(buttons[1]->findChild<QToolButton *>("selectTab"), Qt::LeftButton);
    QCOMPARE(workspace.selectedID(), second);
    // Closing the first asks; Don't Save removes it.
    DialogDesk desk;
    desk.replies = {"Don’t Save"};
    QTest::mouseClick(buttons[0]->findChild<QToolButton *>("closeTab"), Qt::LeftButton);
    QTRY_COMPARE(int(workspace.tabs().size()), 1);
    QCOMPARE(workspace.selectedID(), second);
    QCOMPARE(titles(*window.tabs()), (QStringList{"Untitled 2"}));
}

void ProjectWorkspaceViewTests::theToolbarFollowsTheFrontSession()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    window.show();
    QAction &fit = action(window, "fitCanvas"), &actual = action(window, "actualPixels");
    QAction &in = action(window, "zoomIn"), &out = action(window, "zoomOut"), &fresh = action(window, "newCanvasToolbar");
    QCOMPARE(fit.toolTip(), QString("Fit canvas in window (Ctrl+0)"));
    QCOMPARE(actual.toolTip(), QString("Actual pixels (Ctrl+1)"));
    QCOMPARE(in.toolTip(), QString("Zoom in (Ctrl++)"));
    QCOMPARE(out.toolTip(), QString("Zoom out (Ctrl+−)"));
    QCOMPARE(fresh.toolTip(), QString("New canvas (Ctrl+N) · Drop images here for new tabs"));
    // No canvas: the zooms wait; New canvas is ready.
    QVERIFY(!fit.isEnabled() && !actual.isEnabled() && !in.isEnabled() && !out.isEnabled() && fresh.isEnabled());
    EditorSession &session = workspace.current().session;
    session.createDocument(400, 300);
    QVERIFY(fit.isEnabled() && actual.isEnabled() && in.isEnabled() && out.isEnabled());
    actual.trigger();
    out.trigger();
    QCOMPARE(session.viewport.zoom(), 2.0 / 3.0);
    in.trigger();
    in.trigger();
    QCOMPARE(session.viewport.zoom(), 1.25);
    // Importing or a busy spell holds New canvas.
    session.setIsImporting(true);
    QVERIFY(!fresh.isEnabled());
    session.setIsImporting(false);
    session.setIsProjectBusy(true);
    QVERIFY(fresh.isEnabled());
    QTRY_VERIFY(!fresh.isEnabled());
    session.setIsProjectBusy(false);
    QVERIFY(fresh.isEnabled());
    // The window carries the project's state for the frame.
    QVERIFY(window.isWindowModified());
    QCOMPARE(window.windowTitle(), QString("Untitled[*]"));
    session.setProjectPath(QString("/somewhere/Trip.comp"));
    QCOMPARE(window.windowFilePath(), QString("/somewhere/Trip.comp"));
    QCOMPARE(window.windowTitle(), QString("Trip[*]"));
    session.history.markSaved();
    session.notify();
    QVERIFY(!window.isWindowModified());
    // Another tab in front: the toolbar follows that session.
    workspace.newCanvas();
    QVERIFY(!fit.isEnabled() && !window.isWindowModified());
    QCOMPARE(window.windowFilePath(), QString());
    QCOMPARE(window.windowTitle(), QString("Untitled 2[*]"));
    // At its smallest the window still shows the whole editor.
    window.resize(800, 520);
    QTRY_COMPARE(window.size(), QSize(800, 520));
    QWidget &status = find<QWidget>(window, "statusBar");
    QTRY_VERIFY(window.rect().contains(status.mapTo(&window, status.rect().bottomRight())));
}

void ProjectWorkspaceViewTests::closingTheWindowAsksThroughTheWorkspace()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    workspace.current().session.createDocument(8, 8);
    DialogDesk desk;
    desk.replies = {"Cancel", "Don’t Save"};
    // Cancel keeps the window and its tab.
    window.close();
    QTRY_COMPARE(desk.seen.size(), 1);
    QTRY_VERIFY(!workspace.isManaging());
    QVERIFY(window.isVisible());
    QVERIFY(workspace.current().session.document().has_value());
    // Don't Save closes it and leaves one fresh tab behind.
    window.close();
    QTRY_VERIFY(!window.isVisible());
    QCOMPARE(int(workspace.tabs().size()), 1);
    QVERIFY(!workspace.current().session.document().has_value());
    // Nothing unsaved: no question at all.
    window.show();
    window.close();
    QTRY_VERIFY(!window.isVisible());
    QCOMPARE(desk.seen.size(), 2);
    // Shown again with new work, it asks again.
    window.show();
    workspace.current().session.createDocument(8, 8);
    desk.replies = {"Cancel"};
    window.close();
    QTRY_COMPARE(desk.seen.size(), 3);
    QTRY_VERIFY(!workspace.isManaging());
    QVERIFY(window.isVisible());
}

void ProjectWorkspaceViewTests::aReplacedTabOutlivesItsEditor()
{
    QTemporaryDir folder;
    store(folder.filePath("Trip.comp"), 1);
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    window.show();
    // The project replaces the empty tab; its editor goes first.
    const std::weak_ptr<ProjectTab> untitled = workspace.tab(workspace.current().id);
    std::optional<bool> aliveAtTheEnd;
    connect(window.content(), &QObject::destroyed, this, [&] { aliveAtTheEnd = !untitled.expired(); });
    bool opened = false;
    workspace.open(folder.filePath("Trip.comp"), [&](bool done) { opened = done; });
    QTRY_VERIFY(opened);
    QVERIFY(aliveAtTheEnd.value());
    QVERIFY(untitled.expired());
    QCOMPARE(int(workspace.tabs().size()), 1);
    QCOMPARE(find<QLabel>(window, "canvasDimensions").text(), QString("8 × 6 px"));
}

void ProjectWorkspaceViewTests::filesAtLaunchGoToTheWorkspace()
{
    QTemporaryDir folder;
    store(folder.filePath("Trip.comp"), 2);
    QImage picture(4, 2, QImage::Format_RGBA8888);
    picture.fill(Qt::green);
    QVERIFY(picture.save(folder.filePath("Picture.png")));
    // The real app opens its arguments; the log says so.
    QProcess app;
    app.setProcessChannelMode(QProcess::MergedChannels);
    // A Qt built for journald logs there unless told otherwise.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("QT_FORCE_STDERR_LOGGING", "1");
    app.setProcessEnvironment(environment);
    app.start(QCoreApplication::applicationDirPath() + "/omaphoto", {folder.filePath("Trip.comp"), folder.filePath("Picture.png")});
    QVERIFY(app.waitForStarted(10'000));
    QByteArray log;
    QTRY_VERIFY2_WITH_TIMEOUT((log += app.readAll()).contains("imported a request of 1 files"), log.constData(), 30'000);
    app.kill();
    app.waitForFinished(10'000);
    const qsizetype opened = log.indexOf("installed a project of 2 layers from " + folder.filePath("Trip.comp").toUtf8());
    const qsizetype imported = log.indexOf("imported " + folder.filePath("Picture.png").toUtf8() + " 4 x 2");
    // Both arrived, the project first, as they were named.
    QVERIFY2(opened >= 0 && imported > opened, log.constData());
    QVERIFY2(log.contains("OmaPhoto " OMAPHOTO_VERSION " on Qt"), log.constData());
}

QTEST_MAIN(ProjectWorkspaceViewTests)
#include "ProjectWorkspaceViewTests.moc"
