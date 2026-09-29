#include "BrushFixtures.h"
#include "SelectionFixtures.h"
#include "SessionFixtures.h"
#include "Rendering/EditorCanvas.h"
#include "Rendering/RasterSnapshot.h"
#include <QtTest>

// Swift 1.2.11: a mask painted anywhere on the canvas grows.
namespace {
// A blue square on a wide canvas, its mask chosen.
std::unique_ptr<EditorSession> masked(bool reveal, QSize canvas = QSize(100, 60))
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(canvas.width(), canvas.height(), true);
    QImage blue = BrushRaster::context(20, 20, false);
    blue.fill(Qt::blue);
    session->addPixelLayer(blue, QPointF(20, 20), QStringLiteral("Blue"), QStringLiteral("Add"));
    session->addLayerMask(reveal);
    session->selectLayerTarget(session->activeLayerID().value(), true);
    session->selectTool(NavigationTool::brush);
    session->setBrushSettings(brush(6, 1, 0, 0, 0));
    return session;
}

// The mask's value at a document point, through its placement.
int maskAt(const EditorSession &session, QPointF point)
{
    const LayerMask mask = session.activeLayer().value().mask.value();
    const LayerTransform placement = mask.placement.value();
    const QImage image = mask.asset.image();
    const QPointF pixel = BrushRaster::pixelToDocument(placement, image.width(), image.height()).inverted().map(point);
    return qGray(image.pixel(int(pixel.x()), int(pixel.y())));
}
}

class MaskPaintAnywhereTests : public QObject {
    Q_OBJECT
private slots:
    void theBrushGrowsAMaskPastItsLayer();
    void newAreaStartsAsTheMasksBackground();
    void gradientsAndFillsGrowAMaskToo();
    void otherToolsStayWithinTheMask();
    void aSolidPlacedMaskGetsAPixelPerDocumentPixel();
    void aSnapshotAndACommitFillWithTheBackground();
    void theCanvasDrawsAGrownStrokeInPlace();
    void uncoveredCornersKeepTheBackground();
    void aGrownPlacedMaskMapsItsTilesUnderEffects();
};

void MaskPaintAnywhereTests::theBrushGrowsAMaskPastItsLayer()
{
    const auto session = masked(true);
    const QUuid id = session->activeLayerID().value();
    session->beginBrush(QPointF(70, 30));
    session->continueBrush(QPointF(80, 30));
    // The stroke's grid reaches the canvas, not just the layer.
    QVERIFY(session->brushStroke()->width >= 100);
    session->finishBrush();
    const LayerMask mask = layerWith(*session, id).mask.value();
    // Grown, it keeps its place on the document, still linked.
    QVERIFY(mask.placement);
    QVERIFY(mask.isLinked);
    QVERIFY(mask.asset.size().width() > 20);
    QCOMPARE(maskAt(*session, QPointF(75, 30)), 0);
    QCOMPARE(maskAt(*session, QPointF(30, 30)), 255);
    // Untouched in a touched tile: the background, white.
    QCOMPARE(maskAt(*session, QPointF(90, 50)), 255);
    // A linked mask still moves with its layer.
    const QPointF before = mask.placement.value().origin;
    session->selectTool(NavigationTool::move);
    session->nudgeLayer(5, 0);
    QCOMPARE(layerWith(*session, id).mask.value().placement.value().origin, before + QPointF(5, 0));
}

void MaskPaintAnywhereTests::newAreaStartsAsTheMasksBackground()
{
    // Hidden everywhere: new area hides, only the paint shows.
    const auto session = masked(false);
    session->setMaskPaintWhite(true);
    session->setBrushSettings(brush(6, 1, 1, 1, 1));
    session->beginBrush(QPointF(70, 30));
    session->continueBrush(QPointF(80, 30));
    session->finishBrush();
    QCOMPARE(maskAt(*session, QPointF(75, 30)), 255);
    QCOMPARE(maskAt(*session, QPointF(90, 50)), 0);
    QCOMPARE(maskAt(*session, QPointF(30, 30)), 0);
}

void MaskPaintAnywhereTests::gradientsAndFillsGrowAMaskToo()
{
    const auto gradient = masked(true);
    gradient->selectTool(NavigationTool::gradient);
    gradient->beginGradient(QPointF(0, 30));
    gradient->moveGradient(std::nullopt, QPointF(100, 30));
    bool done = false;
    gradient->commitGradient([&] { done = true; });
    QVERIFY(QTest::qWaitFor([&] { return done; }, 5000));
    QVERIFY(gradient->activeLayer().value().mask.value().placement);
    QVERIFY(gradient->activeLayer().value().mask.value().asset.size().width() >= 100);
    // The Smear paints within the mask's own grid.
    const auto fill = masked(true);
    fill->applySelection(rectPath(QRectF(60, 10, 30, 30)), SelectionMode::replace, QStringLiteral("Select"));
    fill->selectLayerTarget(fill->activeLayerID().value(), true);
    bool filled = false;
    fill->fillSelection(EditorSession::FillSource::foreground, [&] { filled = true; });
    QVERIFY(QTest::qWaitFor([&] { return filled; }, 5000));
    QCOMPARE(maskAt(*fill, QPointF(75, 25)), 0);
    QCOMPARE(maskAt(*fill, QPointF(30, 30)), 255);
}

void MaskPaintAnywhereTests::otherToolsStayWithinTheMask()
{
    // The Smear's blur paints within the mask's own grid.
    const auto session = masked(true);
    session->selectTool(NavigationTool::blur);
    session->setBlurMode(BlurToolMode::blur);
    session->beginBrush(QPointF(30, 30));
    QVERIFY(session->brushStroke() && session->brushStroke()->isBlur);
    QCOMPARE(session->brushStroke()->width, 20);
    session->cancelBrush();
}

void MaskPaintAnywhereTests::aSolidPlacedMaskGetsAPixelPerDocumentPixel()
{
    // Two-pixel masks are solid; this placement is 20 by 10.
    const auto session = masked(true);
    const QUuid id = session->activeLayerID().value();
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        ProjectLayerRecord &layer = record(snapshot, id);
        QImage white = BrushRaster::context(2, 2, true);
        white.fill(255);
        snapshot.masks.insert_or_assign(id, ImportedImage(white, white, QStringLiteral("Mask")));
        layer.maskPlacement = LayerTransform{.origin = QPointF(20, 20), .size = QSizeF(20, 10)};
        layer.maskLinked = false;
    });
    session->selectLayerTarget(id, true);
    QCOMPARE(layerWith(*session, id).mask.value().asset.size(), QSize(2, 2));
    // Without growth the grid is the box, a pixel each.
    session->selectTool(NavigationTool::blur);
    session->setBlurMode(BlurToolMode::blur);
    session->beginBrush(QPointF(25, 25));
    QCOMPARE(session->brushStroke()->width, 20);
    QCOMPARE(session->brushStroke()->height, 10);
    session->cancelBrush();
    // The brush grows it, still a pixel per document pixel.
    session->selectTool(NavigationTool::brush);
    session->beginBrush(QPointF(30, 25));
    QCOMPARE(session->brushStroke()->width, 100);
    QCOMPARE(session->brushStroke()->height, 60);
    session->finishBrush();
    QCOMPARE(maskAt(*session, QPointF(30, 25)), 0);
    QCOMPARE(maskAt(*session, QPointF(22, 22)), 255);
}

void MaskPaintAnywhereTests::aSnapshotAndACommitFillWithTheBackground()
{
    // Past its base a mask snapshot is its fill.
    QImage base = BrushRaster::context(4, 4, true);
    base.fill(255);
    const auto snapshot = RasterSnapshot::replacing(ImportedImage(base, base, QStringLiteral("Mask")), QRectF(2, 2, 4, 4), {}, QRectF(0, 0, 8, 8), true, 0);
    const QImage image = snapshot->makeImage();
    QCOMPARE(qGray(image.pixel(0, 0)), 0);
    QCOMPARE(qGray(image.pixel(3, 3)), 255);
    const BrushCommit::Output output = BrushCommit::render(BrushCommit::Input{8, 8, ImportedImage(base, base, QStringLiteral("Mask")), {}, true,
                                                                              QStringLiteral("Mask"), QRectF(2, 2, 4, 4), 0});
    QCOMPARE(qGray(output.asset.image().pixel(0, 0)), 0);
    QCOMPARE(qGray(output.asset.image().pixel(3, 3)), 255);
}

void MaskPaintAnywhereTests::theCanvasDrawsAGrownStrokeInPlace()
{
    const auto session = masked(true);
    CanvasView canvas(*session);
    canvas.resize(100, 60);
    session->viewport.resize(QSizeF(100, 60), 1, QSizeF(100, 60));
    session->zoom(1);
    session->beginBrush(QPointF(70, 30));
    session->continueBrush(QPointF(80, 30));
    const QImage shot = canvas.grab().toImage();
    const auto at = [&](QPointF point) { return shot.pixelColor(session->viewport.viewPoint(point, QSizeF(100, 60)).toPoint()); };
    // Mid-stroke the layer stays where it is, whole.
    QCOMPARE(at(QPointF(21.5, 21.5)), QColor(Qt::blue));
    QCOMPARE(at(QPointF(38.5, 38.5)), QColor(Qt::blue));
    QVERIFY(at(QPointF(45.5, 30.5)) != QColor(Qt::blue));
}

void MaskPaintAnywhereTests::uncoveredCornersKeepTheBackground()
{
    // Tiles apart: the grown box holds corners no tile covers.
    const auto brushed = masked(false, QSize(600, 400));
    brushed->setMaskPaintWhite(true);
    brushed->setBrushSettings(brush(6, 1, 1, 1, 1));
    brushed->beginBrush(QPointF(500, 350));
    brushed->continueBrush(QPointF(510, 350));
    brushed->finishBrush();
    QCOMPARE(maskAt(*brushed, QPointF(505, 350)), 255);
    QCOMPARE(maskAt(*brushed, QPointF(300, 100)), 0);
    // A fill, committed off the thread, keeps it too.
    const auto filled = masked(false, QSize(600, 400));
    filled->applySelection(rectPath(QRectF(500, 340, 20, 20)), SelectionMode::replace, QStringLiteral("Select"));
    filled->selectLayerTarget(filled->activeLayerID().value(), true);
    filled->setMaskPaintWhite(true);
    bool done = false;
    filled->fillSelection(EditorSession::FillSource::foreground, [&] { done = true; });
    QVERIFY(QTest::qWaitFor([&] { return done; }, 5000));
    QCOMPARE(maskAt(*filled, QPointF(510, 350)), 255);
    QCOMPARE(maskAt(*filled, QPointF(300, 100)), 0);
    // Revealing everywhere, the corner stays white.
    const auto revealed = masked(true, QSize(600, 400));
    revealed->applySelection(rectPath(QRectF(500, 340, 20, 20)), SelectionMode::replace, QStringLiteral("Select"));
    revealed->selectLayerTarget(revealed->activeLayerID().value(), true);
    done = false;
    revealed->fillSelection(EditorSession::FillSource::foreground, [&] { done = true; });
    QVERIFY(QTest::qWaitFor([&] { return done; }, 5000));
    QCOMPARE(maskAt(*revealed, QPointF(510, 350)), 0);
    QCOMPARE(maskAt(*revealed, QPointF(300, 100)), 255);
}

void MaskPaintAnywhereTests::aGrownPlacedMaskMapsItsTilesUnderEffects()
{
    // A wide layer; its solid placed mask grows wide.
    auto session = std::make_unique<EditorSession>();
    session->createDocument(1200, 100, true);
    QImage blue = BrushRaster::context(200, 40, false);
    blue.fill(Qt::blue);
    session->addPixelLayer(blue, QPointF(100, 30), QStringLiteral("Blue"), QStringLiteral("Add"));
    const QUuid id = session->activeLayerID().value();
    LayerEffects effects;
    effects.stroke = StrokeEffect{.size = 2, .red = 1, .green = 1, .blue = 0, .opacity = 1};
    session->setEffects(effects);
    session->addLayerMask(true);
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        ProjectLayerRecord &layer = record(snapshot, id);
        layer.maskPlacement = layer.transform;
        layer.maskLinked = false;
    });
    session->selectLayerTarget(id, true);
    session->selectTool(NavigationTool::brush);
    session->setBrushSettings(brush(10, 1, 0, 0, 0));
    CanvasView canvas(*session);
    canvas.resize(1200, 100);
    session->viewport.resize(QSizeF(1200, 100), 1, QSizeF(1200, 100));
    session->zoom(1);
    // A first pass at the left end, then the right.
    session->beginBrush(QPointF(110, 50));
    canvas.grab();
    session->continueBrush(QPointF(110, 51));
    canvas.grab();
    session->continueBrush(QPointF(280, 50));
    const QImage shot = canvas.grab().toImage();
    const auto at = [&](QPointF point) { return shot.pixelColor(session->viewport.viewPoint(point, QSizeF(1200, 100)).toPoint()); };
    QVERIFY2(at(QPointF(280.5, 50.5)) != QColor(Qt::blue), qPrintable(at(QPointF(280.5, 50.5)).name()));
    QCOMPARE(at(QPointF(200.5, 35.5)), QColor(Qt::blue));
}

QTEST_MAIN(MaskPaintAnywhereTests)
#include "MaskPaintAnywhereTests.moc"
