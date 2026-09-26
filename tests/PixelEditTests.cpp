#include "BrushFixtures.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore.h"
#include "SelectionFixtures.h"
#include "Rendering/RasterSnapshot.h"
#include "SessionRecord.h"
#include <QTemporaryDir>

// The session's pixel edits: strokes, erase, clears, fills.
namespace {
// Swift's makeSession: a blank layer, the Brush, hard red 20.
std::unique_ptr<EditorSession> makeSession(int width = 600, int height = 80)
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(width, height, true);
    session->addBlankLayer();
    session->selectTool(NavigationTool::brush);
    session->setBrushSettings(brush(20, 1, 1, 0, 0));
    return session;
}

QImage render(const EditorSession &session)
{
    return ImageExporter::render(session.projectSnapshot().value()).image;
}

QImage live(const EditorSession &session)
{
    return preview(*session.brushStroke(), session.document().value().size());
}

void select(EditorSession &session, const QRectF &rect)
{
    session.applySelection(rectPath(rect), SelectionMode::replace, "Select");
}
}

class PixelEditTests : public QObject {
    Q_OBJECT
private slots:
    void aStrokeHoldsTheSessionAndShiftLinesStartWhereItEnded();
    void eraseClearsAndDeleteFillsAndClears();
    void brushCallsAnnounceAndRefuseInSilence();
    void aRasterEditLandsOnlyOnTheLayerItReadAndTheBudgetCountsEveryLayer();
    void aMaskSwitchedOffMeanwhileStaysOff();
};

void PixelEditTests::aStrokeHoldsTheSessionAndShiftLinesStartWhereItEnded()
{
    const auto session = makeSession();
    const QUuid id = session->activeLayerID().value();
    QVERIFY(!session->shiftLineStart().has_value());
    session->addLayerMask(true);
    session->selectLayerTarget(id, false);
    session->beginBrush(QPointF(20, 40));
    // While the stroke lasts nothing else moves.
    QVERIFY(!session->canEditLayers() && !session->canUseHistory() && !session->canStartProjectOperation());
    QVERIFY(!session->canInvert() && !session->canAdjustColors() && !session->canContentAwareFill());
    const int count = session->history.undoCount();
    bool inverted = false;
    session->invertPixels([&] { inverted = true; });
    QTRY_VERIFY(inverted);
    QVERIFY(session->brushStroke() && session->history.undoCount() == count);
    session->selectTool(NavigationTool::move);
    QCOMPARE(session->tool(), NavigationTool::brush);
    session->selectLayer(session->document().value().layers.front().id);
    QCOMPARE(session->activeLayerID(), std::optional(id));
    session->selectLayerTarget(id, true);
    QVERIFY(!session->isMaskSelected());
    session->continueBrush(QPointF(100, 40));
    QCOMPARE(session->shiftLineStart(), std::optional(QPointF(100, 40)));
    // Busy, the mouse-up waits; free, it commits.
    session->setIsProjectBusy(true);
    QVERIFY(!session->finishBrushImmediately());
    session->setIsProjectBusy(false);
    QVERIFY(session->finishBrushImmediately());
    QVERIFY(session->finishBrushImmediately());
    QCOMPARE(session->shiftLineStart(), std::optional(QPointF(100, 40)));
    session->selectLayer(session->document().value().layers.front().id);
    QVERIFY(!session->shiftLineStart().has_value());
    session->selectLayer(id);
    session->selectLayerTarget(id, true);
    QVERIFY(session->isMaskSelected() && !session->shiftLineStart().has_value());
    // A Shift-click's line: the old end to the new point.
    session->selectLayerTarget(id, false);
    session->beginBrush(session->shiftLineStart().value());
    session->continueBrush(QPointF(300, 40));
    session->finishBrush();
    QCOMPARE(alpha(render(*session), 200, 40), 255);
    // Two selected layers paint nothing; an import ends a stroke.
    const QUuid other = session->document().value().layers.front().id;
    session->selectLayers({id, other}, id);
    QVERIFY(!session->canPaint());
    session->selectLayer(id);
    QTemporaryDir folder;
    const QString path = folder.filePath(QStringLiteral("dot.png"));
    QImage dot(2, 2, QImage::Format_RGBA8888_Premultiplied);
    dot.fill(Qt::green);
    QVERIFY(dot.save(path));
    session->beginBrush(QPointF(400, 40));
    bool imported = false;
    session->importImages({QUrl::fromLocalFile(path)}, std::nullopt, [&] { imported = true; });
    QVERIFY(!session->brushStroke());
    QCOMPARE(session->history.undoName(), QString("Brush Stroke"));
    QTRY_VERIFY(imported);
    QCOMPARE(session->activeLayer().value().name, QString("dot"));
}

void PixelEditTests::eraseClearsAndDeleteFillsAndClears()
{
    const auto session = makeSession(20, 10);
    QImage red = BrushRaster::context(20, 10, false);
    red.fill(Qt::red);
    session->insert(ImportedImage(red, red, "Red"));
    session->selectTool(NavigationTool::brush);
    // Pixels to invert or fill, refused while a stroke lasts.
    QVERIFY(session->canInvert() && session->canAdjustColors());
    session->beginBrush(QPointF(15, 5));
    QVERIFY(!session->canInvert() && !session->canAdjustColors());
    session->cancelBrush();
    QVERIFY(session->canInvert() && session->canAdjustColors());
    // Erase takes the pixels out; the mask keeps painting.
    session->setBrushMode(BrushToolMode::erase);
    session->setBrushSettings(brush(4, 1, 0, 0, 1));
    session->beginBrush(QPointF(5, 5));
    QCOMPARE(alpha(live(*session), 5, 5), 0);
    session->finishBrush();
    QCOMPARE(session->history.undoName(), QString("Erase"));
    QCOMPARE(alpha(render(*session), 5, 5), 0);
    QCOMPARE(pixel(render(*session), 15, 5), (std::vector<int>{255, 0, 0, 255}));
    session->undo();
    // Delete with a selection clears; without, it deletes.
    session->selectTool(NavigationTool::marquee);
    select(*session, QRectF(0, 0, 10, 10));
    bool done = false;
    session->clearSelectedPixels([&] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(session->history.undoName(), QString("Clear"));
    QCOMPARE(alpha(render(*session), 5, 5), 0);
    QCOMPARE(pixel(render(*session), 15, 5), (std::vector<int>{255, 0, 0, 255}));
    session->undo();
    done = false;
    session->fillSelection(EditorSession::FillSource::background, [&] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(session->history.undoName(), QString("Fill"));
    QCOMPARE(pixel(render(*session), 5, 5), (std::vector<int>{255, 255, 255, 255}));
    session->undo();
    session->deleteKeyPressed();
    QTRY_COMPARE(session->history.undoName(), QString("Clear"));
    session->deselect();
    const int layers = int(session->document().value().layers.size());
    session->deleteKeyPressed();
    QCOMPARE(int(session->document().value().layers.size()), layers - 1);
    // Back past the delete, deselect and clear: red again.
    session->undo();
    session->undo();
    session->undo();
    QCOMPARE(pixel(render(*session), 5, 5), (std::vector<int>{255, 0, 0, 255}));
    // A mask target fills with the background: black's is white.
    session->selectLayer(session->document().value().layers.back().id);
    session->addLayerMask(true);
    select(*session, QRectF(0, 0, 10, 10));
    session->setMaskPaintWhite(false);
    done = false;
    session->clearSelectedPixels([&] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(session->history.undoName(), QString("Fill Mask"));
    QCOMPARE(alpha(render(*session), 5, 5), 255);
    session->setMaskPaintWhite(true);
    done = false;
    session->clearSelectedPixels([&] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(alpha(render(*session), 5, 5), 0);
}

void PixelEditTests::brushCallsAnnounceAndRefuseInSilence()
{
    const auto session = makeSession();
    QSignalSpy changes(session.get(), &EditorSession::changed);
    const auto announced = [&](const std::function<void()> &change) {
        const qsizetype before = changes.count();
        change();
        return changes.count() > before;
    };
    QVERIFY(!announced([&] { session->continueBrush(QPointF(1, 1)); }));
    QVERIFY(announced([&] { session->changeBrushSize(true); }));
    QVERIFY(announced([&] { session->setBrushMode(BrushToolMode::erase); }));
    QVERIFY(announced([&] { session->setMaskPaintWhite(true); }));
    QVERIFY(announced([&] { session->typeOpacityDigit(5, 1); }));
    QVERIFY(announced([&] { session->beginBrush(QPointF(10, 10)); }));
    QVERIFY(!announced([&] { session->selectTool(NavigationTool::move); }));
    QVERIFY(!announced([&] { session->typeOpacityDigit(5, 2); }));
    QVERIFY(announced([&] { session->continueBrush(QPointF(20, 10)); }));
    QVERIFY(announced([&] { session->cancelBrush(); }));
    // The last signal of a change sees what it leaves.
    QStringList seen;
    connect(session.get(), &EditorSession::changed, session.get(), [&] { seen = described(*session); });
    session->beginBrush(QPointF(10, 10));
    QCOMPARE(seen, described(*session));
    session->continueBrush(QPointF(50, 10));
    QCOMPARE(seen, described(*session));
    session->finishBrush();
    QCOMPARE(seen, described(*session));
    session->changeBrushHardness(false);
    QCOMPARE(seen, described(*session));
}

void PixelEditTests::aRasterEditLandsOnlyOnTheLayerItReadAndTheBudgetCountsEveryLayer()
{
    const auto session = makeSession(20, 10);
    QImage red = BrushRaster::context(20, 10, false);
    red.fill(Qt::red);
    session->insert(ImportedImage(red, red, "Red"));
    const QUuid id = session->activeLayerID().value();
    QImage white = BrushRaster::context(20, 10, true);
    white.fill(Qt::white);
    rewrite(*session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, id, LayerMask::assetFrom(white)); });
    session->selectLayerTarget(id, false);
    select(*session, QRectF(0, 0, 10, 10));
    // The mask replaced while the clear flies: nothing lands.
    bool done = false;
    session->clearSelectedPixels([&] { done = true; });
    QVERIFY(session->isProjectBusy());
    QImage black = BrushRaster::context(20, 10, true);
    rewrite(*session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, id, LayerMask::assetFrom(black)); });
    QTRY_VERIFY(done);
    QVERIFY(!session->isProjectBusy());
    QCOMPARE(session->history.undoCount(), 0);
    QCOMPARE(int(layerWith(*session, id).mask.value().asset.image().constScanLine(5)[5]), 0);
    QCOMPARE(pixel(layerWith(*session, id).asset.value().image(), 5, 5), (std::vector<int>{255, 0, 0, 255}));
    // Six layers of 16 megapixels leave a stroke 4 million.
    const auto big = std::make_unique<EditorSession>();
    big->createDocument(4000, 4000, true);
    for (int i = 0; i < 6; ++i) {
        const auto raster = std::make_shared<const RasterSnapshot>(4000, 4000, BrushRaster::context(1, 1, false), QRectF(0, 0, 4000, 4000), std::vector<BrushPatch>{});
        big->insert(ImportedImage(raster, QImage(), QStringLiteral("Big %1").arg(i)));
    }
    big->addBlankLayer();
    big->selectTool(NavigationTool::brush);
    QCOMPARE(big->makeRasterEdit(big->activeLayer().value())->pixelLimit, qint64(4'000'000));
    // A 2000 px tip needs more tiles than that: refused.
    big->setBrushSettings(brush(2000, 1, 1, 0, 0));
    big->beginBrush(QPointF(2000, 2000));
    QVERIFY(!big->brushStroke() && big->brushError().has_value());
    big->setBrushError(std::nullopt);
    big->setBrushSettings(brush(20, 1, 1, 0, 0));
    big->beginBrush(QPointF(2000, 2000));
    QVERIFY(big->brushStroke());
    big->cancelBrush();
    // A masked layer's budget counts the other masks too.
    QImage mask = BrushRaster::context(4000, 4000, true);
    const QUuid last = big->activeLayerID().value();
    rewrite(*big, [&](ProjectSnapshot &snapshot) {
        for (ProjectLayerRecord &record : snapshot.manifest.layers)
            setMask(snapshot, record.id, LayerMask::assetFrom(mask));
    });
    big->selectLayerTarget(last, false);
    // Seven other 16-million masks: the budget runs below zero.
    QCOMPARE(big->makeRasterEdit(big->activeLayer().value())->pixelLimit, qint64(-12'000'000));
}

void PixelEditTests::aMaskSwitchedOffMeanwhileStaysOff()
{
    const auto session = makeSession(20, 10);
    QImage red = BrushRaster::context(20, 10, false);
    red.fill(Qt::red);
    session->insert(ImportedImage(red, red, "Red"));
    const QUuid id = session->activeLayerID().value();
    QImage white = BrushRaster::context(20, 10, true);
    white.fill(Qt::white);
    rewrite(*session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, id, LayerMask::assetFrom(white)); });
    session->selectLayerTarget(id, false);
    select(*session, QRectF(0, 0, 10, 10));
    bool done = false;
    session->clearSelectedPixels([&] { done = true; });
    QVERIFY(session->isProjectBusy());
    // Only the switch changes meanwhile: same pixels, same place.
    rewrite(*session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).maskEnabled = false; });
    QTRY_VERIFY(done);
    QVERIFY(!session->isProjectBusy());
    QCOMPARE(session->history.undoCount(), 1);
    QCOMPARE(session->history.undoName(), QString("Clear"));
    QVERIFY(!layerWith(*session, id).mask.value().isEnabled);
    // The commit crops the cleared half away: read the document.
    QCOMPARE(pixel(render(*session), 5, 5)[3], 0);
    QCOMPARE(pixel(render(*session), 15, 5), (std::vector<int>{255, 0, 0, 255}));
}

QTEST_GUILESS_MAIN(PixelEditTests)
#include "PixelEditTests.moc"
