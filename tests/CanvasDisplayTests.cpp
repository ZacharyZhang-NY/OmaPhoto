#include "Rendering/EditorCanvas.h"
#include "IO/ImageExporter.h"
#include "RenderFixtures.h"
#include "SessionFixtures.h"
#include <QtTest>

// What the canvas paints: the checkerboard, the layers, the grid.
namespace {
ImportedImage filled(int width, int height, QRgb premultiplied, const QString &name)
{
    const QImage image = solid(width, height, premultiplied);
    return ImportedImage(image, image, name);
}

// A sized canvas whose viewport is set by hand.
struct Scene {
    EditorSession session;
    CanvasView canvas{session};
    Scene(int width, int height, int canvasWidth, int canvasHeight, double zoom = 1)
    {
        session.createDocument(width, height);
        // Pixels alone: Swift's overlay view draws the box apart.
        session.setShowsTransformControls(false);
        // The built-in dark's Base, Swift's surround gray.
        QPalette dark = canvas.palette();
        dark.setColor(QPalette::Base, QColor(0x1b, 0x1b, 0x1b));
        canvas.setPalette(dark);
        canvas.resize(canvasWidth, canvasHeight);
        session.viewport.resize(QSizeF(canvasWidth, canvasHeight), 1, QSizeF(width, height));
        session.zoom(zoom);
    }
    QImage shot() { return canvas.grab().toImage().convertToFormat(QImage::Format_ARGB32); }
    QPointF origin() { return session.viewport.viewPoint(QPointF(0, 0), session.document().value().size()); }
};

// The worst channel gap, pixel centre against the export.
int gapToExport(Scene &scene, const std::function<bool(int)> &counts = [](int) { return true; })
{
    const QImage exported = ImageExporter::render(scene.session.projectSnapshot().value()).image.convertToFormat(QImage::Format_ARGB32);
    const QImage shot = scene.shot();
    const QPointF origin = scene.origin();
    const double zoom = scene.session.viewport.pointsPerPixel();
    int worst = 0;
    for (int y = 1; y < exported.height() - 1; ++y) {
        for (int x = 1; x < exported.width() - 1; ++x) {
            if (!counts(x))
                continue;
            const QRgb expected = exported.pixel(x, y), actual = shot.pixel(int(origin.x() + (x + 0.5) * zoom), int(origin.y() + (y + 0.5) * zoom));
            worst = std::max({worst, std::abs(qRed(expected) - qRed(actual)), std::abs(qGreen(expected) - qGreen(actual)),
                              std::abs(qBlue(expected) - qBlue(actual))});
        }
    }
    return worst;
}

int level(const QImage &image, int x, int y)
{
    return qRed(image.pixel(x, y));
}

// A two by two layer: black column, white column.
ImportedImage blackWhite()
{
    QImage image = BrushRaster::context(2, 2, false);
    for (int y = 0; y < 2; ++y) {
        image.setPixel(0, y, qRgba(0, 0, 0, 255));
        image.setPixel(1, y, qRgba(255, 255, 255, 255));
    }
    return ImportedImage(image, image, "Edge");
}
}

class CanvasDisplayTests : public QObject {
    Q_OBJECT
private slots:
    void theBackgroundCheckerboardShadowAndBorderSurroundTheDocument();
    void layersCompositeAsTheExportDoes();
    void blendModesMeetNoCheckerboardButTranslucencyDoes();
    void thePixelGridAppearsFromEightHundredPercent();
    void fromTwoHundredPercentPixelsAreHardEdged();
    void aPartialRepaintKeepsTheTilesAligned();
    void theLiveCompositeDrawsWhatTheExportDraws();
    void adjustmentsShowAsTheExportDoes();
    void anAdjustmentsMaskClipsItOnTheCanvas();
    void grainFollowsTheViewOnTheCanvas();
};

void CanvasDisplayTests::theBackgroundCheckerboardShadowAndBorderSurroundTheDocument()
{
    Scene scene(100, 50, 300, 200);
    QCOMPARE(scene.origin(), QPointF(100, 75));
    const QImage shot = scene.shot();
    QCOMPARE(shot.size(), QSize(300, 200));
    QCOMPARE(level(shot, 10, 10), 27);
    // Ten-point tiles, the even ones lighter.
    QCOMPARE(level(shot, 105, 80), 89);
    QCOMPARE(level(shot, 115, 80), 77);
    QCOMPARE(level(shot, 125, 90), 77);
    QCOMPARE(level(shot, 195, 120), 77);
    QCOMPARE(level(shot, 135, 90), 89);
    // The shadow darkens below and fades over fourteen points.
    QVERIFY(level(shot, 150, 127) < 22);
    const int fading = level(shot, 150, 135);
    QVERIFY2(fading > 18 && fading < 27, qPrintable(QString::number(fading)));
    QCOMPARE(level(shot, 150, 145), 27);
    QCOMPARE(level(shot, 150, 60), 27);
    // The border brightens the edge, astride it.
    QVERIFY(std::max(level(shot, 99, 80), level(shot, 100, 80)) >= 95);
}

// Hue, opacity, a clip, a placed mask, a folder mask.
void richScene(EditorSession &session)
{
    if (!session.linkMask(QUuid(), QUuid()))
        session.insert(filled(20, 10, qRgba(0, 0, 255, 255), "Blue"), QPointF(10, 5));
    session.insert(filled(6, 4, qRgba(255, 0, 0, 255), "Red"), QPointF(5, 3));
    const QUuid red = session.activeLayerID().value();
    session.setLayerOpacity(0.5);
    session.insert(filled(20, 10, qRgba(0, 255, 0, 255), "Green"), QPointF(10, 5));
    const QUuid green = session.activeLayerID().value();
    if (!session.linkMask(red, green))
        throw std::runtime_error("the green layer could not be clipped");
    session.insert(filled(8, 5, qRgba(255, 255, 0, 255), "Yellow"), QPointF(14, 5));
    const QUuid yellow = session.activeLayerID().value();
    session.setLayerBlendMode(LayerBlendMode::hue);
    session.groupSelectedLayers();
    const QUuid folder = session.activeLayerID().value();
    // Red's mask is soft and offset; the folder's is hard.
    rewrite(session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, red, LayerMask::assetFrom(gray(6, 4, 100)));
        record(snapshot, red).maskPlacement = LayerTransform{.origin = {3, 1}, .size = {6, 4}};
        record(snapshot, red).maskLinked = false;
        setMask(snapshot, green, LayerMask::assetFrom(gray(20, 10, 180)));
        QImage half = QImage(20, 10, QImage::Format_Grayscale8);
        half.fill(255);
        for (int y = 0; y < 10; ++y) {
            for (int x = 0; x < 15; ++x)
                half.setPixel(x, y, qRgb(0, 0, 0));
        }
        setMask(snapshot, folder, LayerMask::assetFrom(half));
        record(snapshot, yellow).transform.origin = QPointF(11, 3);
    });
}

void CanvasDisplayTests::layersCompositeAsTheExportDoes()
{
    Scene scene(20, 10, 60, 40);
    EditorSession &session = scene.session;
    richScene(session);
    session.zoom(1);
    const QImage exported = ImageExporter::render(session.projectSnapshot().value()).image.convertToFormat(QImage::Format_ARGB32);
    const QImage shot = scene.shot();
    const QPointF origin = scene.origin();
    QCOMPARE(origin, QPointF(20, 15));
    // Inside the border's hairline every pixel is the export's.
    int worst = 0;
    for (int y = 1; y < 9; ++y) {
        for (int x = 1; x < 19; ++x) {
            const QRgb expected = exported.pixel(x, y), actual = shot.pixel(int(origin.x()) + x, int(origin.y()) + y);
            QCOMPARE(qAlpha(expected), 255);
            worst = std::max({worst, std::abs(qRed(expected) - qRed(actual)), std::abs(qGreen(expected) - qGreen(actual)),
                              std::abs(qBlue(expected) - qBlue(actual))});
        }
    }
    QVERIFY2(worst <= 1, qPrintable(QString::number(worst)));
    // The edge carries half the hairline: 13% white, halved.
    const int edge = qRed(shot.pixel(int(origin.x()), int(origin.y()) + 5)) - qRed(exported.pixel(0, 5));
    QVERIFY2(edge >= 15 && edge <= 18, qPrintable(QString::number(edge)));
    // The scene holds hue, opacity, a live mask, two masks.
    QCOMPARE(exported.pixel(0, 0), qRgb(0, 0, 255));
    const QRgb clipped = exported.pixel(4, 2);
    QVERIFY(qGreen(clipped) > qRed(clipped) && qRed(clipped) > 0 && qBlue(clipped) > qGreen(clipped));
    QCOMPARE(exported.pixel(12, 4), qRgb(0, 0, 255));
    const QRgb hued = exported.pixel(16, 4);
    QVERIFY(qRed(hued) == qGreen(hued) && qRed(hued) > 0 && qBlue(hued) == 0);
    // A tried blend mode and an open edit both show.
    const QUuid yellow = session.document().value().layers.back().id;
    session.selectLayer(yellow);
    session.previewBlendMode(LayerBlendMode::normal, yellow);
    QCOMPARE(scene.shot().pixel(int(origin.x()) + 16, int(origin.y()) + 4), qRgb(255, 255, 0));
    session.previewBlendMode(std::nullopt, std::nullopt);
    // A drag's edit: hidden controls keep the pixels bare.
    session.beginTransform(false);
    LayerTransform draft = session.transformEdit().value().draft;
    draft.origin += QPointF(0, 2);
    session.previewTransform(draft);
    const QImage moved = scene.shot();
    QCOMPARE(moved.pixel(int(origin.x()) + 16, int(origin.y()) + 6), hued);
    QCOMPARE(moved.pixel(int(origin.x()) + 16, int(origin.y()) + 4), qRgb(0, 0, 255));
    session.cancelTransform();
}

void CanvasDisplayTests::blendModesMeetNoCheckerboardButTranslucencyDoes()
{
    Scene multiplied(20, 10, 60, 40);
    multiplied.session.insert(filled(20, 10, qRgba(255, 255, 255, 255), "White"), QPointF(10, 5));
    multiplied.session.setLayerBlendMode(LayerBlendMode::multiply);
    const QImage white = multiplied.shot();
    QCOMPARE(level(white, 25, 20), 255);
    QCOMPARE(level(white, 35, 20), 255);
    // Half red over the tiles: half red, half tile.
    Scene translucent(20, 10, 60, 40);
    translucent.session.insert(filled(20, 10, qRgba(128, 0, 0, 128), "Red"), QPointF(10, 5));
    const QImage red = translucent.shot();
    const QRgb even = red.pixel(25, 20), odd = red.pixel(35, 20);
    QVERIFY(std::abs(qRed(even) - 172) <= 2 && std::abs(qGreen(even) - 44) <= 2);
    QVERIFY(std::abs(qRed(odd) - 166) <= 2 && std::abs(qGreen(odd) - 38) <= 2);
}

void CanvasDisplayTests::thePixelGridAppearsFromEightHundredPercent()
{
    Scene scene(4, 2, 80, 60, 8);
    scene.session.insert(filled(4, 2, qRgba(0, 0, 0, 255), "Black"), QPointF(2, 1));
    QCOMPARE(scene.origin(), QPointF(24, 22));
    QImage shot = scene.shot();
    QCOMPARE(level(shot, 28, 26), 0);
    // A hairline on each pixel boundary, half in each neighbour.
    QVERIFY(level(shot, 31, 26) > 15 && level(shot, 32, 26) > 15);
    QVERIFY(level(shot, 32, 26) < 60);
    QVERIFY(level(shot, 28, 30) > 15 && level(shot, 28, 29) > 15);
    // Half a point over: whole pixels, crossings filled once.
    scene.session.viewport.translate(QSizeF(0.5, 0.5));
    shot = scene.shot();
    QCOMPARE(level(shot, 32, 27), 63);
    QCOMPARE(level(shot, 28, 30), 63);
    QCOMPARE(level(shot, 32, 30), 63);
    QCOMPARE(level(shot, 28, 27), 0);
    scene.session.viewport.translate(QSizeF(-0.5, -0.5));
    scene.session.setShowsPixelGrid(false);
    shot = scene.shot();
    QCOMPARE(level(shot, 31, 26), 0);
    QCOMPARE(level(shot, 32, 26), 0);
    QCOMPARE(level(shot, 28, 30), 0);
    // Below 800% there is no grid, whatever the flag.
    scene.session.setShowsPixelGrid(true);
    scene.session.zoom(7.9);
    shot = scene.shot();
    const QPointF origin = scene.origin();
    for (int x = int(origin.x()) + 2; x < int(origin.x()) + 29; ++x)
        QCOMPARE(level(shot, x, int(origin.y()) + 7), 0);
}

void CanvasDisplayTests::fromTwoHundredPercentPixelsAreHardEdged()
{
    Scene scene(2, 2, 40, 20, 4);
    scene.session.insert(blackWhite(), QPointF(1, 1));
    QCOMPARE(scene.origin(), QPointF(16, 6));
    QImage shot = scene.shot();
    QCOMPARE(level(shot, 19, 10), 0);
    QCOMPARE(level(shot, 20, 10), 255);
    scene.session.zoom(2);
    shot = scene.shot();
    QCOMPARE(scene.origin(), QPointF(18, 8));
    QCOMPARE(level(shot, 19, 10), 0);
    QCOMPARE(level(shot, 20, 10), 255);
    // Below 200% the boundary blends into its neighbours.
    scene.session.zoom(1.9);
    shot = scene.shot();
    QCOMPARE(scene.origin(), QPointF(18.1, 8.1));
    const int left = level(shot, 19, 10), right = level(shot, 20, 10);
    QVERIFY2(left >= 40 && left <= 80, qPrintable(QString::number(left)));
    QVERIFY2(right >= 175 && right <= 215, qPrintable(QString::number(right)));
}

void CanvasDisplayTests::aPartialRepaintKeepsTheTilesAligned()
{
    Scene scene(100, 50, 300, 200);
    QImage target(300, 200, QImage::Format_ARGB32);
    target.fill(Qt::magenta);
    // The region lands at the offset: its own top left.
    scene.canvas.render(&target, QPoint(120, 80), QRegion(QRect(120, 80, 30, 30)));
    QCOMPARE(target.pixel(125, 85), qRgb(77, 77, 77));
    QCOMPARE(target.pixel(135, 85), qRgb(89, 89, 89));
    QCOMPARE(target.pixel(125, 95), qRgb(89, 89, 89));
    QCOMPARE(target.pixel(200, 10), qRgb(255, 0, 255));
}

void CanvasDisplayTests::theLiveCompositeDrawsWhatTheExportDraws()
{
    EditorSession session;
    session.createDocument(20, 10);
    richScene(session);
    const QImage exported = ImageExporter::render(session.projectSnapshot().value()).image;
    QImage composite = BrushRaster::context(20, 10, false);
    {
        QPainter painter(&composite);
        session.drawLiveComposite(session.document().value(), painter);
    }
    QCOMPARE(composite, exported);
    // An open edit shows its draft, not the stored transform.
    const QUuid yellow = session.document().value().layers.back().id;
    session.selectLayer(yellow);
    session.beginTransform();
    LayerTransform draft = session.transformEdit().value().draft;
    draft.origin += QPointF(0, 2);
    session.previewTransform(draft);
    const auto composed = [&] {
        QImage image = BrushRaster::context(20, 10, false);
        QPainter painter(&image);
        session.drawLiveComposite(session.document().value(), painter);
        return image;
    };
    const QImage moved = composed();
    QVERIFY(moved != exported);
    QCOMPARE(moved.pixel(16, 6), exported.pixel(16, 4));
    QCOMPARE(moved.pixel(16, 4), qRgba(0, 0, 255, 255));
    session.cancelTransform();
    // A blend mode tried on shows as well.
    session.previewBlendMode(LayerBlendMode::normal, yellow);
    const QImage previewed = composed();
    QCOMPARE(previewed.pixel(16, 4), qRgba(255, 255, 0, 255));
    session.previewBlendMode(std::nullopt, std::nullopt);
    QCOMPARE(composed(), exported);
}

void CanvasDisplayTests::adjustmentsShowAsTheExportDoes()
{
    Scene scene(20, 10, 60, 40);
    EditorSession &session = scene.session;
    richScene(session);
    const QImage before = ImageExporter::render(session.projectSnapshot().value()).image;
    session.selectLayer(session.document().value().layers.back().id);
    session.addAdjustment(AdjustmentKind::levels);
    session.setAdjustmentEditingID(std::nullopt);
    const QUuid levels = session.activeLayerID().value();
    LayerAdjustment darker{AdjustmentKind::levels};
    darker.levels.ranges[0].outputWhite = 160;
    session.updateAdjustment(levels, darker);
    session.zoom(1);
    QVERIFY(ImageExporter::render(session.projectSnapshot().value()).image != before);
    QVERIFY2(gapToExport(scene) <= 1, qPrintable(QString::number(gapToExport(scene))));
    // The adjustment layer's own changes repaint the view.
    scene.canvas.synchronizeDisplay();
    QVERIFY(!scene.canvas.synchronizeDisplay());
    session.setLayerOpacity(0.5);
    QVERIFY(scene.canvas.synchronizeDisplay());
    QVERIFY(gapToExport(scene) <= 1);
    // Undo restores settings, no revision: still a repaint.
    LayerAdjustment lighter = darker;
    lighter.levels.ranges[0].outputWhite = 220;
    session.beginEdit(QStringLiteral("Edit Levels"));
    session.updateAdjustment(levels, lighter);
    session.endEdit();
    scene.canvas.synchronizeDisplay();
    session.undo();
    QVERIFY(scene.canvas.synchronizeDisplay());
}

void CanvasDisplayTests::anAdjustmentsMaskClipsItOnTheCanvas()
{
    // At 200%, its left half hidden by a hard mask.
    Scene scene(20, 10, 80, 60, 2);
    EditorSession &session = scene.session;
    session.insert(filled(20, 10, qRgba(200, 120, 40, 255), "Orange"));
    session.addAdjustment(AdjustmentKind::levels);
    session.setAdjustmentEditingID(std::nullopt);
    const QUuid levels = session.activeLayerID().value();
    LayerAdjustment darker{AdjustmentKind::levels};
    darker.levels.ranges[0].outputWhite = 60;
    session.updateAdjustment(levels, darker);
    QImage half(20, 10, QImage::Format_Grayscale8);
    half.fill(255);
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x)
            half.setPixel(x, y, qRgb(0, 0, 0));
    }
    rewrite(session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, levels, LayerMask::assetFrom(half)); });
    // Installing fits the view: back to two hundred percent.
    session.zoom(2);
    QVERIFY2(gapToExport(scene) <= 1, qPrintable(QString::number(gapToExport(scene))));
    // Smooth at 150%, the mask takes the view's scale.
    session.zoom(1.5);
    QVERIFY(gapToExport(scene, [](int x) { return x < 9 || x > 10; }) <= 1);
    session.zoom(2);
    // A disabled mask hides nothing.
    session.toggleLayerMask();
    QVERIFY(!session.activeLayer().value().mask.value().isEnabled);
    QVERIFY(gapToExport(scene) <= 1);
    session.toggleLayerMask();
    // Mid-stroke, new paint on the mask shows at once.
    const QPointF origin = scene.origin();
    const QPoint left(int(origin.x() + 3.5 * 2), int(origin.y() + 5.5 * 2));
    const int hidden = qRed(scene.shot().pixel(left));
    session.selectLayerTarget(levels, true);
    session.setMaskPaintWhite(true);
    session.selectTool(NavigationTool::brush);
    session.beginBrush(QPointF(3.5, 5.5));
    session.continueBrush(QPointF(4, 5.5));
    QVERIFY(session.brushStroke());
    const int painted = qRed(scene.shot().pixel(left));
    QVERIFY2(hidden == 200 && painted < 70, qPrintable(QStringLiteral("%1 %2").arg(hidden).arg(painted)));
    session.cancelBrush();
}

void CanvasDisplayTests::grainFollowsTheViewOnTheCanvas()
{
    // Swift's canvas hands Grain the view's region, not the document's.
    Scene scene(40, 40, 120, 120);
    EditorSession &session = scene.session;
    session.insert(filled(40, 40, qRgba(128, 128, 128, 255), "Gray"));
    session.addAdjustment(AdjustmentKind::grain);
    session.setAdjustmentEditingID(std::nullopt);
    LayerAdjustment grain = session.activeLayer().value().adjustment.value();
    GrainSettings strong = grain.grain();
    strong.amount = 100;
    // Pinned: under some seeds the pan keeps one pixel's tone.
    strong.seed = 7;
    grain.setGrain(strong);
    session.updateAdjustment(session.activeLayerID().value(), grain);
    session.zoom(1);
    QVERIFY(gapToExport(scene) > 20);
    // A pan moves the pattern under the same document pixel.
    const auto pixel = [&scene] {
        const QPointF origin = scene.origin();
        return scene.shot().pixel(int(origin.x() + 20.5), int(origin.y() + 20.5));
    };
    const QRgb before = pixel();
    session.viewport.translate(QSizeF(3, 0));
    QVERIFY(pixel() != before);
}

QTEST_MAIN(CanvasDisplayTests)
#include "CanvasDisplayTests.moc"
