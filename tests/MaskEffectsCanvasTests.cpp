#include "EffectsCanvasFixtures.h"

// Swift 1.2.11 (b683acb): a painted mask keeps effects on.
namespace {
bool nearly(QColor colour, QColor expected)
{
    return std::abs(colour.red() - expected.red()) <= 40 && std::abs(colour.green() - expected.green()) <= 40
        && std::abs(colour.blue() - expected.blue()) <= 40;
}

// Black down the mask, eight wide; a later pass cuts.
void paintHole(Scene &scene)
{
    EditorSession &session = scene.session;
    session.selectLayerTarget(session.activeLayerID().value(), true);
    session.selectTool(NavigationTool::brush);
    session.setMaskPaintWhite(false);
    BrushSettings settings = session.brushSettings();
    settings.diameter = 8;
    settings.hardness = 1;
    session.setBrushSettings(settings);
    session.beginBrush(QPointF(30, 2));
    session.continueBrush(QPointF(30, 6));
    scene.shot();
    session.continueBrush(QPointF(30, 35));
}
}

class MaskEffectsCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void aPaintedMaskRedoesTheEffects();
    void aPlacedMaskRedoesThemInTheLayersGrid();
    void untouchedTilesKeepTheOldMask();
    void aFinerPlacedMaskMapsIntoTheLayersGrid();
};

void MaskEffectsCanvasTests::aPaintedMaskRedoesTheEffects()
{
    Scene scene;
    scene.session.setEffects(outline(2));
    scene.session.addLayerMask(true);
    scene.landed();
    QVERIFY(nearly(scene.at(QPoint(27, 20)), QColor(0, 0, 255)));
    paintHole(scene);
    // Mid-stroke the outline follows the hole: its middle is clear.
    QVERIFY2(nearly(scene.at(QPoint(27, 20)), yellow), qPrintable(scene.at(QPoint(27, 20)).name()));
    QVERIFY2(nearly(scene.at(QPoint(33, 20)), yellow), qPrintable(scene.at(QPoint(33, 20)).name()));
    QCOMPARE(scene.at(QPoint(30, 20)), QColor(Qt::white));
    QVERIFY(nearly(scene.at(QPoint(25, 20)), QColor(0, 0, 255)));
    // Committed, the surface stands in until new effects land.
    QVERIFY(scene.session.finishBrushImmediately());
    scene.canvas.synchronizeDisplay();
    QVERIFY(scene.session.effectsPreviews.rendered(scene.square).value().placement);
    QVERIFY(nearly(scene.at(QPoint(27, 20)), yellow));
    QTRY_VERIFY(!scene.session.effectsPreviews.rendered(scene.square).value().placement);
    QVERIFY(nearly(scene.at(QPoint(27, 20)), yellow));
}

void MaskEffectsCanvasTests::aPlacedMaskRedoesThemInTheLayersGrid()
{
    Scene scene;
    scene.session.setEffects(outline(2));
    scene.session.addLayerMask(true);
    // A white mask in the layer's grid, placed on it.
    rewrite(scene.session, [&](ProjectSnapshot &snapshot) {
        ProjectLayerRecord &square = record(snapshot, scene.square);
        QImage white = BrushRaster::context(10, 10, true);
        white.fill(255);
        snapshot.masks.insert_or_assign(scene.square, ImportedImage(white, white, QStringLiteral("Mask")));
        square.maskPlacement = square.transform;
        square.maskLinked = false;
    });
    scene.session.selectLayer(scene.square);
    QVERIFY(layerWith(scene.session, scene.square).mask.value().placement);
    scene.landed();
    paintHole(scene);
    QVERIFY2(nearly(scene.at(QPoint(27, 20)), yellow), qPrintable(scene.at(QPoint(27, 20)).name()));
    QVERIFY2(nearly(scene.at(QPoint(33, 20)), yellow), qPrintable(scene.at(QPoint(33, 20)).name()));
    QCOMPARE(scene.at(QPoint(30, 20)), QColor(Qt::white));
    QVERIFY(scene.session.finishBrushImmediately());
    scene.canvas.synchronizeDisplay();
    QVERIFY(nearly(scene.at(QPoint(27, 20)), yellow));
}

void MaskEffectsCanvasTests::untouchedTilesKeepTheOldMask()
{
    // A layer wider than a tile, painted at one end.
    EditorSession session;
    CanvasView canvas{session};
    session.createDocument(700, 40);
    canvas.resize(700, 40);
    session.viewport.resize(QSizeF(700, 40), 1, QSizeF(700, 40));
    session.zoom(1);
    const QImage blue = solid(600, 10, qRgba(0, 0, 255, 255));
    session.insert(ImportedImage(blue, blue, QStringLiteral("Wide")), QPointF(350, 20));
    session.setEffects(outline(2));
    session.addLayerMask(true);
    canvas.grab();
    session.selectLayerTarget(session.activeLayerID().value(), true);
    session.selectTool(NavigationTool::brush);
    session.setMaskPaintWhite(false);
    session.beginBrush(QPointF(100, 20));
    const QImage shot = canvas.grab().toImage();
    const QPointF far = session.viewport.viewPoint(QPointF(600.5, 20.5), QSizeF(700, 40));
    QCOMPARE(shot.pixelColor(far.toPoint()), QColor(0, 0, 255));
}

void MaskEffectsCanvasTests::aFinerPlacedMaskMapsIntoTheLayersGrid()
{
    // The committed mask's landed effects are the oracle.
    Scene scene;
    scene.session.setEffects(outline(2));
    scene.session.addLayerMask(true);
    rewrite(scene.session, [&](ProjectSnapshot &snapshot) {
        ProjectLayerRecord &square = record(snapshot, scene.square);
        QImage white = BrushRaster::context(600, 600, true);
        white.fill(255);
        snapshot.masks.insert_or_assign(scene.square, ImportedImage(white, white, QStringLiteral("Mask")));
        square.maskPlacement = square.transform;
        square.maskLinked = false;
    });
    scene.session.selectLayer(scene.square);
    scene.landed();
    scene.session.selectLayerTarget(scene.square, true);
    scene.session.selectTool(NavigationTool::brush);
    scene.session.setMaskPaintWhite(false);
    BrushSettings settings = scene.session.brushSettings();
    settings.diameter = 1;
    settings.hardness = 1;
    scene.session.setBrushSettings(settings);
    scene.session.beginBrush(QPointF(32, 16));
    scene.shot();
    scene.session.continueBrush(QPointF(32, 24));
    const QImage painting = scene.shot();
    // The oracle: a whole render through the stroke's mask.
    const ImageLayer square = layerWith(scene.session, scene.square);
    const QImage preview = scene.session.brushStroke()->placedMaskPreview(scene.session.brushStroke()->paintTransform).value();
    const LayerEffectsRenderer::Rendered expected = LayerEffectsRenderer::render(square.asset.value().image(), preview, outline(2));
    int largest = 0;
    for (int y = 10; y < 30; ++y) {
        for (int x = 20; x < 40; ++x) {
            const QPoint at(x - 25 + int(expected.inset), y - 15 + int(expected.inset));
            if (!expected.image.rect().contains(at))
                continue;
            const QColor over = expected.image.pixelColor(at);
            const QColor shown = painting.pixelColor(scene.shown(QPointF(x + 0.5, y + 0.5)));
            // Over white, premultiplied: the channel plus what shows through.
            const auto on = [&](int channel) { return channel + 255 - over.alpha(); };
            largest = std::max({largest, std::abs(shown.red() - on(qRound(over.redF() * over.alphaF() * 255))),
                                std::abs(shown.green() - on(qRound(over.greenF() * over.alphaF() * 255))),
                                std::abs(shown.blue() - on(qRound(over.blueF() * over.alphaF() * 255)))});
        }
    }
    QVERIFY2(largest <= 2, qPrintable(QString::number(largest)));
}

QTEST_MAIN(MaskEffectsCanvasTests)
#include "MaskEffectsCanvasTests.moc"
