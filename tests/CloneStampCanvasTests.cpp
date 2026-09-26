#include "BrushCanvasFixtures.h"

// Swift's Clone Stamp on the canvas: source, crosshair, preview.
namespace {
// Left half red with a green square, right half blue.
void colors(Canvas &shown)
{
    QImage image = BrushRaster::context(400, 300, false);
    QPainter painter(&image);
    painter.fillRect(QRect(0, 0, 200, 300), QColor(255, 0, 0));
    painter.fillRect(QRect(200, 0, 200, 300), QColor(0, 0, 255));
    painter.fillRect(QRect(60, 60, 120, 120), QColor(0, 255, 0));
    painter.end();
    shown.session.insert(ImportedImage(image, image, QStringLiteral("Colors")));
    shown.session.selectTool(NavigationTool::cloneStamp);
    shown.session.setBrushSettings(BrushSettings{.diameter = 10, .hardness = 1});
    shown.canvas->synchronizeDisplay();
}

std::vector<int> at(const QImage &image, int x, int y)
{
    const QImage bytes = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    const uchar *pixel = bytes.constScanLine(y) + x * 4;
    return {pixel[0], pixel[1], pixel[2], pixel[3]};
}

std::vector<int> centre(const QImage &image)
{
    return at(image, image.width() / 2, image.height() / 2);
}

bool near(QColor colour, int red, int green, int blue)
{
    return std::abs(colour.red() - red) <= 3 && std::abs(colour.green() - green) <= 3 && std::abs(colour.blue() - blue) <= 3;
}

const std::vector<int> greenPixel = {0, 255, 0, 255};
}

class CloneStampCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void altClickSetsTheSourceAndPaintsNothing();
    void theCrosshairAndPreviewShowTheSource();
    void altAndSpaceBringACursorBack();
    void thePreviewIsKeptUntilSomethingChanges();
    void thePreviewTakesOneClicksSoftness();
    void theBrushKeepsItsAltClicks();
    void theCrosshairAndPreviewFollowTheZoom();
    void thePreviewShowsTheLayerOrEveryLayerAtTheSource();
    void aCancelledStrokeOrAnotherStepRedrawsThePreview();
};

void CloneStampCanvasTests::altClickSetsTheSourceAndPaintsNothing()
{
    Canvas shown;
    colors(shown);
    const int steps = shown.session.history.undoCount();
    shown.click(QPointF(120, 120), Qt::AltModifier);
    QCOMPARE(shown.session.cloneSource().value(), QPointF(120, 120));
    QVERIFY(!shown.session.brushStroke() && !shown.session.brushError());
    QCOMPARE(shown.session.history.undoCount(), steps);
    // Without a source, a press asks for one.
    Canvas bare;
    colors(bare);
    bare.click(QPointF(300, 200));
    QCOMPARE(bare.session.brushError().value(), QString("Alt-click where Clone Stamp should copy from first."));
    QVERIFY(!bare.session.cloneSource());
}

void CloneStampCanvasTests::theCrosshairAndPreviewShowTheSource()
{
    Canvas shown;
    colors(shown);
    shown.click(QPointF(120, 120), Qt::AltModifier);
    shown.hover(QPointF(300, 200));
    QCOMPARE(shown.canvas->brushCursor().marker().value(), QPointF(120, 120));
    QCOMPARE(centre(shown.canvas->brushCursor().preview()), greenPixel);
    // A stroke fixes the offset: crosshair follows, preview goes.
    shown.press(QPointF(300, 200));
    QVERIFY(shown.session.brushStroke());
    QVERIFY(shown.canvas->brushCursor().preview().isNull());
    shown.move(QPointF(310, 205));
    QCOMPARE(shown.canvas->brushCursor().marker().value(), QPointF(130, 125));
    shown.release(QPointF(310, 205));
    QCOMPARE(pixel(shown.session, 300, 200), greenPixel);
    QCOMPARE(shown.session.history.undoName(), QString("Clone Stamp"));
    // Aligned, the next hover previews from the kept offset.
    shown.hover(QPointF(360, 290));
    QCOMPARE(shown.canvas->brushCursor().marker().value(), QPointF(180, 210));
    QCOMPARE(centre(shown.canvas->brushCursor().preview()), (std::vector<int>{255, 0, 0, 255}));
    // Leaving takes the circle and the crosshair with it.
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(shown.canvas, &leave);
    QVERIFY(!shown.canvas->brushCursor().marker() && !shown.canvas->brushCursor().circle());
}

void CloneStampCanvasTests::altAndSpaceBringACursorBack()
{
    Canvas shown;
    colors(shown);
    shown.hover(QPointF(300, 200));
    QCOMPARE(shown.canvas->cursor().shape(), Qt::CrossCursor);
    shown.click(QPointF(120, 120), Qt::AltModifier);
    shown.hover(QPointF(300, 200));
    QCOMPARE(shown.canvas->cursor().shape(), Qt::BlankCursor);
    QVERIFY(!shown.canvas->brushCursor().preview().isNull());
    // Holding Alt to pick again: the crosshair cursor, no preview.
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    QTest::keyPress(shown.canvas, Qt::Key_Alt, Qt::AltModifier);
    QCOMPARE(shown.canvas->cursor().shape(), Qt::CrossCursor);
    QVERIFY(shown.canvas->brushCursor().preview().isNull());
    QVERIFY(shown.canvas->brushCursor().marker().has_value());
    QTest::keyRelease(shown.canvas, Qt::Key_Alt, Qt::NoModifier);
    QCOMPARE(shown.canvas->cursor().shape(), Qt::BlankCursor);
    QVERIFY(!shown.canvas->brushCursor().preview().isNull());
    QTest::keyPress(shown.canvas, Qt::Key_Space);
    QCOMPARE(shown.canvas->cursor().shape(), Qt::OpenHandCursor);
    QVERIFY(!shown.canvas->brushCursor().marker() && !shown.canvas->brushCursor().circle());
    QTest::keyRelease(shown.canvas, Qt::Key_Space);
    QCOMPARE(shown.canvas->cursor().shape(), Qt::BlankCursor);
    // The source is Clone Stamp's: the Brush keeps its crosshair.
    shown.session.selectTool(NavigationTool::brush);
    shown.hover(QPointF(301, 200));
    QCOMPARE(shown.canvas->cursor().shape(), Qt::CrossCursor);
}

void CloneStampCanvasTests::thePreviewIsKeptUntilSomethingChanges()
{
    Canvas shown;
    colors(shown);
    shown.click(QPointF(120, 120), Qt::AltModifier);
    shown.hover(QPointF(300, 200));
    const qint64 first = shown.canvas->brushCursor().preview().cacheKey();
    // Before any stroke it shows the source wherever you point.
    shown.hover(QPointF(301, 200));
    QCOMPARE(shown.canvas->brushCursor().preview().cacheKey(), first);
    // A step, place, sample, size or layer draws it anew.
    shown.drag(QPointF(300, 200), QPointF(302, 200));
    shown.hover(QPointF(300, 200));
    const qint64 stepped = shown.canvas->brushCursor().preview().cacheKey();
    QVERIFY(stepped != first);
    shown.hover(QPointF(301, 200));
    const qint64 moved = shown.canvas->brushCursor().preview().cacheKey();
    QVERIFY(moved != stepped);
    shown.session.setCloneSettings(CloneSettings{true, true});
    shown.hover(QPointF(301, 200));
    const qint64 all = shown.canvas->brushCursor().preview().cacheKey();
    QVERIFY(all != moved);
    BrushSettings settings = shown.session.brushSettings();
    settings.diameter = 12;
    shown.session.setBrushSettings(settings);
    shown.hover(QPointF(301, 200));
    const qint64 wider = shown.canvas->brushCursor().preview().cacheKey();
    QVERIFY(wider != all);
    QCOMPARE(shown.canvas->brushCursor().preview().size(), QSize(12, 12));
    shown.session.addBlankLayer();
    shown.hover(QPointF(301, 200));
    QVERIFY(shown.canvas->brushCursor().preview().cacheKey() != wider);
}

void CloneStampCanvasTests::thePreviewTakesOneClicksSoftness()
{
    Canvas shown;
    colors(shown);
    BrushSettings settings = shown.session.brushSettings();
    settings.diameter = 60;
    shown.session.setBrushSettings(settings);
    shown.click(QPointF(120, 120), Qt::AltModifier);
    shown.hover(QPointF(300, 200));
    // Hard: green to near the rim, over the blue.
    QImage view = shown.canvas->grab().toImage();
    QCOMPARE(view.pixelColor(300, 200), QColor(0, 255, 0));
    QCOMPARE(view.pixelColor(324, 200), QColor(0, 255, 0));
    // Soft: one click fades, so the rim shows mostly blue.
    settings.hardness = 0;
    shown.session.setBrushSettings(settings);
    shown.hover(QPointF(300, 200));
    view = shown.canvas->grab().toImage();
    const QColor middle = view.pixelColor(300, 200), rim = view.pixelColor(324, 200);
    QVERIFY2(middle.green() > 100 && rim.green() < middle.green() - 60 && rim.blue() > 150,
             qPrintable(middle.name() + " " + rim.name()));
    // The tip: one click at the brush's size, rounded up.
    const QImage soft = shown.canvas->brushCursor().tip();
    QCOMPARE(soft.size(), QSize(60, 60));
    QVERIFY(at(soft, 30, 30)[3] > 240 && at(soft, 54, 30)[3] < 60);
    settings.hardness = 1;
    settings.diameter = 60.5;
    settings.opacity = 0.5;
    shown.session.setBrushSettings(settings);
    shown.hover(QPointF(300, 200));
    const QImage hard = shown.canvas->brushCursor().tip();
    QCOMPARE(hard.size(), QSize(61, 61));
    QCOMPARE(at(hard, 55, 30)[3], 255);
    // At half opacity the preview is half over the blue.
    view = shown.canvas->grab().toImage();
    QVERIFY2(near(view.pixelColor(300, 200), 0, 128, 128), qPrintable(view.pixelColor(300, 200).name()));
}

void CloneStampCanvasTests::theBrushKeepsItsAltClicks()
{
    Canvas shown;
    colors(shown);
    shown.session.selectTool(NavigationTool::brush);
    shown.click(QPointF(120, 120), Qt::AltModifier);
    QVERIFY(!shown.session.cloneSource());
}

void CloneStampCanvasTests::theCrosshairAndPreviewFollowTheZoom()
{
    Canvas shown;
    colors(shown);
    shown.click(QPointF(120, 120), Qt::AltModifier);
    shown.hover(QPointF(300, 200));
    QCOMPARE(shown.canvas->brushCursor().preview().size(), QSize(10, 10));
    shown.session.zoom(2, QPointF(0, 0));
    shown.canvas->synchronizeDisplay();
    const QPointF source = shown.session.viewport.viewPoint(QPointF(120, 120), shown.documentSize());
    QVERIFY(source != QPointF(120, 120));
    shown.hover(QPointF(300, 200));
    QCOMPARE(shown.canvas->brushCursor().marker().value(), source);
    // Screen pixels: ten document pixels at 200% are twenty.
    QCOMPARE(shown.canvas->brushCursor().preview().size(), QSize(20, 20));
    BrushSettings settings = shown.session.brushSettings();
    settings.diameter = 2000;
    shown.session.setBrushSettings(settings);
    shown.hover(QPointF(300, 200));
    QCOMPARE(shown.canvas->brushCursor().preview().size(), QSize(1024, 1024));
}

void CloneStampCanvasTests::thePreviewShowsTheLayerOrEveryLayerAtTheSource()
{
    Canvas shown;
    colors(shown);
    const QUuid colorsID = shown.session.activeLayerID().value();
    // Green begins at x 60: red left, green right.
    shown.click(QPointF(60, 120), Qt::AltModifier);
    shown.hover(QPointF(300, 200));
    const QImage edge = shown.canvas->brushCursor().preview();
    QCOMPARE(at(edge, 2, 5), (std::vector<int>{255, 0, 0, 255}));
    QCOMPARE(at(edge, 7, 5), greenPixel);
    // The blank layer alone shows nothing; every layer shows green.
    shown.session.addBlankLayer();
    shown.session.selectTool(NavigationTool::cloneStamp);
    shown.hover(QPointF(301, 200));
    QCOMPARE(at(shown.canvas->brushCursor().preview(), 7, 5), (std::vector<int>{0, 0, 0, 0}));
    shown.session.setCloneSettings(CloneSettings{true, true});
    shown.hover(QPointF(301, 200));
    QCOMPARE(at(shown.canvas->brushCursor().preview(), 7, 5), greenPixel);
    // Choosing another layer, no step taken, draws it anew.
    shown.session.setCloneSettings(CloneSettings{true, false});
    shown.hover(QPointF(301, 200));
    const int steps = shown.session.history.undoCount();
    shown.session.selectLayer(colorsID);
    QCOMPARE(shown.session.history.undoCount(), steps);
    shown.hover(QPointF(301, 200));
    QCOMPARE(at(shown.canvas->brushCursor().preview(), 7, 5), greenPixel);
}

void CloneStampCanvasTests::aCancelledStrokeOrAnotherStepRedrawsThePreview()
{
    Canvas shown;
    colors(shown);
    shown.session.addBlankLayer();
    const QUuid blank = shown.session.activeLayerID().value();
    shown.session.selectLayer(shown.session.document().value().layers.front().id);
    shown.session.selectTool(NavigationTool::cloneStamp);
    shown.click(QPointF(120, 120), Qt::AltModifier);
    shown.hover(QPointF(300, 200));
    const qint64 first = shown.canvas->brushCursor().preview().cacheKey();
    // Escape ends a stroke with no step: the revision moved.
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    shown.press(QPointF(300, 200));
    QTest::keyClick(shown.canvas, Qt::Key_Escape);
    QVERIFY(!shown.session.brushStroke());
    shown.release(QPointF(300, 200));
    shown.hover(QPointF(300, 200));
    const qint64 cancelled = shown.canvas->brushCursor().preview().cacheKey();
    QVERIFY(cancelled != first);
    // A step elsewhere moves the history alone.
    shown.session.toggleLayerVisibility(blank);
    shown.hover(QPointF(300, 200));
    QVERIFY(shown.canvas->brushCursor().preview().cacheKey() != cancelled);
}

QTEST_MAIN(CloneStampCanvasTests)
#include "CloneStampCanvasTests.moc"
