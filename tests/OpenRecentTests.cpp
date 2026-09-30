#include "ExternalChangeFixtures.h"
#include "IO/RecentProjects.h"
#include "MenuFixtures.h"
#include <QApplication>
#include <QLabel>
#include <QMenu>
#include <QSettings>
#include <QStandardPaths>

// Swift's 73dc0f4: File > Open Recent.
namespace {
QString canonical(const QString &path)
{
    return QFileInfo(path).canonicalFilePath();
}

// A saved project named `name` in `root`.
QString project(const QTemporaryDir &root, const QString &name)
{
    const QString path = root.filePath(name + QStringLiteral(".comp"));
    if (!QDir().rename(savedProject(root), path))
        throw std::runtime_error("could not name " + path.toStdString());
    return path;
}

QStringList titles(QMenu &menu)
{
    QStringList shown;
    for (const QAction *entry : menu.actions())
        shown << (entry->isSeparator() ? QStringLiteral("-") : entry->text() + (entry->isEnabled() ? "" : " (off)"));
    return shown;
}
}

class OpenRecentTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void opensAndSavesAreNotedNewestFirst();
    void theListKeepsTenAndOneEntryAPlace();
    void movedProjectsLeaveTheList();
    void theMenuListsOpensAndClears();
    void aProjectDeletedWhileListedIsNamed();
    void comingBackToTheAppChecksAgain();
    void onlyWhatLandsIsNoted();
    void aReloadAfterSavingNotesItsPlaceLast();

private:
    QTemporaryDir m_stored;
};

void OpenRecentTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    // Starts from what earlier runs stored, missing places dropped.
    QVERIFY(m_stored.isValid());
    QSettings().setValue(QStringLiteral("recentProjects"), QStringList{m_stored.filePath("Missing.comp"), m_stored.path()});
    QCOMPARE(RecentProjects::shared().paths(), QStringList{m_stored.path()});
}

void OpenRecentTests::init()
{
    RecentProjects::shared().clear();
}

void OpenRecentTests::opensAndSavesAreNotedNewestFirst()
{
    QTemporaryDir root;
    const QString opened = project(root, QStringLiteral("Opened"));
    EditorSession session;
    ProjectController controller(session);
    QVERIFY(answered([&](auto done) { controller.open(opened + QStringLiteral("/"), done); }));
    QCOMPARE(RecentProjects::shared().paths(), QStringList{canonical(opened)});
    // Saved elsewhere: the copy comes first.
    const QString copy = root.filePath(QStringLiteral("Copy.comp"));
    DialogDesk desk;
    desk.replies = {copy};
    QVERIFY(answered([&](auto done) { controller.save(true, done); }));
    QCOMPARE(RecentProjects::shared().paths(), (QStringList{canonical(copy), canonical(opened)}));
    // Opened again: back to the front, listed once.
    QVERIFY(answered([&](auto done) { controller.open(opened, done); }));
    QCOMPARE(RecentProjects::shared().paths(), (QStringList{canonical(opened), canonical(copy)}));
    // A failed open notes nothing.
    QDir(root.filePath("Broken.comp")).mkpath(".");
    desk.replies = {"OK"};
    QVERIFY(!answered([&](auto done) { controller.open(root.filePath("Broken.comp"), done); }));
    QCOMPARE(RecentProjects::shared().paths(), (QStringList{canonical(opened), canonical(copy)}));
    QCOMPARE(QSettings().value(QStringLiteral("recentProjects")).toStringList(), RecentProjects::shared().paths());
}

void OpenRecentTests::theListKeepsTenAndOneEntryAPlace()
{
    QTemporaryDir root;
    QStringList made;
    for (int index = 0; index < 12; ++index) {
        const QString place = root.filePath(QString::number(index));
        QVERIFY(QDir().mkpath(place));
        made.prepend(canonical(place));
        RecentProjects::shared().note(place);
    }
    QCOMPARE(RecentProjects::shared().paths(), made.mid(0, 10));
    // A link to a listed place is that place.
    QVERIFY(QFile::link(root.filePath("5"), root.filePath("link")));
    RecentProjects::shared().note(root.filePath("link"));
    QCOMPARE(RecentProjects::shared().paths().front(), made[6]);
    QCOMPARE(RecentProjects::shared().paths().size(), 10);
}

void OpenRecentTests::movedProjectsLeaveTheList()
{
    QTemporaryDir root;
    const QString kept = project(root, QStringLiteral("Kept"));
    const QString moved = project(root, QStringLiteral("Moved"));
    RecentProjects::shared().note(kept);
    RecentProjects::shared().note(moved);
    int announced = 0;
    connect(&RecentProjects::shared(), &RecentProjects::changed, this, [&] { ++announced; });
    QVERIFY(QDir().rename(moved, root.filePath("Elsewhere.comp")));
    RecentProjects::shared().refresh();
    QCOMPARE(RecentProjects::shared().paths(), QStringList{canonical(kept)});
    QCOMPARE(announced, 1);
    // Nothing changed: no signal.
    RecentProjects::shared().refresh();
    QCOMPARE(announced, 1);
    // Back in place, it is listed again.
    QVERIFY(QDir().rename(root.filePath("Elsewhere.comp"), moved));
    RecentProjects::shared().refresh();
    QCOMPARE(RecentProjects::shared().paths().size(), 2);
    disconnect(&RecentProjects::shared(), nullptr, this, nullptr);
}

void OpenRecentTests::theMenuListsOpensAndClears()
{
    QTemporaryDir root;
    const QString first = project(root, QStringLiteral("First"));
    const QString second = project(root, QStringLiteral("Second"));
    Bar bar;
    QMenu &recent = *bar.action("openRecent").menu();
    // Swift's place: right after Open Project…, no key.
    const QList<QAction *> file = bar.menus->parent()->findChildren<QMenu *>(QString(), Qt::FindDirectChildrenOnly).front()->actions();
    QCOMPARE(file.indexOf(&bar.action("openRecent")), file.indexOf(&bar.action("openProject")) + 1);
    QCOMPARE(file.indexOf(&bar.action("importImages")), file.indexOf(&bar.action("openRecent")) + 1);
    QCOMPARE(bar.action("openRecent").text(), QString("Open Recent"));
    QVERIFY(bar.action("openRecent").shortcut().isEmpty());
    QCOMPARE(titles(recent), (QStringList{"-", "Clear Menu (off)"}));
    RecentProjects::shared().note(first);
    RecentProjects::shared().note(second);
    QCOMPARE(titles(recent), (QStringList{"Second", "First", "-", "Clear Menu"}));
    // An entry opens its project in a tab.
    recent.actions()[1]->trigger();
    QTRY_VERIFY(bar.session().projectPath().has_value());
    QCOMPARE(bar.session().projectPath().value(), canonical(first));
    QCOMPARE(titles(recent), (QStringList{"First", "Second", "-", "Clear Menu"}));
    // Busy holds the whole menu, as Open Project.
    QVERIFY(bar.action("openRecent").isEnabled());
    bar.session().setIsProjectBusy(true);
    QVERIFY(!bar.action("openRecent").isEnabled());
    bar.session().setIsProjectBusy(false);
    QVERIFY(bar.action("openRecent").isEnabled());
    bar.action("clearRecent").trigger();
    QCOMPARE(titles(recent), (QStringList{"-", "Clear Menu (off)"}));
    QVERIFY(RecentProjects::shared().paths().isEmpty());
    // An ampersand is part of the name, no mnemonic.
    RecentProjects::shared().note(project(root, QStringLiteral("R&D")));
    QCOMPARE(recent.actions()[0]->text(), QString("R&&D"));
    QCOMPARE(recent.actions()[0]->iconText(), QString("R&D"));
}

void OpenRecentTests::aProjectDeletedWhileListedIsNamed()
{
    QTemporaryDir root;
    const QString gone = project(root, QStringLiteral("<b>Gone"));
    RecentProjects::shared().note(gone);
    const QString listed = canonical(gone);
    QVERIFY(QDir(gone).removeRecursively());
    EditorSession session;
    ProjectController controller(session);
    DialogDesk desk;
    desk.replies = {"OK"};
    // The name shows literally, never as markup.
    desk.note = [] {
        for (QWidget *top : QApplication::topLevelWidgets()) {
            if (auto *label = top->findChild<QLabel *>(QStringLiteral("qt_msgbox_informativelabel")))
                return QStringLiteral("format=%1").arg(int(label->textFormat()));
        }
        return QStringLiteral("no label");
    };
    QVERIFY(!answered([&](auto done) { controller.open(listed, done); }));
    QCOMPARE(desk.seen.size(), 1);
    QVERIFY2(desk.seen[0].startsWith("alert|2|Couldn’t open the project|The file “<b>Gone.comp” couldn’t be opened because there is no such file.")
                 && desk.seen[0].endsWith("|format=0"),
             qPrintable(desk.seen[0]));
    QVERIFY(RecentProjects::shared().paths().isEmpty());
    QVERIFY(!session.isProjectBusy());
}

void OpenRecentTests::comingBackToTheAppChecksAgain()
{
    QTemporaryDir root;
    const QString moved = project(root, QStringLiteral("Moved"));
    RecentProjects::shared().note(moved);
    QVERIFY(QDir().rename(moved, root.filePath("Elsewhere.comp")));
    // Only becoming active looks again.
    emit qGuiApp->applicationStateChanged(Qt::ApplicationInactive);
    QCOMPARE(RecentProjects::shared().paths().size(), 1);
    emit qGuiApp->applicationStateChanged(Qt::ApplicationActive);
    QVERIFY(RecentProjects::shared().paths().isEmpty());
}

void OpenRecentTests::onlyWhatLandsIsNoted()
{
    QTemporaryDir root;
    const QString kept = project(root, QStringLiteral("Kept")), other = project(root, QStringLiteral("Other"));
    const QString incoming = project(root, QStringLiteral("Incoming"));
    EditorSession session;
    ProjectController controller(session);
    QVERIFY(answered([&](auto done) { controller.open(kept, done); }));
    RecentProjects::shared().note(other);
    const QStringList before{canonical(other), canonical(kept)};
    // Replacing unsaved work cancelled: the incoming project stays out.
    session.addBlankLayer();
    DialogDesk desk;
    desk.replies = {"Cancel", "<cancel>"};
    QVERIFY(!answered([&](auto done) { controller.open(incoming, done); }));
    QCOMPARE(RecentProjects::shared().paths(), before);
    // Save As cancelled.
    QVERIFY(!answered([&](auto done) { controller.save(true, done); }));
    QCOMPARE(RecentProjects::shared().paths(), before);
    // A failed save over a listed place promotes nothing.
    QVERIFY(QFile::setPermissions(root.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    desk.replies = {"OK"};
    QVERIFY(!answered([&](auto done) { controller.save(false, done); }));
    QVERIFY(QFile::setPermissions(root.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    QCOMPARE(RecentProjects::shared().paths(), before);
    // A save is noted once it lands.
    std::optional<bool> saved;
    controller.save(false, [&](bool value) { saved = value; });
    QVERIFY(controller.externalChanges.saving);
    QCOMPARE(RecentProjects::shared().paths(), before);
    QTRY_VERIFY(saved.has_value());
    QVERIFY(saved.value());
    QCOMPARE(RecentProjects::shared().paths(), (QStringList{canonical(kept), canonical(other)}));
    QCOMPARE(QSettings().value(QStringLiteral("recentProjects")).toStringList(), RecentProjects::shared().paths());
}

void OpenRecentTests::aReloadAfterSavingNotesItsPlaceLast()
{
    QTemporaryDir root;
    const QString reopened = project(root, QStringLiteral("Reopened")), other = project(root, QStringLiteral("Other"));
    EditorSession session;
    ProjectController controller(session);
    QVERIFY(answered([&](auto done) { controller.open(reopened, done); }));
    RecentProjects::shared().note(other);
    // Another place noted as the save lands, before the reload.
    bool armed = true;
    connect(&RecentProjects::shared(), &RecentProjects::changed, this, [&] {
        if (armed && RecentProjects::shared().paths().front() == canonical(reopened)) {
            armed = false;
            RecentProjects::shared().note(other);
        }
    });
    session.addBlankLayer();
    DialogDesk desk;
    desk.replies = {"Save"};
    QVERIFY(answered([&](auto done) { controller.open(reopened, done); }));
    disconnect(&RecentProjects::shared(), nullptr, this, nullptr);
    QVERIFY(!armed);
    QCOMPARE(RecentProjects::shared().paths(), (QStringList{canonical(reopened), canonical(other)}));
}

QTEST_MAIN(OpenRecentTests)
#include "OpenRecentTests.moc"
