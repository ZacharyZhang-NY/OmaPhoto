#include "CanvasFixtures.h"
#include "Document/MagicWand.h"
#include "RenderFixtures.h"
#include "SelectionFixtures.h"
#include "AddressSpaceLimit.h"
#include <malloc.h>
#include <set>

// The Magic Wand: matching, tracing, the session's click, the canvas's.
namespace {
const std::array<uchar, 4> red{255, 0, 0, 255}, blue{0, 0, 255, 255};

// Premultiplied RGBA pixels, top row first.
QImage picture(int width, int height, const std::function<std::array<uchar, 4>(int, int)> &colour)
{
    QImage image = BrushRaster::context(width, height, false);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::array<uchar, 4> pixel = colour(x, y);
            std::copy(pixel.begin(), pixel.end(), image.scanLine(y) + x * 4);
        }
    }
    return image;
}

// Pixel indices (y × width + x) an outline covers.
std::set<int> pixels(const std::optional<QPainterPath> &path, int width, int height)
{
    std::set<int> covered;
    if (!path)
        return covered;
    const QImage coverage = DocumentSelection{path.value(), false}.coverage(width, height);
    for (int index = 0; index < width * height; ++index) {
        if (coverage.constScanLine(index / width)[index % width] >= 128)
            covered.insert(index);
    }
    return covered;
}

WandSettings settings(int tolerance = 32, WandSampleSize size = WandSampleSize::point, bool contiguous = true)
{
    WandSettings result;
    result.tolerance = tolerance;
    result.sampleSize = size;
    result.contiguous = contiguous;
    return result;
}

std::set<int> block(int fromColumn, int toColumn, int fromRow, int toRow, int width)
{
    std::set<int> result;
    for (int y = fromRow; y < toRow; ++y) {
        for (int x = fromColumn; x < toColumn; ++x)
            result.insert(y * width + x);
    }
    return result;
}

std::set<int> all(int count)
{
    std::set<int> result;
    for (int index = 0; index < count; ++index)
        result.insert(index);
    return result;
}

std::set<int> without(std::set<int> from, const std::set<int> &taken)
{
    for (const int index : taken)
        from.erase(index);
    return from;
}

// Runs the wand and waits for its step.
void wand(EditorSession &session, QPointF point, SelectionMode mode)
{
    bool done = false;
    session.magicWand(point, mode, [&] { done = true; });
    QTRY_VERIFY(done);
}
}

class MagicWandTests : public QObject {
    Q_OBJECT
private slots:
    void contiguousStopsAtOtherColorsWhileNonContiguousFindsEveryMatch();
    void toleranceAppliesToEveryChannelIncludingAlpha();
    void sampleSizeAveragesThePixelsAroundTheClick();
    void outlinesReproduceTheirPixelsWithHolesAndCornerTouches();
    void theWandReadsTheActiveLayerOrEveryVisibleLayerAndCombinesModes();
    void clickingInsideASelectionMakesANewWandSelectionRatherThanDeselecting();
    void theWandRefusesOutsideBusyAndMidMoveAndDropsAReplacedDocument();
    void aTooDetailedOutlineIsReportedAndTitlesAreSwifts();
    void thisLayerReadsItsOwnPixelsWhereverAndHoweverTheyShow();
    void aSampleThatCannotFlattenAndAnotherDocumentLeaveTheSelection();
};

void MagicWandTests::contiguousStopsAtOtherColorsWhileNonContiguousFindsEveryMatch()
{
    const QImage stripes = picture(10, 4, [](int x, int) { return x < 3 || x >= 6 ? red : blue; });
    const std::set<int> left = block(0, 3, 0, 4, 10), right = block(6, 10, 0, 4, 10);
    const std::optional<QPainterPath> connected = MagicWand::select(stripes, QPointF(1.5, 2.5), settings());
    QVERIFY(pixels(connected, 10, 4) == left);
    QVERIFY(connected.value().fillRule() == Qt::WindingFill);
    const std::optional<QPainterPath> everywhere = MagicWand::select(stripes, QPointF(1.5, 2.5), settings(32, WandSampleSize::point, false));
    std::set<int> both = left;
    both.insert(right.begin(), right.end());
    QVERIFY(pixels(everywhere, 10, 4) == both);
    // Rows keep their order: the top row selects itself.
    const QImage banded = picture(4, 3, [](int, int y) { return y == 0 ? red : blue; });
    QVERIFY(pixels(MagicWand::select(banded, QPointF(1, 0), settings()), 4, 3) == std::set<int>({0, 1, 2, 3}));
    // The pixel under the point, never the one rounded to.
    QVERIFY(pixels(MagicWand::select(stripes, QPointF(2.7, 0.2), settings()), 10, 4) == left);
    QVERIFY(!MagicWand::select(banded, QPointF(9, 0), settings()).has_value());
    QVERIFY(!MagicWand::select(banded, QPointF(std::nan(""), 0), settings()).has_value());
    QVERIFY(!MagicWand::select(banded, QPointF(-0.5, 0), settings()).has_value());
}

void MagicWandTests::toleranceAppliesToEveryChannelIncludingAlpha()
{
    const std::array<std::array<uchar, 4>, 4> columns{{{100, 100, 100, 255}, {132, 100, 100, 255}, {133, 100, 100, 255}, {100, 100, 100, 222}}};
    const QImage row = picture(4, 1, [&](int x, int) { return columns[size_t(x)]; });
    const auto run = [&](int tolerance) {
        return pixels(MagicWand::select(row, QPointF(0.5, 0.5), settings(tolerance, WandSampleSize::point, false)), 4, 1);
    };
    QVERIFY(run(0) == std::set<int>({0}));
    QVERIFY(run(32) == std::set<int>({0, 1}));
    QVERIFY(run(33) == std::set<int>({0, 1, 2, 3}));
    // A wild tolerance clamps to the range.
    QVERIFY(run(900) == std::set<int>({0, 1, 2, 3}));
    QVERIFY(run(-5) == std::set<int>({0}));
}

void MagicWandTests::sampleSizeAveragesThePixelsAroundTheClick()
{
    const QImage dot = picture(5, 5, [](int x, int y) { return x == 2 && y == 2 ? std::array<uchar, 4>{255, 255, 255, 255} : std::array<uchar, 4>{0, 0, 0, 255}; });
    const QPointF center(2.5, 2.5);
    QVERIFY(pixels(MagicWand::select(dot, center, settings(10, WandSampleSize::point, false)), 5, 5) == std::set<int>({12}));
    // Averaged 3 × 3 to 28: black fits, white not.
    const std::optional<QPainterPath> averaged = MagicWand::select(dot, center, settings(30, WandSampleSize::threeByThree, false));
    QVERIFY(pixels(averaged, 5, 5) == without(all(25), {12}));
    QCOMPARE(radius(WandSampleSize::point), 0);
    QCOMPARE(radius(WandSampleSize::threeByThree), 1);
    QCOMPARE(radius(WandSampleSize::fiveByFive), 2);
}

void MagicWandTests::outlinesReproduceTheirPixelsWithHolesAndCornerTouches()
{
    const int width = 8, height = 6;
    std::vector<uchar> mask(size_t(width * height), 0);
    // A ring round a corner hole; a diagonal touch.
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            if (!(x == 1 && y == 1))
                mask[size_t(y * width + x)] = 255;
        }
    }
    mask[size_t(4 * width + 5)] = 255;
    mask[size_t(5 * width + 6)] = 255;
    std::set<int> expected;
    for (int index = 0; index < width * height; ++index) {
        if (mask[size_t(index)])
            expected.insert(index);
    }
    QVERIFY(pixels(MagicWand::outline(mask, width, height), width, height) == expected);
    QVERIFY(!MagicWand::outline(std::vector<uchar>(4, 0), 2, 2).has_value());
    // A mask of the wrong size outlines nothing.
    QVERIFY(!MagicWand::outline(std::vector<uchar>(3, 255), 2, 2).has_value());
}

void MagicWandTests::theWandReadsTheActiveLayerOrEveryVisibleLayerAndCombinesModes()
{
    EditorSession session;
    session.createDocument(20, 10);
    const QImage halves = picture(20, 10, [](int x, int) { return x < 10 ? red : blue; });
    session.insert(ImportedImage(halves, halves, "Halves"));
    session.addBlankLayer();
    session.selectTool(NavigationTool::wand);
    const std::set<int> left = block(0, 10, 0, 10, 20);
    const auto selected = [&] { return pixels(session.selection() ? std::optional(session.selection().value().path) : std::nullopt, 20, 10); };
    // A blank active layer is clear: the whole canvas matches.
    wand(session, QPointF(2, 2), SelectionMode::replace);
    QCOMPARE(selected().size(), size_t(200));
    QCOMPARE(session.history.undoName(), QString("Magic Wand"));
    QVERIFY(!session.isProjectBusy());
    WandSettings everything = session.wandSettings();
    everything.sampleAllLayers = true;
    session.setWandSettings(everything);
    wand(session, QPointF(2, 2), SelectionMode::replace);
    QVERIFY(selected() == left);
    // A new outline takes the Anti-alias setting of the moment.
    QVERIFY(session.selection().value().antialiased);
    session.setSelectionAntialiased(false);
    wand(session, QPointF(2, 2), SelectionMode::replace);
    QVERIFY(!session.selection().value().antialiased);
    session.setSelectionAntialiased(true);
    const int count = session.history.undoCount();
    wand(session, QPointF(15, 5), SelectionMode::add);
    QCOMPARE(selected().size(), size_t(200));
    QCOMPARE(session.history.undoCount(), count + 1);
    wand(session, QPointF(2, 2), SelectionMode::subtract);
    QVERIFY(selected() == without(all(200), left));
    session.undo();
    QCOMPARE(selected().size(), size_t(200));
    // Hidden, a layer leaves the composite to the one beneath.
    session.toggleLayerVisibility(session.activeLayerID().value());
    session.selectLayer(session.document().value().layers[0].id);
    session.applySelection(rectPath(QRectF(0, 0, 5, 5)), SelectionMode::replace, "Select");
    wand(session, QPointF(2, 2), SelectionMode::replace);
    QVERIFY(selected() == left);
    session.toggleLayerVisibility(session.activeLayerID().value());
    wand(session, QPointF(2, 2), SelectionMode::replace);
    QCOMPARE(selected().size(), size_t(200));
    // A folder as the active layer reads as clear.
    session.setWandSettings(settings());
    session.addGroup();
    wand(session, QPointF(15, 5), SelectionMode::replace);
    QCOMPARE(selected().size(), size_t(200));
    // No match: New deselects, Add changes nothing.
    const auto dotted = std::make_unique<EditorSession>();
    dotted->createDocument(5, 5);
    const QImage dot = picture(5, 5, [](int x, int y) { return x == 2 && y == 2 ? std::array<uchar, 4>{255, 255, 255, 255} : std::array<uchar, 4>{0, 0, 0, 255}; });
    dotted->insert(ImportedImage(dot, dot, "Dot"));
    dotted->setWandSettings(settings(30, WandSampleSize::threeByThree, true));
    dotted->selectAll();
    wand(*dotted, QPointF(2.5, 2.5), SelectionMode::add);
    QVERIFY(dotted->selection().has_value());
    QCOMPARE(dotted->history.undoName(), QString("Select All"));
    wand(*dotted, QPointF(2.5, 2.5), SelectionMode::replace);
    QVERIFY(!dotted->selection().has_value());
    QCOMPARE(dotted->history.undoName(), QString("Deselect"));
}

void MagicWandTests::clickingInsideASelectionMakesANewWandSelectionRatherThanDeselecting()
{
    Shown shown(QSize(20, 10), QSize(400, 200));
    EditorSession &session = shown.session;
    shown.settle();
    const QImage halves = picture(20, 10, [](int x, int) { return x < 10 ? red : blue; });
    session.insert(ImportedImage(halves, halves, "Halves"));
    session.selectTool(NavigationTool::wand);
    WandSettings everything = session.wandSettings();
    everything.sampleAllLayers = true;
    session.setWandSettings(everything);
    session.selectAll();
    shown.canvas->synchronizeDisplay();
    const QPointF spot = session.viewport.viewPoint(QPointF(3.5, 5.5), shown.documentSize());
    QTest::mouseClick(shown.canvas, Qt::LeftButton, Qt::NoModifier, spot.toPoint());
    const std::set<int> left = block(0, 10, 0, 10, 20);
    QTRY_VERIFY(!session.isProjectBusy() && session.selection().has_value() && pixels(session.selection().value().path, 20, 10) == left);
    // A Shift click adds through the wand as well.
    const QPointF other = session.viewport.viewPoint(QPointF(15.5, 5.5), shown.documentSize());
    QTest::mouseClick(shown.canvas, Qt::LeftButton, Qt::ShiftModifier, other.toPoint());
    QTRY_VERIFY(!session.isProjectBusy() && pixels(session.selection().value().path, 20, 10).size() == 200);
    QCOMPARE(session.history.undoName(), QString("Magic Wand"));
}

void MagicWandTests::theWandRefusesOutsideBusyAndMidMoveAndDropsAReplacedDocument()
{
    EditorSession session;
    bool done = false;
    session.magicWand(QPointF(1, 1), SelectionMode::replace, [&] { done = true; });
    QTRY_VERIFY(done);
    session.createDocument(20, 10);
    const QImage halves = picture(20, 10, [](int x, int) { return x < 10 ? red : blue; });
    session.insert(ImportedImage(halves, halves, "Halves"));
    session.selectAll();
    const int count = session.history.undoCount();
    // Outside the canvas, busy, or mid-move: refused, the selection kept.
    for (const QPointF point : {QPointF(-1, 1), QPointF(20, 1), QPointF(1, 10)}) {
        wand(session, point, SelectionMode::replace);
        QCOMPARE(pixels(session.selection().value().path, 20, 10).size(), size_t(200));
    }
    session.setIsProjectBusy(true);
    wand(session, QPointF(1, 1), SelectionMode::replace);
    QCOMPARE(pixels(session.selection().value().path, 20, 10).size(), size_t(200));
    session.setIsProjectBusy(false);
    QVERIFY(session.beginSelectionMove());
    wand(session, QPointF(1, 1), SelectionMode::replace);
    session.endSelectionMove();
    QCOMPARE(pixels(session.selection().value().path, 20, 10).size(), size_t(200));
    QCOMPARE(session.history.undoCount(), count);
    // No number for a point: refused, as one outside.
    wand(session, QPointF(std::nan(""), 1), SelectionMode::replace);
    QCOMPARE(pixels(session.selection().value().path, 20, 10).size(), size_t(200));
    QCOMPARE(session.history.undoCount(), count);
    // The document gone mid-wand: the outline is dropped.
    session.deselect();
    done = false;
    session.magicWand(QPointF(1, 1), SelectionMode::replace, [&] { done = true; });
    QVERIFY(session.isProjectBusy());
    session.clearProject();
    QTRY_VERIFY(done);
    QVERIFY(!session.document().has_value() && !session.isProjectBusy());
}

void MagicWandTests::aTooDetailedOutlineIsReportedAndTitlesAreSwifts()
{
    QCOMPARE(title(WandSampleSize::point), QString("Point Sample"));
    QCOMPARE(title(WandSampleSize::threeByThree), QString("3 by 3 Average"));
    QCOMPARE(title(WandSampleSize::fiveByFive), QString("5 by 5 Average"));
    QCOMPARE(QString::fromUtf8(MagicWandError(MagicWandError::Kind::memory).what()), QString("There isn’t enough memory to make that selection."));
    // A checkerboard of eight million pixels has too many edges.
    QImage board(4000, 2000, QImage::Format_RGBA8888_Premultiplied);
    for (int y = 0; y < 2000; ++y) {
        uchar *row = board.scanLine(y);
        for (int x = 0; x < 4000; ++x) {
            const uchar value = (x + y) % 2 ? 255 : 0;
            row[x * 4] = value;
            row[x * 4 + 1] = value;
            row[x * 4 + 2] = value;
            row[x * 4 + 3] = 255;
        }
    }
    QVERIFY_THROWS_EXCEPTION(MagicWandError, MagicWand::select(board, QPointF(0.5, 0.5), settings(0, WandSampleSize::point, false)));
    EditorSession session;
    session.createDocument(4000, 2000);
    session.insert(ImportedImage(board, QImage(), "Board"));
    session.setWandSettings(settings(0, WandSampleSize::point, false));
    wand(session, QPointF(0.5, 0.5), SelectionMode::replace);
    QVERIFY(!session.selection().has_value() && !session.isProjectBusy());
    QCOMPARE(session.brushError(), std::optional(QString("That selection is too detailed to outline. Try a different Tolerance, or turn on Contiguous.")));
}

void MagicWandTests::thisLayerReadsItsOwnPixelsWhereverAndHoweverTheyShow()
{
    EditorSession session;
    session.createDocument(30, 20);
    const QImage square = picture(10, 5, [](int, int) { return red; });
    session.insert(ImportedImage(square, square, "Red"));
    const QUuid id = session.activeLayerID().value();
    // Moved, scaled, hidden, masked, faint: its pixels still read.
    QImage mask = BrushRaster::context(10, 5, true);
    mask.fill(Qt::black);
    rewrite(session, [&](ProjectSnapshot &snapshot) {
        record(snapshot, id).transform = LayerTransform{.origin = {5, 2}, .size = {20, 10}, .sampling = LayerSampling::nearest};
        record(snapshot, id).isVisible = false;
        record(snapshot, id).opacity = 0.2;
        setMask(snapshot, id, LayerMask::assetFrom(mask));
    });
    session.selectTool(NavigationTool::wand);
    session.setWandSettings(settings(0, WandSampleSize::point, false));
    wand(session, QPointF(10, 5), SelectionMode::replace);
    QVERIFY(pixels(session.selection().value().path, 30, 20) == block(5, 25, 2, 12, 30));
    // Outside its placed pixels, the layer alone reads as clear.
    session.setWandSettings(settings(0, WandSampleSize::point, true));
    wand(session, QPointF(1, 1), SelectionMode::replace);
    QVERIFY(pixels(session.selection().value().path, 30, 20) == without(all(600), block(5, 25, 2, 12, 30)));
    // Its pixels read opaque, not within 200 of clear.
    session.setWandSettings(settings(200, WandSampleSize::point, false));
    wand(session, QPointF(1, 1), SelectionMode::replace);
    QVERIFY(pixels(session.selection().value().path, 30, 20) == without(all(600), block(5, 25, 2, 12, 30)));
}

void MagicWandTests::aSampleThatCannotFlattenAndAnotherDocumentLeaveTheSelection()
{
    // A painted base on a tiny canvas: only it fails.
    const auto raster = std::make_shared<const RasterSnapshot>(4000, 3000, solid(2, 2, qRgba(255, 0, 0, 255)), QRectF(0, 0, 2, 2), std::vector<BrushPatch>{});
    EditorSession session;
    session.createDocument(20, 10);
    session.insert(ImportedImage(raster, QImage(), "Painted"));
    session.applySelection(rectPath(QRectF(0, 0, 10, 10)), SelectionMode::replace, "Select");
    const int count = session.history.undoCount();
    bool done = false;
    {
        // Freed heap returns first: the room is real.
        malloc_trim(0);
        const AddressSpaceLimit limit(16 * 1024 * 1024);
        session.magicWand(QPointF(1, 1), SelectionMode::replace, [&] { done = true; });
    }
    QTRY_VERIFY(done);
    QVERIFY(!session.isProjectBusy() && !raster->hasMaterializedPixels());
    QCOMPARE(session.history.undoCount(), count);
    QCOMPARE(bounds(session), QRectF(0, 0, 10, 10));
    // Another document installed mid-wand: its outline never lands.
    const auto small = std::make_unique<EditorSession>();
    small->createDocument(20, 10);
    const QImage halves = picture(20, 10, [](int x, int) { return x < 10 ? red : blue; });
    small->insert(ImportedImage(halves, halves, "Halves"));
    done = false;
    small->magicWand(QPointF(1, 1), SelectionMode::replace, [&] { done = true; });
    QVERIFY(small->isProjectBusy());
    rewrite(*small, [](ProjectSnapshot &snapshot) { snapshot.manifest.documentID = QUuid::createUuid(); });
    QTRY_VERIFY(done);
    QVERIFY(!small->selection().has_value() && !small->isProjectBusy());
    QCOMPARE(small->history.undoCount(), 0);
}

QTEST_MAIN(MagicWandTests)
#include "MagicWandTests.moc"
