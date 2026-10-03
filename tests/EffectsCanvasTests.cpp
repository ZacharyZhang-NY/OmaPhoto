#include "Document/LayerEffects+Renderer.h"
#include "IO/ImageExporter.h"
#include "Rendering/EditorCanvas.h"
#include "RenderFixtures.h"
#include "EffectsCanvasFixtures.h"
#include "SelectionFixtures.h"
#include "SessionFixtures.h"
#include <QPainterPathStroker>
#include <QtTest>

// Swift's effects on the canvas: previews, the paint's surface, distortions.
namespace {
// Counts the paints a widget receives.
struct Painted : QObject {
    int &count;
    explicit Painted(int &count) : count(count) {}
    bool eventFilter(QObject *, QEvent *event) override
    {
        count += event->type() == QEvent::Paint;
        return false;
    }
};

// A pixel only the warp makes yellow, clear of handles.
std::optional<QPoint> warpedOnly(const DistortWarp::Warped &warped, const QRectF &unwarped, const std::vector<QPointF> &handles)
{
    const QImage image = warped.image.convertToFormat(QImage::Format_ARGB32);
    const QTransform toImage = BrushRaster::pixelToDocument(warped.transform, image.width(), image.height()).inverted();
    const auto yellowAt = [&](QPointF point) {
        const QPointF mapped = toImage.map(point);
        const QPoint pixel(int(std::floor(mapped.x())), int(std::floor(mapped.y())));
        return image.rect().contains(pixel) && image.pixel(pixel) == qRgba(255, 255, 0, 255);
    };
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 60; ++x) {
            const QPointF centre(x + 0.5, y + 0.5);
            const bool nearHandle = std::any_of(handles.begin(), handles.end(), [&](QPointF handle) {
                return std::abs(handle.x() - centre.x()) < 7 && std::abs(handle.y() - centre.y()) < 7;
            });
            if (nearHandle || unwarped.adjusted(-1, -1, 1, 1).contains(centre))
                continue;
            if (yellowAt(centre) && yellowAt(centre + QPointF(1, 0)) && yellowAt(centre - QPointF(1, 0)) && yellowAt(centre + QPointF(0, 1))
                && yellowAt(centre - QPointF(0, 1)))
                return QPoint(x, y);
        }
    }
    return std::nullopt;
}
}

class EffectsCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void theCanvasDrawsALayersEffectsOnceTheyLand();
    void aLandedPreviewRepaintsTheView();
    void aPlacedMaskClipsThePreviewAsItClipsTheExport();
    void changingEffectsRepaintsTheView();
    void aDeletedLayersPreviewGoesWithIt();
    void aPendingDistortionWarpsTheEffectsAndApplyingKeepsThem();
    void paintKeepsItsEffectsAndHandsThemOn();
    void aSurfaceIsMadeAnewForOtherSettings();
    void paintOnAMaskedLayerKeepsItsMask();
    void paintPastTheSurfacesBudgetShowsTheLastEffects();
    void invalidEffectsShowNothing();
    void effectsDrawAheadOfAColourEditsPreview();
    void movingPixelsKeepTheEffects();
    void aGradientKeepsTheEffects();
};

void EffectsCanvasTests::theCanvasDrawsALayersEffectsOnceTheyLand()
{
    Scene scene;
    LayerEffects effects = outline(2);
    effects.shadow = ShadowEffect{.angle = 90, .distance = 6, .blur = 0, .red = 1, .green = 0, .blue = 0, .opacity = 1};
    scene.session.setEffects(effects);
    // Until the worker lands, the layer alone.
    QCOMPARE(scene.at(QPoint(23, 20)), QColor(Qt::white));
    QTRY_COMPARE(scene.at(QPoint(23, 20)), yellow);
    QCOMPARE(scene.at(QPoint(30, 29)), QColor(Qt::red));
    QCOMPARE(scene.at(QPoint(30, 20)), QColor(Qt::blue));
    QCOMPARE(scene.gapToExport(), 0);
}

void EffectsCanvasTests::aLandedPreviewRepaintsTheView()
{
    Scene scene;
    QWidget window;
    CanvasView &canvas = *new CanvasView(scene.session, &window);
    canvas.resize(300, 200);
    int paints = 0;
    Painted counter(paints);
    canvas.installEventFilter(&counter);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTest::qWait(100);
    const int shown = paints;
    scene.session.setEffects(outline(2));
    canvas.synchronizeDisplay();
    QVERIFY(QTest::qWaitFor([&] { return paints > shown; }, 5000));
    // The worker's result arrives later and asks for a paint.
    const int before = paints;
    QVERIFY(!scene.session.effectsPreviews.rendered(scene.square));
    QTRY_VERIFY(scene.session.effectsPreviews.rendered(scene.square) && paints > before);
}

void EffectsCanvasTests::aPlacedMaskClipsThePreviewAsItClipsTheExport()
{
    Scene scene;
    // Left half hidden, the mask placed two pixels right.
    QImage half = gray(10, 10, 255);
    for (int y = 0; y < 10; ++y)
        std::fill_n(half.scanLine(y), 5, uchar(0));
    rewrite(scene.session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, scene.square, LayerMask::assetFrom(half));
        record(snapshot, scene.square).maskPlacement = placedAt(QPointF(27, 15), QSizeF(10, 10));
        record(snapshot, scene.square).maskLinked = false;
        record(snapshot, scene.square).effects = outline(2);
    });
    scene.session.zoom(1);
    scene.landed();
    // Swift asks first with the mask unplaced; the twin once.
    QCOMPARE(scene.gapToExport(), 0);
    QCOMPARE(scene.at(QPoint(30, 20)), yellow);
    QCOMPARE(scene.at(QPoint(33, 20)), QColor(Qt::blue));
}

void EffectsCanvasTests::changingEffectsRepaintsTheView()
{
    Scene scene;
    QVERIFY(scene.canvas.synchronizeDisplay());
    QVERIFY(!scene.canvas.synchronizeDisplay());
    scene.session.setEffects(outline(2));
    QVERIFY(scene.canvas.synchronizeDisplay());
}

void EffectsCanvasTests::aDeletedLayersPreviewGoesWithIt()
{
    Scene scene;
    scene.session.setEffects(outline(2));
    scene.landed();
    scene.session.deleteLayerOrMask();
    scene.shot();
    QVERIFY(!scene.session.effectsPreviews.rendered(scene.square));
}

void EffectsCanvasTests::aPendingDistortionWarpsTheEffectsAndApplyingKeepsThem()
{
    Scene scene;
    scene.session.setEffects(outline(2));
    const EffectsPreviewCache::Result effects = scene.landed();
    EditorSession &session = scene.session;
    session.beginTransform();
    session.beginDistort();
    // The top right corner pulled far up and out.
    const Corners corners{QPointF(25, 15), QPointF(55, 5), QPointF(35, 25), QPointF(25, 25)};
    session.previewCorners(corners);
    const ImageLayer layer = layerWith(session, scene.square);
    const std::optional<DistortWarp::Warped> warped = session.distortedEffects(layer, effects.image, effects.inset);
    // The effects' grown box takes the layer's perspective.
    const LayerTransform grown = LayerEffectsRenderer::placed(layer.transform, effects.image, effects.inset);
    const DistortWarp::Warped expected = DistortWarp::warp(effects.image, grown, DistortWarp::carried(grown, layer.transform, corners), false, 2048);
    QVERIFY(warped.value().image == expected.image && warped.value().transform == expected.transform);
    QCOMPARE(session.distortedEffects(layer, effects.image, effects.inset).value().image.cacheKey(), warped.value().image.cacheKey());
    const std::vector<QPointF> handles{corners[0], corners[1], corners[2], corners[3], QPointF(40, 10), QPointF(45, 15), QPointF(30, 25), QPointF(25, 20)};
    const QPoint outside = warpedOnly(warped.value(), QRectF(21, 11, 18, 18), handles).value();
    QCOMPARE(scene.at(outside), yellow);
    // A layer outside the edit keeps its own.
    QVERIFY(!session.distortedEffects(session.document().value().layers.front(), effects.image, effects.inset));
    // Past 2048 pixels a side the warp is drawn smaller.
    session.previewCorners({QPointF(25, 15), QPointF(2500, 15), QPointF(2500, 25), QPointF(25, 25)});
    const DistortWarp::Warped far = session.distortedEffects(layer, effects.image, effects.inset).value();
    QVERIFY(far.image.size() == QSize(2048, 9) && far.transform.size == QSizeF(4456, 19));
    // New corners or other effects warp anew.
    session.previewCorners({QPointF(25, 15), QPointF(54, 5), QPointF(35, 25), QPointF(25, 25)});
    const qint64 moved = session.distortedEffects(layer, effects.image, effects.inset).value().image.cacheKey();
    QVERIFY(moved != warped.value().image.cacheKey());
    QVERIFY(session.distortedEffects(layer, effects.image.copy(), effects.inset).value().image.cacheKey() != moved);
    session.previewCorners(corners);
    const std::optional<DistortWarp::Warped> again = session.distortedEffects(layer, effects.image, effects.inset);
    // Applied: the warped effects show until new ones land.
    session.commitTransform();
    const EffectsPreviewCache::Result seeded = session.effectsPreviews.rendered(scene.square).value();
    QVERIFY(seeded.image.cacheKey() == again.value().image.cacheKey() && seeded.placement == warped.value().transform);
    QCOMPARE(scene.at(outside), yellow);
    QTRY_VERIFY(!session.effectsPreviews.rendered(scene.square).value().placement);
    // A plain transform, or a mask's distortion, warps nothing.
    session.beginTransform();
    QVERIFY(!session.distortedEffects(layerWith(session, scene.square), effects.image, effects.inset));
    session.cancelTransform();
    session.addLayerMask(true);
    session.toggleMaskLink(scene.square);
    session.selectLayerTarget(scene.square, true);
    session.beginTransform();
    session.beginDistort();
    session.previewCorners(corners);
    QVERIFY(session.transformEdit().value().mask && session.transformEdit().value().corners);
    QVERIFY(!session.distortedEffects(layerWith(session, scene.square), effects.image, effects.inset));
    session.cancelTransform();
}

void EffectsCanvasTests::paintKeepsItsEffectsAndHandsThemOn()
{
    Scene scene;
    EditorSession &session = scene.session;
    session.selectTool(NavigationTool::brush);
    BrushSettings settings = session.brushSettings();
    settings.diameter = 4;
    settings.red = 1;
    session.setBrushSettings(settings);
    // Without effects a stroke makes no surface to hand on.
    session.beginBrush(QPointF(45, 10));
    QCOMPARE(scene.at(QPoint(45, 10)), QColor(Qt::red));
    QVERIFY(session.finishBrushImmediately());
    scene.canvas.synchronizeDisplay();
    QVERIFY(!session.effectsPreviews.rendered(scene.square));
    // An overlay covers the wet paint too, as it will.
    LayerEffects effects = outline(2);
    effects.colorOverlay = ColorOverlayEffect{.red = 0, .green = 1, .blue = 0, .opacity = 1};
    session.setEffects(effects);
    scene.landed();
    session.beginBrush(QPointF(45, 20));
    session.continueBrush(QPointF(55, 20));
    QCOMPARE(scene.at(QPoint(50, 16)), yellow);
    QCOMPARE(scene.at(QPoint(50, 20)), QColor(Qt::green));
    // Nothing is handed on while the stroke lasts.
    scene.canvas.synchronizeDisplay();
    QVERIFY(!session.effectsPreviews.rendered(scene.square).value().placement);
    QVERIFY(session.finishBrushImmediately());
    // The surface is handed on once, so nothing blinks.
    scene.canvas.synchronizeDisplay();
    QVERIFY(session.effectsPreviews.rendered(scene.square).value().placement);
    QCOMPARE(scene.at(QPoint(50, 16)), yellow);
    QTRY_VERIFY(!session.effectsPreviews.rendered(scene.square).value().placement);
    scene.canvas.synchronizeDisplay();
    QVERIFY(!session.effectsPreviews.rendered(scene.square).value().placement);
    QCOMPARE(scene.gapToExport(), 0);
}

void EffectsCanvasTests::aSurfaceIsMadeAnewForOtherSettings()
{
    Scene scene;
    EditorSession &session = scene.session;
    session.setEffects(outline(2));
    scene.landed();
    session.selectTool(NavigationTool::brush);
    BrushSettings settings = session.brushSettings();
    settings.diameter = 4;
    settings.red = 1;
    session.setBrushSettings(settings);
    session.beginBrush(QPointF(45, 20));
    session.continueBrush(QPointF(55, 20));
    QCOMPARE(scene.at(QPoint(50, 16)), yellow);
    QVERIFY(session.finishBrushImmediately());
    // A canvas alone follows nothing: the old surface stays.
    LayerEffects cyan = outline(2);
    cyan.stroke.value().red = 0;
    cyan.stroke.value().blue = 1;
    session.setEffects(cyan);
    session.beginBrush(QPointF(45, 32));
    session.continueBrush(QPointF(55, 32));
    QCOMPARE(scene.at(QPoint(50, 28)), QColor(0, 255, 255));
    session.cancelBrush();
}

void EffectsCanvasTests::paintOnAMaskedLayerKeepsItsMask()
{
    Scene scene;
    QImage half = gray(10, 10, 255);
    for (int y = 0; y < 10; ++y)
        std::fill_n(half.scanLine(y), 5, uchar(0));
    rewrite(scene.session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, scene.square, LayerMask::assetFrom(half));
        record(snapshot, scene.square).effects = outline(2);
    });
    scene.session.zoom(1);
    scene.landed();
    EditorSession &session = scene.session;
    session.selectTool(NavigationTool::brush);
    BrushSettings settings = session.brushSettings();
    settings.diameter = 4;
    settings.red = 1;
    session.setBrushSettings(settings);
    session.beginBrush(QPointF(45, 20));
    session.continueBrush(QPointF(55, 20));
    // Hidden half hidden, unoutlined; paint past the layer shows.
    QCOMPARE(scene.at(QPoint(27, 20)), QColor(Qt::white));
    QCOMPARE(scene.at(QPoint(23, 20)), QColor(Qt::white));
    QCOMPARE(scene.at(QPoint(32, 20)), QColor(Qt::blue));
    QCOMPARE(scene.at(QPoint(50, 16)), yellow);
    session.cancelBrush();
}

void EffectsCanvasTests::paintPastTheSurfacesBudgetShowsTheLastEffects()
{
    Scene scene;
    // Scaled down 200 times, the stroke's grid passes 80 million.
    const QImage fine = solid(2000, 2000, qRgba(0, 0, 255, 255));
    rewrite(scene.session, [&](ProjectSnapshot &snapshot) {
        snapshot.images.insert_or_assign(scene.square, ImportedImage(fine, fine, QStringLiteral("Square")));
        record(snapshot, scene.square).transform = placedAt(QPointF(25, 15), QSizeF(10, 10));
        record(snapshot, scene.square).effects = outline(400);
    });
    scene.session.zoom(1);
    scene.landed();
    EditorSession &session = scene.session;
    session.selectTool(NavigationTool::brush);
    BrushSettings settings = session.brushSettings();
    settings.diameter = 4;
    settings.red = 1;
    session.setBrushSettings(settings);
    session.beginBrush(QPointF(45, 20));
    session.continueBrush(QPointF(55, 20));
    // No surface: the last effects show, the paint without them.
    QCOMPARE(scene.at(QPoint(23, 20)), yellow);
    QCOMPARE(scene.at(QPoint(50, 20)), QColor(Qt::red));
    QCOMPARE(scene.at(QPoint(50, 16)), QColor(Qt::white));
    session.cancelBrush();
    // A seed stands in where it belongs.
    session.effectsPreviews.seed(scene.square, solid(8, 8, qRgba(0, 255, 0, 255)), placedAt(QPointF(2, 2), QSizeF(8, 8)));
    session.beginBrush(QPointF(45, 20));
    QCOMPARE(scene.at(QPoint(5, 5)), QColor(Qt::green));
    QCOMPARE(scene.at(QPoint(23, 20)), QColor(Qt::white));
    session.cancelBrush();
}

void EffectsCanvasTests::invalidEffectsShowNothing()
{
    Scene scene;
    // A stroke past Swift's 500 pixels, which files may hold.
    rewrite(scene.session, [&](ProjectSnapshot &snapshot) { record(snapshot, scene.square).effects = outline(600); });
    scene.session.zoom(1);
    QCOMPARE(scene.at(QPoint(23, 20)), QColor(Qt::white));
    QTest::qWait(150);
    QVERIFY(!scene.session.effectsPreviews.rendered(scene.square));
    // Painting makes no surface for them either.
    EditorSession &session = scene.session;
    session.selectTool(NavigationTool::brush);
    BrushSettings settings = session.brushSettings();
    settings.diameter = 4;
    session.setBrushSettings(settings);
    session.beginBrush(QPointF(45, 20));
    QCOMPARE(scene.at(QPoint(45, 16)), QColor(Qt::white));
    session.cancelBrush();
}

void EffectsCanvasTests::effectsDrawAheadOfAColourEditsPreview()
{
    Scene scene(qRgba(100, 100, 100, 255));
    scene.session.setEffects(outline(2));
    scene.landed();
    LevelsSettings inverted;
    inverted.setCurrent(LevelRange{0, 1, 255, 255, 0});
    scene.session.beginLevels();
    scene.session.updateLevels(inverted, true);
    QTRY_VERIFY(scene.session.levels().value().preparedPreview);
    // Swift draws the effects first: the layer shows unadjusted.
    QCOMPARE(scene.at(QPoint(30, 20)), QColor(100, 100, 100));
    QCOMPARE(scene.at(QPoint(23, 20)), yellow);
}

// A band round a path, where overlays draw.
QPainterPath band(const QPainterPath &path, double width)
{
    QPainterPathStroker stroker;
    stroker.setWidth(width);
    return stroker.createStroke(path);
}

// The document as shown, pixel by pixel, overlays' bands aside.
int gapBetween(Scene &scene, const QImage &one, const QImage &other, const QPainterPath &skip)
{
    int worst = 0;
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 60; ++x) {
            if (skip.contains(QPointF(x + 0.5, y + 0.5)))
                continue;
            const QRgb a = one.pixel(scene.shown(QPointF(x + 0.5, y + 0.5))), b = other.pixel(scene.shown(QPointF(x + 0.5, y + 0.5)));
            worst = std::max({worst, std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b))});
        }
    }
    return worst;
}

// Swift's movingWithEffects: the stroke follows the lifted pixels.
void EffectsCanvasTests::movingPixelsKeepTheEffects()
{
    Scene scene;
    EditorSession &session = scene.session;
    session.setEffects(outline(2));
    scene.landed();
    session.applySelection(rectPath(QRectF(25, 15, 10, 10)), SelectionMode::replace, QStringLiteral("Select"));
    QVERIFY(session.beginPixelMove());
    // 5.6 rounds to 6: the square moves to 31..41.
    session.movePixels(QSizeF(5.6, 0));
    scene.canvas.synchronizeDisplay();
    QCOMPARE(scene.at(QPoint(42, 20)), yellow);
    QCOMPARE(scene.at(QPoint(24, 20)), QColor(Qt::white));
    const QImage moving = scene.shot();
    bool done = false;
    session.finishPixelMove([&done] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(session.selection().value().path.boundingRect(), QRectF(31, 15, 10, 10));
    scene.canvas.synchronizeDisplay();
    QCOMPARE(gapBetween(scene, moving, scene.shot(), band(rectPath(QRectF(31, 15, 10, 10)), 4)), 0);
    session.deselect();
    QTRY_COMPARE(scene.gapToExport(), 0);
}

// Swift's gradientWithEffects: the pending fill shows its effects.
void EffectsCanvasTests::aGradientKeepsTheEffects()
{
    Scene scene;
    EditorSession &session = scene.session;
    session.setEffects(outline(2));
    scene.landed();
    // Black fading to clear across the square's left half.
    session.applySelection(rectPath(QRectF(10, 5, 20, 30)), SelectionMode::replace, QStringLiteral("Select"));
    session.selectTool(NavigationTool::gradient);
    session.beginGradient(QPointF(15, 20));
    session.moveGradient(std::nullopt, QPointF(30, 20));
    session.endGradientDrag();
    scene.canvas.synchronizeDisplay();
    // The stroke rings the new paint at once.
    QVERIFY(scene.at(QPoint(8, 20)).red() > 100 && scene.at(QPoint(8, 20)).blue() < 30);
    const QImage pending = scene.shot();
    bool done = false;
    session.commitGradient([&done] { done = true; });
    QTRY_VERIFY(done);
    scene.canvas.synchronizeDisplay();
    // The line, its ends and the ants aside.
    QPainterPath line;
    line.moveTo(15, 20);
    line.lineTo(30, 20);
    const QPainterPath overlays = band(rectPath(QRectF(10, 5, 20, 30)), 4).united(band(line, 24));
    QTRY_VERIFY(gapBetween(scene, pending, scene.shot(), overlays) <= 1);
    session.deselect();
    QTRY_COMPARE(scene.gapToExport(), 0);
}

QTEST_MAIN(EffectsCanvasTests)
#include "EffectsCanvasTests.moc"
