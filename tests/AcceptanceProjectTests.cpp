#include "AcceptanceFixtures.h"
#include "DNGFixture.h"
#include "DialogDesk.h"
#include "IO/ProjectStore.h"
#include "PSDFixture.h"
#include "UI/ColorPickerSheet.h"
#include <QMenu>

// The acceptance run, part three: sizes, exports, imports, projects, theme.
class AcceptanceProjectTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void cropSizesAndExports();
    void imports();
    void projectsRecentScrubAndReload();
    void omarchyTheme();
};

namespace {
QUuid photo(App &app)
{
    if (!scene(1200, 800).save(app.path("photo.png")))
        throw std::runtime_error("the photo could not be written");
    app.session().createDocument(1200, 800);
    app.importFile(app.path("photo.png"));
    return app.session().activeLayerID().value();
}

// Types into a sheet's field, as the resize tests do.
void type(App &app, const char *name, const QString &text)
{
    auto *field = app.window.findChild<QDialog *>()->findChild<PickerField *>(QString::fromLatin1(name));
    if (!field)
        throw std::runtime_error(std::string("no field named ") + name);
    field->setFocus();
    field->selectAll();
    QTest::keyClicks(field, text);
    QTest::keyClick(field, Qt::Key_Return);
}

QSize document(App &app)
{
    return QSize(int(app.session().document().value().width), int(app.session().document().value().height));
}
}

void AcceptanceProjectTests::cropSizesAndExports()
{
    App app;
    photo(app);
    ProjectController &controller = app.workspace.current().controller;
    // Crop to the middle.
    app.session().selectTool(NavigationTool::crop);
    app.session().setCropRect(QRectF(100, 100, 1000, 600));
    bool done = false;
    app.session().commitCrop([&done] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(document(app), QSize(1000, 600));
    // Canvas Size widens it; Trim takes the clear edges back.
    done = false;
    controller.canvasSize([&done] { done = true; });
    QTRY_VERIFY(app.window.findChild<QDialog *>());
    app.shot(QStringLiteral("10-canvas-size"));
    type(app, "canvasWidth", QStringLiteral("1400"));
    QTRY_VERIFY(done);
    QCOMPARE(document(app), QSize(1400, 600));
    done = false;
    controller.trim([&done] { done = true; });
    QTRY_VERIFY(app.window.findChild<QDialog *>());
    QTest::keyClick(app.window.findChild<QDialog *>(), Qt::Key_Return);
    QTRY_VERIFY(done);
    // The crop hid the photo's edges; Trim finds them again.
    QCOMPARE(document(app), QSize(1200, 600));
    // Image Size halves it.
    done = false;
    controller.imageSize([&done] { done = true; });
    QTRY_VERIFY(app.window.findChild<QDialog *>());
    type(app, "imageWidth", QStringLiteral("600"));
    QTRY_VERIFY(done);
    QCOMPARE(document(app), QSize(600, 300));
    // PNG and JPEG exports, through their panels and sheet.
    DialogDesk desk;
    desk.replies = {app.path("scene.png")};
    done = false;
    controller.exportPNG([&done] { done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 30000);
    QCOMPARE(QImage(app.path("scene.png")).size(), QSize(600, 300));
    desk.replies = {app.path("scene.jpg")};
    done = false;
    controller.exportJPEG([&done] { done = true; });
    QTRY_VERIFY(app.window.findChild<QDialog *>());
    app.shot(QStringLiteral("11-export-jpeg"));
    QPushButton *exported = app.window.findChild<QDialog *>()->findChild<QPushButton *>(QStringLiteral("jpegExport"));
    QVERIFY(exported);
    QTRY_VERIFY_WITH_TIMEOUT(exported->isEnabled(), 30000);
    exported->click();
    QTRY_VERIFY_WITH_TIMEOUT(done, 30000);
    QCOMPARE(QImage(app.path("scene.jpg")).size(), QSize(600, 300));
}

void AcceptanceProjectTests::imports()
{
    App app;
    app.session().createDocument(800, 600);
    QVERIFY(scene(200, 150).save(app.path("Scene.jpg")));
    QVERIFY(scene(200, 150).save(app.path("Scene.png")));
    QVERIFY(scene(200, 150).save(app.path("Scene.tif")));
    QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/red.heic"), app.path("Red.heic")));
    QFile svg(app.path("Mark.svg"));
    QVERIFY(svg.open(QIODevice::WriteOnly));
    svg.write(R"(<svg xmlns="http://www.w3.org/2000/svg" width="120" height="80"><circle cx="60" cy="40" r="30" fill="#e03020"/></svg>)");
    svg.close();
    PSDRecord sky = PSDFixture::record(QStringLiteral("Sky"), PSDFixture::colorImage(40, 20, 0, 0, 1), QRectF(0, 0, 40, 20));
    QFile psd(app.path("Trip.psd"));
    QVERIFY(psd.open(QIODevice::WriteOnly));
    psd.write(PSDFixture::data(PSDDocument{40, 20, 72, {sky}}, PSDFixture::colorImage(40, 20, 0, 0, 0, 0)));
    psd.close();
    DNGFixture::write(app.path("Gray.dng"), {});
    QList<QUrl> urls;
    for (const char *name : {"Scene.jpg", "Scene.png", "Scene.tif", "Red.heic", "Mark.svg", "Trip.psd", "Gray.dng"})
        urls << QUrl::fromLocalFile(app.path(QString::fromLatin1(name)));
    bool done = false;
    app.session().importImages(urls, std::nullopt, [&done] { done = true; });
    // A camera RAW file asks how to develop it.
    QTRY_VERIFY_WITH_TIMEOUT(app.session().rawDevelop(), 30000);
    app.shot(QStringLiteral("12-raw-develop"));
    app.session().finishRawDevelop(app.session().rawDevelop().value().settings);
    QTRY_VERIFY_WITH_TIMEOUT(done, 30000);
    QVERIFY(!app.session().importError());
    QStringList names;
    for (const ImageLayer &layer : app.session().document().value().layers)
        names << layer.name;
    // A layered Photoshop file arrives as a folder of layers.
    QCOMPARE(names, (QStringList{"Scene", "Scene", "Scene", "Red", "Mark", "Trip", "Sky", "Gray"}));
    const std::vector<ImageLayer> &layers = app.session().document().value().layers;
    QVERIFY(layers[5].isGroup);
    QCOMPARE(layers[6].parentID, std::optional<QUuid>(layers[5].id));
    app.shot(QStringLiteral("13-imports"));
}

void AcceptanceProjectTests::projectsRecentScrubAndReload()
{
    App app;
    photo(app);
    ProjectController &controller = app.workspace.current().controller;
    const QString project = app.path("Trip.comp");
    DialogDesk desk;
    desk.replies = {project};
    bool saved = false;
    controller.save(true, [&saved](bool ok) { saved = ok; });
    QTRY_VERIFY_WITH_TIMEOUT(saved, 30000);
    // A second save runs while editing goes on.
    saved = false;
    app.action("newBlankLayer").trigger();
    controller.save(false, [&saved](bool ok) { saved = ok; });
    app.action("newBlankLayer").trigger();
    QCOMPARE(int(app.session().document().value().layers.size()), 3);
    QTRY_VERIFY_WITH_TIMEOUT(saved, 30000);
    QCOMPARE(int(ProjectStore::load(project).manifest.layers.size()), 2);
    // Open Recent lists the project.
    const QMenu *recent = app.action("openRecent").menu();
    QVERIFY(recent);
    QCOMPARE(recent->actions().first()->text(), QStringLiteral("Trip"));
    // Scrubbing the opacity title drags the value.
    app.session().selectLayer(app.session().document().value().layers.front().id);
    QLabel *opacity = nullptr;
    for (QLabel *label : app.window.findChildren<QLabel *>())
        if (label->text() == QStringLiteral("Opacity") && label->cursor().shape() == Qt::SizeHorCursor)
            opacity = label;
    QVERIFY(opacity);
    QTest::mousePress(opacity, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
    QMouseEvent move(QEvent::MouseMove, QPointF(-38, 2), opacity->mapToGlobal(QPointF(-38, 2)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(opacity, &move);
    QTest::mouseRelease(opacity, Qt::LeftButton, Qt::NoModifier, QPoint(-38, 2));
    QVERIFY2(app.session().activeLayer().value().opacity < 0.9, qPrintable(QString::number(app.session().activeLayer().value().opacity)));
    app.shot(QStringLiteral("14-scrub"));
    // Another program rewrites the project; the app asks, then reverts.
    ProjectSnapshot outside = ProjectStore::load(project);
    outside.manifest.layers.front().name = QStringLiteral("Changed Outside");
    QVERIFY(app.session().isModified());
    const qsizetype asked = desk.seen.size();
    desk.replies = {QStringLiteral("Revert")};
    ProjectStore::save(outside, project);
    QTRY_VERIFY_WITH_TIMEOUT(app.session().document().value().layers.front().name == QStringLiteral("Changed Outside"), 30000);
    QCOMPARE(desk.seen.size(), asked + 1);
    QCOMPARE(desk.seen.last(), QString("alert|2|“Trip.comp” was changed on disk.|Another app changed this project. You can revert to the version on "
                                       "disk, losing your unsaved changes, or keep what you have.|Revert,Keep Mine"));
    QVERIFY(desk.replies.isEmpty() && !app.session().isModified());
    QCOMPARE(int(app.session().document().value().layers.size()), 2);
    // The shortcuts window lists what the menus bind.
    app.action("keyboardShortcuts").trigger();
    app.shot(QStringLiteral("15-shortcuts"));
}

void AcceptanceProjectTests::omarchyTheme()
{
    App app;
    photo(app);
    QCOMPARE(app.window.palette().color(QPalette::Base), QColor("#1b1b1b"));
    // Omarchy's Catppuccin Latte, switched in while the app runs.
    QVERIFY(QDir().mkpath(app.path("omarchy/theme")));
    QFile colors(app.path("omarchy/theme/colors.toml"));
    QVERIFY(colors.open(QIODevice::WriteOnly));
    colors.write("mode = \"light\"\naccent = \"#1e66f5\"\nselection = \"#ccd0da\"\nmuted = \"#acb0be\"\nbackground = \"#eff1f5\"\n"
                 "dark_background = \"#e3e4e8\"\ndarker_background = \"#d7d8dc\"\nlighter_background = \"#dce0e8\"\nforeground = \"#4c4f69\"\n"
                 "dark_foreground = \"#9ca0b0\"\nlight_foreground = \"#5c5f77\"\nbright_foreground = \"#4c4f69\"\nred = \"#d20f39\"\n"
                 "yellow = \"#df8e1d\"\norange = \"#d84e2b\"\ngreen = \"#40a02b\"\n");
    colors.close();
    QTRY_COMPARE_WITH_TIMEOUT(app.window.palette().color(QPalette::Window), QColor("#eff1f5"), 10000);
    QCOMPARE(app.window.palette().color(QPalette::Highlight), QColor("#1e66f5"));
    app.shot(QStringLiteral("16-omarchy-light"));
}

QTEST_MAIN(AcceptanceProjectTests)
#include "AcceptanceProjectTests.moc"
