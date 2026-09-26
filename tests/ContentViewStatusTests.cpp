#include "ContentView.h"
#include <QPainter>
#include <QProgressBar>
#include <QtTest>

// The status bar: numbers, colour space, and each tool's words.
namespace {
template <typename Widget> Widget &find(QWidget &root, const char *name)
{
    Widget *found = root.findChild<Widget *>(QString::fromLatin1(name));
    if (!found)
        throw std::runtime_error(std::string("no widget named ") + name);
    return *found;
}
}

class ContentViewStatusTests : public QObject {
    Q_OBJECT
private slots:
    void theStatusBarSaysWhatIsGoingOn();
    void everyToolHasItsHint();
    void zoomPrintsAsSwiftPrintsIt();
};

void ContentViewStatusTests::theStatusBarSaysWhatIsGoingOn()
{
    EditorSession session;
    ContentView view(session);
    view.show();
    auto &zoom = find<QLabel>(view, "zoomStatus"), &size = find<QLabel>(view, "canvasDimensions");
    auto &colour = find<QLabel>(view, "colourStatus"), &activity = find<QLabel>(view, "activityStatus");
    auto &spinner = *view.findChild<QProgressBar *>();
    QVERIFY(!zoom.isVisible() && !size.isVisible() && !spinner.isVisible());
    QCOMPARE(colour.text(), QString("Ready when you are"));
    session.createDocument(30000, 12345);
    QVERIFY(zoom.isVisible() && size.isVisible());
    QCOMPARE(size.text(), QString("30,000 × 12,345 px"));
    QCOMPARE(colour.text(), QString("sRGB · Transparent"));
    QCOMPARE(zoom.width(), 62);
    // Importing shows at once; busy only after its quarter second.
    session.setIsImporting(true);
    QVERIFY(spinner.isVisible());
    QCOMPARE(activity.text(), QString("Importing images…"));
    session.setIsProjectBusy(true);
    QCOMPARE(activity.text(), QString("Importing images…"));
    QTRY_COMPARE(activity.text(), QString("Working…"));
    QVERIFY(spinner.isVisible());
    session.setIsProjectBusy(false);
    session.setIsImporting(false);
    QVERIFY(!spinner.isVisible());
    QCOMPARE(activity.text(), ContentView::hint(session.tool()));
    // The Marquee's and the Lasso's words follow their kinds.
    session.selectTool(NavigationTool::marquee);
    session.setMarqueeKind(LassoKind::ellipse);
    QCOMPARE(activity.text(), ContentView::hint(NavigationTool::marquee, ToolIconKind::ellipse));
    QVERIFY(activity.text().startsWith("Drag an ellipse"));
    session.selectTool(NavigationTool::lasso);
    session.setLassoKind(LassoKind::polygonal);
    QCOMPARE(activity.text(), ContentView::hint(NavigationTool::lasso, ToolIconKind::polygonal));
    QVERIFY(activity.text().startsWith("Click corners"));
    session.setLassoKind(LassoKind::freehand);
    QCOMPARE(activity.text(), ContentView::hint(NavigationTool::lasso));
    // The brush's words follow its mode.
    session.selectTool(NavigationTool::brush);
    session.setBrushMode(BrushToolMode::erase);
    QCOMPARE(activity.text(), QString("Drag to erase · [ ] size · Shift-[ ] hardness · 1–0 opacity · Escape cancel · Space to pan"));
    session.setBrushMode(BrushToolMode::paint);
    QCOMPARE(activity.text(), ContentView::hint(NavigationTool::brush));
    // The Smear's words follow its mode.
    session.selectTool(NavigationTool::blur);
    session.setBlurMode(BlurToolMode::blur);
    QCOMPARE(activity.text(), QString("Drag to soften · [ ] size · Shift-[ ] hardness · 1–0 strength · Space to pan"));
    session.setBlurMode(BlurToolMode::smudge);
    QCOMPARE(activity.text(), QString("Drag to smudge · [ ] size · Shift-[ ] hardness · 1–0 strength · Space to pan"));
    QCOMPARE(ContentView::hint(NavigationTool::brush, ToolIconKind::plain, BlurToolMode::smudge), ContentView::hint(NavigationTool::brush));
    session.setBlurMode(BlurToolMode::liquify);
    QCOMPARE(activity.text(), ContentView::hint(NavigationTool::blur));
    // Busy alone turns the spinner as well.
    session.setIsProjectBusy(true);
    QTRY_COMPARE(activity.text(), QString("Working…"));
    QVERIFY(spinner.isVisible());
    session.setIsProjectBusy(false);
    QVERIFY(!spinner.isVisible());
    QCOMPARE(view.findChild<QWidget *>("statusBar")->height(), 30);
    // At the narrowest the hint gives way, not the numbers.
    session.selectTool(NavigationTool::marquee);
    view.resize(800, 520);
    QTest::qWait(30);
    QVERIFY(size.width() >= size.sizeHint().width() && colour.width() >= colour.sizeHint().width());
    QVERIFY(activity.width() < activity.sizeHint().width());
    QVERIFY(activity.geometry().right() <= view.width() - 18);
    // The hint keeps its dim tone under any palette.
    QPalette loud = activity.palette();
    loud.setColor(QPalette::PlaceholderText, QColor(200, 20, 20));
    loud.setColor(QPalette::WindowText, QColor(20, 20, 200));
    activity.setPalette(loud);
    const QImage tinted = activity.grab().toImage();
    int red = 0, blue = 0;
    for (int y = 0; y < tinted.height(); ++y) {
        for (int x = 0; x < tinted.width(); ++x) {
            const QColor pixel = tinted.pixelColor(x, y);
            red += pixel.red() > 150 && pixel.blue() < 100;
            blue += pixel.blue() > 150 && pixel.red() < 100;
        }
    }
    QVERIFY2(red > 100 && blue == 0, qPrintable(QString("%1 %2").arg(red).arg(blue)));
    activity.setPalette(QPalette());
    // What it paints there is the hint ending in dots.
    const auto painted = [&](const QString &words) {
        QImage image(activity.size(), QImage::Format_RGB32);
        image.fill(activity.palette().color(QPalette::Window));
        QPainter painter(&image);
        painter.setFont(activity.font());
        painter.setPen(activity.palette().color(QPalette::PlaceholderText));
        painter.drawText(image.rect(), Qt::AlignRight | Qt::AlignVCenter, words);
        return image;
    };
    // Tones differ a little between paint paths; letters do not.
    const auto apart = [&](const QImage &one, const QImage &other) {
        int count = 0;
        for (int y = 0; y < one.height(); ++y) {
            for (int x = 0; x < one.width(); ++x)
                count += std::abs(qRed(one.pixel(x, y)) - qRed(other.pixel(x, y))) > 64;
        }
        return count;
    };
    const QString cut = activity.fontMetrics().elidedText(activity.text(), Qt::ElideRight, activity.width());
    QVERIFY(cut.endsWith(QChar(0x2026)) && cut != activity.text());
    const QImage narrow = activity.grab().toImage().convertToFormat(QImage::Format_RGB32);
    QVERIFY2(apart(narrow, painted(cut)) < 20 && apart(narrow, painted(activity.text())) > 200,
             qPrintable(QString("%1 %2").arg(apart(narrow, painted(cut))).arg(apart(narrow, painted(activity.text())))));
    view.resize(2000, 520);
    QTest::qWait(30);
    QVERIFY(activity.width() >= activity.sizeHint().width());
    QVERIFY(apart(activity.grab().toImage().convertToFormat(QImage::Format_RGB32), painted(activity.text())) < 20);
}

void ContentViewStatusTests::everyToolHasItsHint()
{
    const std::pair<NavigationTool, const char *> hints[] = {
        {NavigationTool::move, "Drag to move · Handles to resize · Circle to rotate · 1–0 layer opacity · Space to pan"},
        {NavigationTool::marquee, "Drag a rectangle · Shift add · Alt subtract · Shift again mid-drag square · Drag inside to move · Ctrl-drag moves pixels · Delete clears · Ctrl+D deselect"},
        {NavigationTool::lasso, "Drag to select · Drag inside to move · Shift add · Alt subtract · Delete clears · Alt+Backspace/Ctrl+Backspace fill · Ctrl+D deselect"},
        {NavigationTool::wand, "Click to select similar colors · Shift add · Alt subtract · Drag inside to move · Ctrl-drag moves pixels · Delete clears · Ctrl+D deselect"},
        {NavigationTool::crop, "Drag to crop · Enter apply · Escape cancel · Space to pan"},
        {NavigationTool::brush, "Drag to paint · [ ] size · Shift-[ ] hardness · 1–0 opacity · Escape cancel · Space to pan"},
        {NavigationTool::spotHealing, "Drag over blemishes to heal · [ ] size · Shift-[ ] hardness · Escape cancel · Space to pan"},
        {NavigationTool::cloneStamp, "Alt-click to set the source · Drag to clone · [ ] size · Shift-[ ] hardness · 1–0 opacity · Space to pan"},
        {NavigationTool::blur, "Drag to push pixels · [ ] size · Shift-[ ] hardness · 1–0 strength · Space to pan"},
        {NavigationTool::gradient, "Drag to draw · Drag ends to adjust · Shift 45° · 1–0 opacity · Enter apply · Escape cancel"},
        {NavigationTool::shape, "Drag to draw a shape on a new layer · Shift square · Alt from center · Shift-U or Tab for the next shape · Escape cancel · Space to pan"},
        {NavigationTool::type, "Drag a text box · Click text to edit · Drag box handles to resize · Ctrl+Return finish · Escape cancel"},
        {NavigationTool::eyedropper, "Click to zoom in · Alt-click to zoom out · Drag right or left to zoom smoothly · Space to pan"},
        {NavigationTool::hand, "Drag to pan · Pinch to zoom"},
        {NavigationTool::zoom, "Click to zoom in · Alt-click to zoom out · Drag right or left to zoom smoothly · Space to pan"},
        {NavigationTool::idle, "No tool selected · Press a tool's key to pick one · Space to pan"},
    };
    // Another tool's kind leaves a tool's words alone.
    QCOMPARE(ContentView::hint(NavigationTool::move, ToolIconKind::eraser), ContentView::hint(NavigationTool::move));
    QCOMPARE(ContentView::hint(NavigationTool::lasso, ToolIconKind::ellipse), ContentView::hint(NavigationTool::lasso));
    QCOMPARE(ContentView::hint(NavigationTool::marquee, ToolIconKind::polygonal), ContentView::hint(NavigationTool::marquee));
    EditorSession session;
    session.createDocument(8, 8);
    ContentView view(session);
    for (const auto &[tool, words] : hints) {
        session.selectTool(tool);
        QCOMPARE(find<QLabel>(view, "activityStatus").text(), QString::fromUtf8(words));
    }
    // The Shape's Shift follows its kind.
    session.selectTool(NavigationTool::shape);
    session.setShapeKind(ShapeKind::ellipse);
    QCOMPARE(find<QLabel>(view, "activityStatus").text(),
             QString("Drag to draw a shape on a new layer · Shift circle · Alt from center · Shift-U or Tab for the next shape · Escape cancel · Space to pan"));
    session.setShapeKind(ShapeKind::line);
    QVERIFY(find<QLabel>(view, "activityStatus").text().contains(QString("· Shift 45° ·")));
    QCOMPARE(ContentView::hint(NavigationTool::move, ToolIconKind::plain, BlurToolMode::liquify, ShapeKind::line), ContentView::hint(NavigationTool::move));
}

void ContentViewStatusTests::zoomPrintsAsSwiftPrintsIt()
{
    QCOMPARE(ContentView::percent(1), QString("100%"));
    QCOMPARE(ContentView::percent(0.125), QString("12.5%"));
    QCOMPARE(ContentView::percent(1.0 / 3), QString("33.3%"));
    QCOMPARE(ContentView::percent(0.001), QString("0.1%"));
    QCOMPARE(ContentView::percent(16), QString("1,600%"));
    QCOMPARE(ContentView::percent(32), QString("3,200%"));
    QCOMPARE(ContentView::percent(0.9996), QString("100%"));
}

QTEST_MAIN(ContentViewStatusTests)
#include "ContentViewStatusTests.moc"
