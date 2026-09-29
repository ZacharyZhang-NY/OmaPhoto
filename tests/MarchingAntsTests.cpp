#include "SelectionCanvasFixtures.h"

// Swift 1.2.10 (8eba077): ants from a screen-resolution outline.
namespace {
// Alternate single pixels: far past the 20,000 elements drawn whole.
QPainterPath checkerboard(QRect region)
{
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    for (int y = region.top(); y <= region.bottom(); ++y) {
        for (int x = region.left(); x <= region.right(); ++x) {
            if ((x + y) % 2 == 0)
                path.addRect(QRectF(x, y, 1, 1));
        }
    }
    return path;
}

// Pixels the ants changed within `area`, against the bare canvas.
int marked(const QImage &shot, const QImage &bare, QRect area)
{
    int count = 0;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x)
            count += shot.pixel(x, y) != bare.pixel(x, y);
    }
    return count;
}

// A border band around a document rectangle, in view pixels.
QRect band(Canvas &shown, QRect region)
{
    const QSizeF size = shown.documentSize();
    return QRectF(shown.session.viewport.viewPoint(region.topLeft(), size), shown.session.viewport.viewPoint(region.bottomRight() + QPoint(1, 1), size))
        .toAlignedRect()
        .adjusted(-2, -2, 2, 2);
}

QTimer &ants(Canvas &shown)
{
    return *shown.canvas->findChild<QTimer *>(QStringLiteral("antsTimer"));
}

QRect viewRect(Canvas &shown, QRect region)
{
    const QSizeF size = shown.documentSize();
    return QRectF(shown.session.viewport.viewPoint(region.topLeft(), size), shown.session.viewport.viewPoint(region.bottomRight() + QPoint(1, 1), size))
        .toAlignedRect();
}
}

class MarchingAntsTests : public QObject {
    Q_OBJECT
private slots:
    void aComplexOutlineZoomedOutIsTracedAtScreenResolution();
    void simpleOutlinesAndActualPixelsDrawInFull();
    void aTickWaitsForTheRepaintBeforeIt();
    void aNewSourceDropsTheOldOutline();
    void aNewStepKeepsTheOldOutlineUntilItLands();
    void theStepFollowsTheZoomAndTheFillRuleHolds();
};

void MarchingAntsTests::aComplexOutlineZoomedOutIsTracedAtScreenResolution()
{
    Canvas shown;
    shown.session.zoom(0.25);
    shown.canvas->synchronizeDisplay();
    const QImage bare = shown.canvas->grab().toImage();
    const QRect region(100, 100, 150, 150);
    const QPainterPath board = checkerboard(region);
    QVERIFY(board.elementCount() > 20'000);
    shown.session.setSelection(DocumentSelection{board}, QStringLiteral("Select"));
    shown.canvas->synchronizeDisplay();
    ants(shown).stop();
    // While the first outline is traced, no ants show.
    QCOMPARE(shown.canvas->grab().toImage(), bare);
    QTRY_VERIFY(shown.canvas->grab().toImage() != bare);
    // The trace covers the board: its edge alone shows.
    const QImage shot = shown.canvas->grab().toImage();
    const QRect view = viewRect(shown, region);
    QCOMPARE(marked(shot, bare, view.adjusted(3, 3, -3, -3)), 0);
    QVERIFY(marked(shot, bare, view.adjusted(-2, -2, 2, 2)) > view.width() * 2);
}

void MarchingAntsTests::simpleOutlinesAndActualPixelsDrawInFull()
{
    // At actual pixels the complex outline draws whole, at once.
    Canvas shown;
    const QImage bare = shown.canvas->grab().toImage();
    const QRect region(100, 100, 150, 150);
    shown.session.setSelection(DocumentSelection{checkerboard(region)}, QStringLiteral("Select"));
    shown.canvas->synchronizeDisplay();
    ants(shown).stop();
    const QImage full = shown.canvas->grab().toImage();
    QVERIFY(marked(full, bare, viewRect(shown, region).adjusted(10, 10, -10, -10)) > 1'000);
    // Zoomed out, a marquee's outline draws at once.
    Canvas other;
    other.session.zoom(0.25);
    other.canvas->synchronizeDisplay();
    const QImage plain = other.canvas->grab().toImage();
    other.session.applySelection(rectPath(QRectF(region)), SelectionMode::replace, QStringLiteral("Select"));
    other.canvas->synchronizeDisplay();
    ants(other).stop();
    QVERIFY(other.canvas->grab().toImage() != plain);
}

void MarchingAntsTests::aTickWaitsForTheRepaintBeforeIt()
{
    Canvas first, second;
    for (Canvas *shown : {&first, &second}) {
        shown->session.applySelection(rectPath(QRectF(50, 50, 100, 100)), SelectionMode::replace, QStringLiteral("Select"));
        shown->canvas->synchronizeDisplay();
        ants(*shown).stop();
    }
    QCOMPARE(first.canvas->grab().toImage(), second.canvas->grab().toImage());
    // Two ticks before a repaint step once, as one.
    QVERIFY(QMetaObject::invokeMethod(&ants(first), "timeout"));
    QVERIFY(QMetaObject::invokeMethod(&ants(first), "timeout"));
    QVERIFY(QMetaObject::invokeMethod(&ants(second), "timeout"));
    const QImage stepped = first.canvas->grab().toImage();
    QCOMPARE(stepped, second.canvas->grab().toImage());
    // Once painted, the next tick steps again.
    QVERIFY(QMetaObject::invokeMethod(&ants(first), "timeout"));
    QVERIFY(first.canvas->grab().toImage() != stepped);
}

void MarchingAntsTests::aNewSourceDropsTheOldOutline()
{
    Canvas shown;
    shown.session.zoom(0.25);
    shown.canvas->synchronizeDisplay();
    const QImage bare = shown.canvas->grab().toImage();
    const QRect first(10, 10, 120, 120), second(200, 100, 120, 120);
    shown.session.setSelection(DocumentSelection{checkerboard(first)}, QStringLiteral("Select"));
    shown.canvas->synchronizeDisplay();
    ants(shown).stop();
    QTRY_VERIFY(shown.canvas->grab().toImage() != bare);
    // Another outline: the first goes at once; the second shows.
    shown.session.setSelection(DocumentSelection{checkerboard(second)}, QStringLiteral("Select"));
    shown.canvas->synchronizeDisplay();
    QCOMPARE(shown.canvas->grab().toImage(), bare);
    QTRY_VERIFY(shown.canvas->grab().toImage() != bare);
    const QImage shot = shown.canvas->grab().toImage();
    QCOMPARE(marked(shot, bare, band(shown, first)), 0);
    QVERIFY(marked(shot, bare, band(shown, second)) > 0);
}

void MarchingAntsTests::aNewStepKeepsTheOldOutlineUntilItLands()
{
    Canvas shown;
    shown.session.zoom(0.5);
    shown.canvas->synchronizeDisplay();
    const QImage half = shown.canvas->grab().toImage();
    shown.session.zoom(0.25);
    shown.canvas->synchronizeDisplay();
    const QImage quarter = shown.canvas->grab().toImage();
    const QRect region(10, 10, 120, 120);
    shown.session.setSelection(DocumentSelection{checkerboard(region)}, QStringLiteral("Select"));
    shown.canvas->synchronizeDisplay();
    ants(shown).stop();
    QTRY_VERIFY(shown.canvas->grab().toImage() != quarter);
    // A step in, the quarter's outline stands in meanwhile.
    shown.session.zoom(0.5);
    shown.canvas->synchronizeDisplay();
    QVERIFY(marked(shown.canvas->grab().toImage(), half, band(shown, region)) > 0);
}

void MarchingAntsTests::theStepFollowsTheZoomAndTheFillRuleHolds()
{
    // Boards two pixels apart: a half step keeps the gap.
    Canvas shown;
    shown.session.zoom(0.5);
    shown.canvas->synchronizeDisplay();
    const QImage bare = shown.canvas->grab().toImage();
    QPainterPath boards = checkerboard(QRect(10, 10, 60, 180));
    boards.addPath(checkerboard(QRect(72, 10, 60, 180)));
    shown.session.setSelection(DocumentSelection{boards}, QStringLiteral("Select"));
    shown.canvas->synchronizeDisplay();
    ants(shown).stop();
    QTRY_VERIFY(shown.canvas->grab().toImage() != bare);
    const QSizeF size = shown.documentSize();
    const QPointF gap = shown.session.viewport.viewPoint(QPointF(71, 100), size);
    QVERIFY(marked(shown.canvas->grab().toImage(), bare, QRect(gap.toPoint() - QPoint(1, 20), QSize(3, 40))) > 0);
    // An odd-even ring keeps its hole through the trace.
    Canvas ring;
    ring.session.zoom(0.25);
    ring.canvas->synchronizeDisplay();
    const QImage plain = ring.canvas->grab().toImage();
    QPainterPath holed = checkerboard(QRect(40, 40, 200, 50));
    holed.addRect(QRectF(40, 40, 200, 200));
    holed.addRect(QRectF(100, 100, 80, 80));
    holed.setFillRule(Qt::OddEvenFill);
    ring.session.setSelection(DocumentSelection{holed}, QStringLiteral("Select"));
    ring.canvas->synchronizeDisplay();
    ants(ring).stop();
    QTRY_VERIFY(ring.canvas->grab().toImage() != plain);
    QVERIFY(marked(ring.canvas->grab().toImage(), plain, band(ring, QRect(100, 100, 80, 80))) > 0);
}

QTEST_MAIN(MarchingAntsTests)
#include "MarchingAntsTests.moc"
