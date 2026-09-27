#include "ContentView.h"
#include "DialogDesk.h"
#include "UI/LayersPanel.h"
#include "UI/ToolHeaderStyle.h"
#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest>

// One project's editor area, driven as a user drives it.
namespace {
template <typename Widget> Widget &find(QWidget &root, const char *name)
{
    Widget *found = root.findChild<Widget *>(QString::fromLatin1(name));
    if (!found)
        throw std::runtime_error(std::string("no widget named ") + name);
    return *found;
}

void type(QLineEdit &field, const QString &text)
{
    field.setFocus();
    field.selectAll();
    QTest::keyClicks(&field, text);
}

const std::pair<NavigationTool, const char *> labelled[] = {
    {NavigationTool::move, "Move / Transform (V)"},
    {NavigationTool::marquee, "Marquee (M)"},
    {NavigationTool::lasso, "Lasso (L)"},
    {NavigationTool::wand, "Magic (W) · Tab switches Wand and Object"},
    {NavigationTool::crop, "Crop (C)"},
    {NavigationTool::brush, "Brush (B) · Eraser (E)"},
    {NavigationTool::spotHealing, "Spot Healing Brush (J)"},
    {NavigationTool::cloneStamp, "Clone Stamp (S) · Alt-click sets the source"},
    {NavigationTool::blur, "Smear (R)"},
    {NavigationTool::gradient, "Gradient (G)"},
    {NavigationTool::shape, "Shape (U) · Shift-U switches Rectangle/Ellipse"},
    {NavigationTool::type, "Type (T)"},
    {NavigationTool::eyedropper, "Eyedropper (I)"},
    {NavigationTool::hand, "Hand (H)"},
    {NavigationTool::zoom, "Zoom (Z)"},
};
}

class ContentViewTests : public QObject {
    Q_OBJECT
private slots:
    void testCreateCanvasAndNavigation();
    void theRailHoldsEveryToolInSwiftsOrder();
    void theBarFollowsTheTool();
    void anImportErrorIsShownUntilItIsDismissed();
    void theImporterPicksFilesAndClearsItsFlag();
    void openProjectGoesToTheController();
    void theCanvasSitsUnderTheWelcomeAndTakesTheKeysWithADocument();
    void aShownViewClosesWithoutHearingItsSession();
};

void ContentViewTests::testCreateCanvasAndNavigation()
{
    EditorSession session;
    ContentView view(session);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto &width = find<QLineEdit>(view, "widthInput"), &height = find<QLineEdit>(view, "heightInput");
    auto &create = find<QPushButton>(view, "createCanvas");
    QVERIFY(width.isVisible() && width.hasFocus());
    type(width, "0");
    QVERIFY(!create.isEnabled());
    type(width, "1200");
    type(height, "800");
    QTest::mouseClick(&create, Qt::LeftButton);
    QCOMPARE(find<QLabel>(view, "canvasDimensions").text(), QString("1,200 × 800 px"));
    // The welcome is gone, fields and all.
    QVERIFY(view.findChildren<QLineEdit *>("widthInput").isEmpty());
    // Swift's welcome makes a project with its first layer.
    QCOMPARE(int(session.document().value().layers.size()), 1);
    QCOMPARE(session.document().value().layers.front().name, QString("Layer 1"));
    session.zoom(1);
    QCOMPARE(find<QLabel>(view, "zoomStatus").text(), QString("100%"));
    session.zoom(session.viewport.zoom() * 1.25);
    QCOMPARE(find<QLabel>(view, "zoomStatus").text(), QString("125%"));
    // Layouts settle in the event loop; then the picture holds.
    QCoreApplication::processEvents();
    QVERIFY(view.grab().save(QCoreApplication::applicationDirPath() + QStringLiteral("/EditorFoundation.png")));
    // A project that is cleared brings a fresh welcome back.
    auto *copied = new QMimeData;
    QImage small(400, 300, QImage::Format_RGBA8888);
    QByteArray png;
    QBuffer buffer(&png);
    small.save(&buffer, "png");
    copied->setData("image/png", png);
    QApplication::clipboard()->setMimeData(copied);
    session.clearProject();
    auto &fresh = find<QLineEdit>(view, "widthInput");
    QVERIFY(fresh.isVisible());
    QCOMPARE(fresh.text(), QString("400"));
    QCOMPARE(find<QLineEdit>(view, "heightInput").text(), QString("300"));
    QCOMPARE(find<QLabel>(view, "colourStatus").text(), QString("Ready when you are"));
    QApplication::clipboard()->clear();
}

void ContentViewTests::theRailHoldsEveryToolInSwiftsOrder()
{
    EditorSession session;
    session.createDocument(8, 8);
    ContentView view(session);
    view.show();
    const QList<QToolButton *> buttons = find<QWidget>(view, "toolRail").findChildren<QToolButton *>();
    QCOMPARE(buttons.size(), 15);
    QCOMPARE(find<QWidget>(view, "toolRail").width(), 56);
    int above = -1;
    for (int index = 0; index < 15; ++index) {
        QCOMPARE(buttons[index]->toolTip(), QString::fromUtf8(labelled[index].second));
        QCOMPARE(buttons[index]->accessibleName(), QString::fromUtf8(labelled[index].second));
        QCOMPARE(buttons[index]->size(), QSize(36, 36));
        // Top to bottom, in that order.
        const int y = buttons[index]->mapTo(&view, QPoint(0, 0)).y();
        QVERIFY(y > above);
        above = y;
    }
    // One button is down: the session's tool, however chosen.
    for (int index = 0; index < 15; ++index) {
        QTest::mouseClick(buttons[index], Qt::LeftButton);
        QCOMPARE(session.tool(), labelled[index].first);
        for (int other = 0; other < 15; ++other)
            QCOMPARE(buttons[other]->isChecked(), other == index);
    }
    session.selectTool(NavigationTool::crop);
    QVERIFY(buttons[4]->isChecked() && !buttons[14]->isChecked());
    session.selectTool(NavigationTool::idle);
    for (const QToolButton *button : buttons)
        QVERIFY(!button->isChecked());
    // A refused click leaves the old button down, alone.
    session.selectTool(NavigationTool::move);
    session.setIsProjectBusy(true);
    QTest::mouseClick(buttons[1], Qt::LeftButton);
    QCOMPARE(session.tool(), NavigationTool::move);
    for (int other = 0; other < 15; ++other)
        QCOMPARE(buttons[other]->isChecked(), other == 0);
    session.setIsProjectBusy(false);
    // A chosen button looks chosen: its plate is painted.
    session.selectTool(NavigationTool::move);
    const QImage chosen = buttons[0]->grab().toImage(), plain = buttons[1]->grab().toImage();
    QVERIFY(chosen.pixelColor(18, 2) != plain.pixelColor(18, 2));
    // The Marquee's and the Lasso's icons follow their kinds.
    session.setMarqueeKind(LassoKind::ellipse);
    QVERIFY(buttons[1]->grab().toImage() != plain);
    session.setMarqueeKind(LassoKind::rectangle);
    QCOMPARE(buttons[1]->grab().toImage(), plain);
    const QImage freehand = buttons[2]->grab().toImage();
    session.setLassoKind(LassoKind::polygonal);
    QVERIFY(buttons[2]->grab().toImage() != freehand);
    // The brush's shows the eraser while it erases.
    QToolButton *brush = nullptr;
    for (int index = 0; index < 15; ++index) {
        if (labelled[index].first == NavigationTool::brush)
            brush = buttons[index];
    }
    QVERIFY(brush);
    const QImage painting = brush->grab().toImage();
    session.setBrushMode(BrushToolMode::erase);
    QVERIFY(brush->grab().toImage() != painting);
    session.setBrushMode(BrushToolMode::paint);
    QCOMPARE(brush->grab().toImage(), painting);
    // Magic's shows the object icon in Object mode.
    QToolButton *const magic = buttons[3];
    const QImage colours = magic->grab().toImage();
    session.setWandMode(WandMode::object);
    QVERIFY(magic->grab().toImage() != colours);
    session.setWandMode(WandMode::wand);
    QCOMPARE(magic->grab().toImage(), colours);
}

void ContentViewTests::theBarFollowsTheTool()
{
    EditorSession session;
    session.createDocument(8, 8);
    ContentView view(session);
    view.show();
    const auto bar = [&]() -> ToolHeaderBar & { return find<ToolHeaderBar>(view, "toolHeader"); };
    // The first tool is Move: its inspector.
    QCOMPARE(bar().height(), 42);
    QCOMPARE(bar().title->text(), QString("Transform"));
    QVERIFY(find<QCheckBox>(view, "transformAutoSelect").isVisible());
    QCOMPARE(bar().mapTo(&view, QPoint(0, 0)), QPoint(0, 0));
    session.selectTool(NavigationTool::idle);
    QCOMPARE(bar().title->text(), QString("Select a tool"));
    QCOMPARE(bar().title->font().pixelSize(), 13);
    QCOMPARE(bar().height(), 42);
    session.selectTool(NavigationTool::eyedropper);
    QCOMPARE(bar().title->text(), QString("Eyedropper"));
    session.selectTool(NavigationTool::hand);
    QCOMPARE(bar().title->text(), QString("Pan"));
    QVERIFY(!find<QLineEdit>(view, "zoomPercentage").isVisible());
    // Hand and Zoom share one bar, never rebuilt between them.
    bar().setProperty("kept", true);
    session.selectTool(NavigationTool::zoom);
    QVERIFY(bar().property("kept").toBool());
    QCOMPARE(bar().title->text(), QString("Zoom"));
    QVERIFY(find<QLineEdit>(view, "zoomPercentage").isVisible());
    session.selectTool(NavigationTool::brush);
    QVERIFY(!bar().property("kept").toBool());
    QCOMPARE(bar().title->text(), QString("Brush"));
    QVERIFY(find<QLineEdit>(view, "brushSize").isVisible());
    // Every brush tool keeps that bar under its own title.
    bar().setProperty("kept", true);
    session.selectTool(NavigationTool::spotHealing);
    QCOMPARE(bar().title->text(), QString("Spot Healing"));
    QVERIFY(find<QLineEdit>(view, "brushSize").isVisible());
    session.selectTool(NavigationTool::cloneStamp);
    QCOMPARE(bar().title->text(), QString("Clone Stamp"));
    session.selectTool(NavigationTool::blur);
    QCOMPARE(bar().title->text(), QString("Smear"));
    QVERIFY(bar().property("kept").toBool());
    // Gradient, Shape, Type and Crop have bars of their own.
    session.selectTool(NavigationTool::gradient);
    QCOMPARE(bar().title->text(), QString("Gradient"));
    QVERIFY(find<QToolButton>(view, "gradientLinear").isVisible());
    session.selectTool(NavigationTool::shape);
    QCOMPARE(bar().title->text(), QString("Shape"));
    QVERIFY(find<QToolButton>(view, "shapeRectangle").isVisible());
    session.selectTool(NavigationTool::type);
    QCOMPARE(bar().title->text(), QString("Type"));
    session.selectTool(NavigationTool::crop);
    QCOMPARE(bar().title->text(), QString("Crop"));
    QVERIFY(find<QComboBox>(view, "cropRatio").isVisible());
    QCOMPARE(bar().height(), 42);
    QCOMPARE(view.findChildren<ToolHeaderBar *>("toolHeader").size(), 1);
    session.selectTool(NavigationTool::blur);
    QCOMPARE(bar().title->text(), QString("Smear"));
    // The three selection tools share the lasso bar.
    session.selectTool(NavigationTool::marquee);
    QCOMPARE(bar().title->text(), QString("Marquee"));
    QVERIFY(find<QToolButton>(view, "marqueeRectangle").isVisible());
    bar().setProperty("kept", true);
    session.selectTool(NavigationTool::lasso);
    QVERIFY(bar().property("kept").toBool());
    QCOMPARE(bar().title->text(), QString("Lasso"));
    session.selectTool(NavigationTool::wand);
    QVERIFY(bar().property("kept").toBool());
    QCOMPARE(bar().title->text(), QString("Magic"));
    QCOMPARE(bar().height(), 42);
    QCOMPARE(bar().height(), 42);
}

void ContentViewTests::anImportErrorIsShownUntilItIsDismissed()
{
    EditorSession session;
    ContentView view(session);
    view.show();
    DialogDesk desk;
    desk.replies = {"OK", "OK", "OK"};
    // Other news meanwhile opens no second alert.
    desk.note = [&] {
        session.selectTool(NavigationTool::hand);
        return QString::number(view.findChildren<QMessageBox *>().size());
    };
    session.setImportError(QString("two files could not be read"));
    QTRY_COMPARE(desk.seen, (QStringList{"alert|2|Import couldn’t finish|two files could not be read|OK|1"}));
    // OK clears the error: file requests may start again.
    QTRY_COMPARE(session.importError(), std::nullopt);
    session.setBrushError(QString("no pixels"));
    QTRY_COMPARE(desk.seen.size(), 2);
    QCOMPARE(desk.seen.last(), QString("alert|2|Couldn’t paint|no pixels|OK|1"));
    QTRY_COMPARE(session.brushError(), std::nullopt);
    session.setCropError(QString("the frame is too large"));
    QTRY_COMPARE(desk.seen.size(), 3);
    QCOMPARE(desk.seen.last(), QString("alert|2|Couldn’t crop|the frame is too large|OK|1"));
    QTRY_COMPARE(session.cropError(), std::nullopt);
    // Cleared from elsewhere, the alert goes without being asked.
    desk.replies.clear();
    session.setImportError(QString("soon gone"));
    session.setImportError(std::nullopt);
    QTest::qWait(50);
    QCOMPARE(desk.seen.size(), 3);
    QCOMPARE(view.findChildren<QMessageBox *>().size(), 0);
}

void ContentViewTests::theImporterPicksFilesAndClearsItsFlag()
{
    QTemporaryDir folder;
    QImage picture(4, 2, QImage::Format_RGBA8888);
    picture.fill(Qt::green);
    QVERIFY(picture.save(folder.filePath("Picked.png")));
    EditorSession session;
    session.createDocument(8, 8);
    ContentView view(session);
    view.show();
    DialogDesk desk;
    QImage second(2, 2, QImage::Format_RGBA8888);
    second.fill(Qt::blue);
    QVERIFY(second.save(folder.filePath("Also.png")));
    desk.replies = {"<cancel>", folder.filePath("Picked.png") + "|" + folder.filePath("Also.png")};
    // One panel however much else changes; it takes several files.
    desk.note = [&] {
        session.selectTool(NavigationTool::hand);
        const QList<QFileDialog *> panels = view.findChildren<QFileDialog *>();
        return QString("%1 %2").arg(panels.size()).arg(panels.value(0) && panels.value(0)->fileMode() == QFileDialog::ExistingFiles ? "many" : "one");
    };
    session.setShowsImporter(true);
    QTRY_COMPARE(desk.seen.size(), 1);
    QTRY_VERIFY(!session.showsImporter());
    QVERIFY(session.document().value().layers.empty());
    session.setShowsImporter(true);
    QTRY_COMPARE(int(session.document().value().layers.size()), 2);
    QCOMPARE(session.document().value().layers.front().name, QString("Picked"));
    QCOMPARE(session.document().value().layers.back().name, QString("Also"));
    QVERIFY(!session.showsImporter());
    // A cancelled panel with a file typed imports nothing.
    desk.replies = {"<cancel>"};
    desk.typeBeforeCancel = folder.filePath("Picked.png");
    session.setShowsImporter(true);
    QTRY_VERIFY(!session.showsImporter());
    QTest::qWait(50);
    QCOMPARE(int(session.document().value().layers.size()), 2);
    desk.typeBeforeCancel.clear();
    QCOMPARE(desk.seen.mid(0, 2), (QStringList{"panel|Open|open|file|||1 many", "panel|Open|open|file|||1 many"}));
    QCOMPARE(desk.seen.size(), 3);
    // The flag cleared from elsewhere takes the panel down.
    desk.replies.clear();
    session.setShowsImporter(true);
    session.setShowsImporter(false);
    QTest::qWait(50);
    QCOMPARE(desk.seen.size(), 3);
}

void ContentViewTests::openProjectGoesToTheController()
{
    EditorSession session;
    ProjectController projects(session);
    ContentView view(session, &projects);
    view.show();
    DialogDesk desk;
    desk.replies = {"<cancel>"};
    QTest::mouseClick(&find<QPushButton>(view, "openProject"), Qt::LeftButton);
    QTRY_COMPARE(desk.seen, (QStringList{"panel|Open Project|open|folder||"}));
    // Without a controller the button has nowhere to go.
    EditorSession lone;
    ContentView bare(lone);
    bare.show();
    QTest::mouseClick(&find<QPushButton>(bare, "openProject"), Qt::LeftButton);
    QTest::qWait(30);
    QCOMPARE(desk.seen.size(), 1);
    QTest::mouseClick(&find<QPushButton>(bare, "importImage"), Qt::LeftButton);
    QVERIFY(lone.showsImporter());
    lone.setShowsImporter(false);
}

void ContentViewTests::theCanvasSitsUnderTheWelcomeAndTakesTheKeysWithADocument()
{
    EditorSession session;
    ContentView view(session);
    view.show();
    QVERIFY(QTest::qWaitForWindowActive(&view));
    auto &canvas = find<CanvasView>(view, "editorCanvas");
    QVERIFY(canvas.isVisible());
    // The rail and the Layers panel take theirs; 480 stays.
    QVERIFY(canvas.width() >= 480 && canvas.height() >= 300);
    // Hidden rulers leave the canvas touching the resize grip.
    QCOMPARE(canvas.mapTo(&view, QPoint(canvas.width(), 0)).x(), view.layersPanel().x() - 8);
    QCOMPARE(canvas.geometry(), canvas.parentWidget()->rect());
    QTRY_VERIFY(find<QLineEdit>(view, "widthInput").hasFocus());
    session.createNewProject(300, 200);
    QTRY_VERIFY(canvas.hasFocus());
    // The viewport follows the canvas; the status shows the fit.
    QTRY_COMPARE(session.viewport.viewSize, QSizeF(canvas.size()));
    QVERIFY(session.viewport.followsFit());
    QCOMPARE(find<QLabel>(view, "zoomStatus").text(), ContentView::percent(session.viewport.zoom()));
    // The view syncs the canvas: its cursor follows the tool.
    session.selectTool(NavigationTool::hand);
    QCOMPARE(canvas.cursor().shape(), Qt::OpenHandCursor);
    // A field keeps the keys through changes; Return returns them.
    session.selectTool(NavigationTool::zoom);
    auto &zoom = find<QLineEdit>(view, "zoomPercentage");
    zoom.setFocus();
    QTRY_VERIFY(zoom.hasFocus());
    session.zoom(1.5);
    QTest::qWait(50);
    QVERIFY(zoom.hasFocus());
    QTest::keyClick(&zoom, Qt::Key_Return);
    QTRY_VERIFY(canvas.hasFocus());
    // A cleared project brings the welcome over the canvas again.
    session.clearProject();
    QVERIFY(canvas.isVisible());
    QTRY_VERIFY(find<QLineEdit>(view, "widthInput").hasFocus());
    session.createNewProject(300, 200);
    QTRY_VERIFY(canvas.hasFocus());
}

void ContentViewTests::aShownViewClosesWithoutHearingItsSession()
{
    EditorSession session;
    session.createDocument(8, 8);
    QSignalSpy changes(&session, &EditorSession::changed);
    {
        ContentView view(session);
        view.show();
        QVERIFY(QTest::qWaitForWindowActive(&view));
        QTRY_VERIFY(find<CanvasView>(view, "editorCanvas").hasFocus());
        changes.clear();
    }
    // Closing cancelled the brush after the view's members went.
    QCOMPARE(changes.count(), 1);
}

QTEST_MAIN(ContentViewTests)
#include "ContentViewTests.moc"
