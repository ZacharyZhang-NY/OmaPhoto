#include "CanvasFixtures.h"
#include "Document/Filters.h"
#include "MenuFixtures.h"
#include "SessionFixtures.h"
#include <QPainter>

extern "C" {
#include "AdjustPixels.h"
}

// Swift 1.2.5: Vignette round by default, painting an empty layer.
namespace {
bool prepared(EditorSession &session)
{
    return QTest::qWaitFor([&] { return session.filterEdit() && !session.filterEdit().value().preparing; }, 20'000);
}

bool committed(EditorSession &session)
{
    bool done = false;
    session.commitFilter([&done] { done = true; });
    return QTest::qWaitFor([&done] { return done; }, 20'000);
}
}

class VignetteCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void clearPixelsTakeTheColourTowardTheFramesEdge();
    void theCanvasFramesALayerThatSitsInsideIt();
    void onlyVignettePaintsAnEmptyLayer();
    void anEmptyLayerTakesTheVignetteAcrossTheCanvas();
    void theCanvasShowsThePreviewOnAnEmptyLayer();
    void aLayerWithPixelsKeepsItsOwnFrameAndClear();
    void aSmallEmptyLayerGrowsToTheCanvas();
    void theFrameIsReadTopDown();
};

void VignetteCanvasTests::clearPixelsTakeTheColourTowardTheFramesEdge()
{
    QImage clear = BrushRaster::context(41, 41, false);
    // Without fillsClear, clear pixels stay as they are.
    QImage kept = clear.copy();
    adjust_colored_vignette(kept.bits(), 41, 41, size_t(kept.bytesPerLine()), 0, 0, 41, 41, 0, 100, 50, 100, 60, 25, 1, 0, 0);
    QCOMPARE(kept, clear);
    adjust_colored_vignette(clear.bits(), 41, 41, size_t(clear.bytesPerLine()), 0, 0, 41, 41, 1, 100, 50, 100, 60, 25, 1, 0, 0);
    // The middle stays clear; the corner is red, near opaque.
    QCOMPARE(clear.pixel(20, 20), qRgba(0, 0, 0, 0));
    const QRgb corner = clear.pixel(0, 0);
    QVERIFY(qAlpha(corner) > 240 && qRed(corner) == qAlpha(corner) && qGreen(corner) == 0 && qBlue(corner) == 0);
}

void VignetteCanvasTests::theCanvasFramesALayerThatSitsInsideIt()
{
    // A 20-pixel layer at (10, 10) of a 40-pixel canvas.
    const QImage clear = BrushRaster::context(20, 20, false);
    FilterSettings settings;
    settings.vignetteAmount = 100;
    FilterJob job{FilterKind::vignette, clear, settings, 1, std::nullopt, QTransform::fromTranslate(10, 10)};
    job.canvas = QRectF(0, 0, 40, 40);
    QImage expected = clear.copy();
    adjust_colored_vignette(expected.bits(), 20, 20, size_t(expected.bytesPerLine()), -10, -10, 40, 40, 1, 100, 50, 100, 60, 25, 0, 0, 0);
    QCOMPARE(PixelFilter::run(job), expected);
    // Its own corner sits inside the frame: fainter.
    QImage framedAlone = clear.copy();
    adjust_colored_vignette(framedAlone.bits(), 20, 20, size_t(framedAlone.bytesPerLine()), 0, 0, 20, 20, 1, 100, 50, 100, 60, 25, 0, 0, 0);
    QVERIFY(qAlpha(expected.pixel(0, 0)) < qAlpha(framedAlone.pixel(0, 0)));
}

void VignetteCanvasTests::onlyVignettePaintsAnEmptyLayer()
{
    Bar bar;
    EditorSession &session = bar.session();
    session.createDocument(40, 30);
    session.addBlankLayer();
    QVERIFY(!session.canAdjustColors() && session.canVignette());
    QVERIFY(bar.action("vignette").isEnabled() && !bar.action("gaussianBlur").isEnabled());
    // Other filters refuse the empty layer.
    session.beginFilter(FilterKind::gaussianBlur);
    QVERIFY(!session.filterEdit());
    // Folders and adjustments hold no pixels to paint.
    session.addAdjustment(AdjustmentKind::curves);
    session.setAdjustmentEditingID(std::nullopt);
    QVERIFY(!session.canVignette() && !bar.action("vignette").isEnabled());
    session.addGroup();
    QVERIFY(!session.canVignette());
}

void VignetteCanvasTests::anEmptyLayerTakesTheVignetteAcrossTheCanvas()
{
    EditorSession session;
    session.createDocument(40, 30);
    session.addBlankLayer();
    const QUuid id = session.activeLayerID().value();
    session.beginFilter(FilterKind::vignette);
    QVERIFY(prepared(session));
    QVERIFY(session.filterEdit().value().startedEmpty && session.filterEdit().value().canvas == QRectF(0, 0, 40, 30));
    // Round by default, as Swift 1.2.5 opens it.
    QCOMPARE(session.filterEdit().value().settings.vignetteRoundness, 100.0);
    QVERIFY(session.filterEdit().value().previewImage(id));
    QVERIFY(!session.activeLayer().value().asset);
    QVERIFY(committed(session));
    QCOMPARE(session.history.undoName(), QString("Vignette"));
    const ImageLayer layer = session.activeLayer().value();
    const QImage pixels = layer.asset.value().image();
    QCOMPARE(pixels.size(), QSize(40, 30));
    QCOMPARE(layer.transform.origin, QPointF(0, 0));
    // The kernel's canvas-wide fill: black edges, a clear middle.
    QImage expected = BrushRaster::context(40, 30, false);
    const FilterSettings defaults;
    adjust_colored_vignette(expected.bits(), 40, 30, size_t(expected.bytesPerLine()), 0, 0, 40, 30, 1, defaults.vignetteAmount, defaults.vignetteMidpoint,
                            defaults.vignetteRoundness, defaults.vignetteFeather, defaults.vignetteHighlights, 0, 0, 0);
    QCOMPARE(pixels, expected);
    QVERIFY(qAlpha(pixels.pixel(20, 15)) == 0 && qAlpha(pixels.pixel(0, 0)) > 80);
    session.undo();
    QVERIFY(!session.activeLayer().value().asset);
}

void VignetteCanvasTests::theCanvasShowsThePreviewOnAnEmptyLayer()
{
    Shown shown(QSize(40, 30));
    shown.settle();
    shown.session.addBlankLayer();
    QCoreApplication::processEvents();
    const QImage before = shown.canvas->grab().toImage();
    shown.session.beginFilter(FilterKind::vignette);
    QVERIFY(prepared(shown.session));
    QVERIFY(shown.session.filterEdit().value().previewImage(shown.session.activeLayerID().value()));
    QTRY_VERIFY(shown.canvas->grab().toImage() != before);
    shown.session.cancelFilter();
    QTRY_COMPARE(shown.canvas->grab().toImage(), before);
}

void VignetteCanvasTests::aLayerWithPixelsKeepsItsOwnFrameAndClear()
{
    // A grey square in clear pixels, smaller than the canvas.
    EditorSession session;
    session.createDocument(48, 48);
    QImage image = BrushRaster::context(24, 24, false);
    QPainter(&image).fillRect(QRect(3, 3, 18, 18), QColor::fromRgbF(0.7, 0.7, 0.7));
    session.insert(ImportedImage(image, image, QStringLiteral("Square")));
    session.beginFilter(FilterKind::vignette);
    QVERIFY(prepared(session));
    QVERIFY(!session.filterEdit().value().startedEmpty && !session.filterEdit().value().canvas);
    QVERIFY(committed(session));
    const QImage pixels = session.activeLayer().value().asset.value().image();
    QCOMPARE(pixels.size(), QSize(24, 24));
    // Clear stays clear; the square darkens within its own frame.
    QCOMPARE(qAlpha(pixels.pixel(0, 0)), 0);
    QCOMPARE(qAlpha(pixels.pixel(3, 3)), 255);
    QVERIFY(qRed(pixels.pixel(3, 3)) < qRed(pixels.pixel(12, 12)));
}

void VignetteCanvasTests::aSmallEmptyLayerGrowsToTheCanvas()
{
    // An empty box inside the canvas, off its corner.
    EditorSession session;
    session.createDocument(40, 30);
    session.addBlankLayer();
    const QUuid id = session.activeLayerID().value();
    rewrite(session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform = LayerTransform{.origin = {3, 7}, .size = {20, 12}}; });
    session.selectLayer(id);
    session.beginFilter(FilterKind::vignette);
    QVERIFY(prepared(session));
    QCOMPARE(session.filterEdit().value().previewSource.size(), QSize(40, 30));
    QCOMPARE(session.filterEdit().value().grownTransform.value(), (LayerTransform{.origin = {0, 0}, .size = {40, 30}}));
    QVERIFY(committed(session));
    const ImageLayer layer = session.activeLayer().value();
    QCOMPARE(layer.transform, (LayerTransform{.origin = {0, 0}, .size = {40, 30}}));
    // The canvas's corner, past the old box, took the colour.
    QVERIFY(qAlpha(layer.asset.value().image().pixel(0, 0)) > 80);
}

void VignetteCanvasTests::theFrameIsReadTopDown()
{
    // Off-centre and not square, so a flip would show.
    const QImage clear = BrushRaster::context(20, 12, false);
    FilterSettings settings;
    settings.vignetteAmount = 100;
    FilterJob job{FilterKind::vignette, clear, settings, 1, std::nullopt, QTransform::fromTranslate(3, 7)};
    job.canvas = QRectF(0, 0, 40, 30);
    const QImage result = PixelFilter::run(job);
    QImage expected = clear.copy();
    adjust_colored_vignette(expected.bits(), 20, 12, size_t(expected.bytesPerLine()), -3, -7, 40, 30, 1, 100, 50, 100, 60, 25, 0, 0, 0);
    QCOMPARE(result, expected);
    QCOMPARE(qAlpha(result.pixel(0, 0)), 100);
}

QTEST_MAIN(VignetteCanvasTests)
#include "VignetteCanvasTests.moc"
