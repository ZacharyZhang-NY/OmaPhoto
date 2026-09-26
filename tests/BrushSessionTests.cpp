#include "BrushFixtures.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore.h"
#include "SelectionFixtures.h"
#include "SessionRecord.h"
#include <QTemporaryDir>

// The session's brush: Swift's BrushTests through the session.
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

BrushSettings withTip(BrushSettings settings, double diameter, double hardness)
{
    settings.diameter = diameter;
    settings.hardness = hardness;
    return settings;
}
}

class BrushSessionTests : public QObject {
    Q_OBJECT
private slots:
    void continuousStrokeCrossesTilesAndCommitsOneUndo();
    void softBrushProducesPartialAlphaAndCancelPreservesDocument();
    void softMaskPaintingPreviewMatchesCommitAndPersists();
    void brushStaysCircularOnNonuniformRotatedFlippedLayer();
    void maskedImagePaintingUsesCoverageAndOpacityOnlyOnce();
    void importedImageLayerExpandsAcrossCanvasWithoutMovingImageOrMask();
    void paintedBoundsTrimTilePaddingAndKeepSoftEdges();
    void opacityCapsTheWholeStrokeEvenWhereItOverlapsItself();
    void liveStrokeReachesNewestSampleAndTailIsReplacedExactly();
    void opacityAppliesToMaskPainting();
    void shiftBracketsStepHardnessByQuarters();
    void numberKeysSetBrushOpacity();
    void largeBlankCanvasOnlyAllocatesTouchedTilesUntilCommit();
    void foldersHiddenLayersAndDisabledMasksRejectPainting();
};

void BrushSessionTests::continuousStrokeCrossesTilesAndCommitsOneUndo()
{
    const auto session = makeSession();
    const int count = session->history.undoCount();
    session->beginBrush(QPointF(20, 40));
    session->continueBrush(QPointF(580, 40));
    QVERIFY(session->brushStroke());
    QCOMPARE(session->brushStroke()->patches().size(), size_t(3));
    QVERIFY(!session->activeLayer().value().asset.has_value() && session->history.undoCount() == count);
    const QImage shown = live(*session);
    for (int x : {20, 255, 256, 511, 512, 579})
        QCOMPARE(pixel(shown, x, 40), (std::vector<int>{255, 0, 0, 255}));
    session->finishBrush();
    QVERIFY(!session->brushError().has_value() && !session->brushStroke());
    QCOMPARE(session->history.undoCount(), count + 1);
    QCOMPARE(session->history.undoName(), QString("Brush Stroke"));
    const QImage result = render(*session);
    for (int x : {20, 255, 256, 511, 512, 579})
        QCOMPARE(pixel(result, x, 40), (std::vector<int>{255, 0, 0, 255}));
    QCOMPARE(alpha(result, 300, 0), 0);
    session->undo();
    QVERIFY(!session->activeLayer().value().asset.has_value());
    session->redo();
    QVERIFY(session->activeLayer().value().asset.has_value());
}

void BrushSessionTests::softBrushProducesPartialAlphaAndCancelPreservesDocument()
{
    const auto session = makeSession(80, 80);
    session->setBrushSettings(withTip(session->brushSettings(), 40, 0));
    const CanvasDocument before = session->document().value();
    session->beginBrush(QPointF(40, 40));
    const QImage shown = live(*session);
    QVERIFY(alpha(shown, 40, 40) > 230);
    const int half = alpha(shown, 50, 40);
    QVERIFY(half > 95 && half < 140);
    QVERIFY(alpha(shown, 57, 40) < 40);
    QCOMPARE(alpha(shown, 64, 40), 0);
    session->cancelBrush();
    QVERIFY(session->document().value() == before);
    session->beginBrush(QPointF(-100, -100));
    session->finishBrush();
    QVERIFY(session->document().value() == before);
}

void BrushSessionTests::softMaskPaintingPreviewMatchesCommitAndPersists()
{
    const auto session = makeSession(80, 80);
    session->setBrushSettings(withTip(session->brushSettings(), 200, 1));
    session->beginBrush(QPointF(40, 40));
    session->finishBrush();
    const ImageIdentity source = session->activeLayer().value().asset.value().identity();
    session->addLayerMask();
    QVERIFY(session->isMaskSelected());
    session->setBrushSettings(withTip(session->brushSettings(), 40, 0));
    session->beginBrush(QPointF(40, 40));
    const QImage shown = live(*session);
    session->finishBrush();
    QCOMPARE(session->history.undoName(), QString("Paint Mask"));
    const QImage result = render(*session);
    for (int x : {10, 40, 54, 70})
        QVERIFY2(std::abs(alpha(result, x, 40) - alpha(shown, x, 40)) <= 1, qPrintable(QStringLiteral("x %1: %2 vs %3").arg(x).arg(alpha(result, x, 40)).arg(alpha(shown, x, 40))));
    QVERIFY(alpha(result, 40, 40) < 25);
    QCOMPARE(session->activeLayer().value().asset.value().identity(), source);
    QCOMPARE(session->activeLayer().value().mask.value().asset.size().width(), 80);
    session->setMaskPaintWhite(true);
    session->setBrushSettings(withTip(session->brushSettings(), 40, 1));
    session->beginBrush(QPointF(40, 40));
    session->finishBrush();
    QCOMPARE(alpha(render(*session), 40, 40), 255);
    session->undo();
    QVERIFY(session->isMaskSelected());
    QVERIFY(alpha(render(*session), 40, 40) < 25);
    session->redo();
    QVERIFY(session->isMaskSelected());
    // Through the project format and back.
    QTemporaryDir folder;
    const QString path = folder.filePath(QStringLiteral("Brush.comp"));
    ProjectStore::save(session->projectSnapshot().value(), path);
    QCOMPARE(alpha(ImageExporter::render(ProjectStore::load(path)).image, 40, 40), 255);
}

void BrushSessionTests::brushStaysCircularOnNonuniformRotatedFlippedLayer()
{
    const auto session = makeSession(100, 100);
    QImage source = BrushRaster::context(100, 100, false);
    QPainter painter(&source);
    painter.fillRect(QRect(2, 2, 6, 6), Qt::red);
    painter.end();
    session->insert(ImportedImage(source, source, "Fixture"));
    const QUuid id = session->activeLayerID().value();
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        record(snapshot, id).transform = LayerTransform{.origin = {25, -50}, .size = {50, 200}, .rotation = 90, .flipX = true, .flipY = true, .sampling = LayerSampling::nearest};
    });
    session->selectLayer(id);
    session->selectTool(NavigationTool::brush);
    session->setBrushSettings(brush(16, 1, 0, 1, 0));
    session->beginBrush(QPointF(50, 50));
    const QImage shown = live(*session);
    session->finishBrush();
    const QImage result = render(*session);
    for (const auto &[x, y] : std::vector<std::pair<int, int>>{{50, 50}, {54, 50}, {50, 54}}) {
        QVERIFY(pixel(result, x, y)[1] > 240);
        QVERIFY(pixel(shown, x, y)[1] > 240);
    }
    for (const auto &[x, y] : std::vector<std::pair<int, int>>{{61, 50}, {50, 61}})
        QCOMPARE(alpha(result, x, y), 0);
}

void BrushSessionTests::maskedImagePaintingUsesCoverageAndOpacityOnlyOnce()
{
    const auto session = makeSession(520, 40);
    const QUuid id = session->activeLayerID().value();
    rewrite(*session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, id, gray(128)); });
    session->selectLayerTarget(id, false);
    session->selectTool(NavigationTool::brush);
    session->setLayerOpacity(0.5);
    session->beginBrush(QPointF(240, 20));
    session->continueBrush(QPointF(280, 20));
    const QImage shown = live(*session);
    session->finishBrush();
    const QImage result = render(*session);
    for (int x : {250, 255, 256, 260}) {
        QVERIFY2(std::abs(alpha(shown, x, 20) - 64) <= 1, qPrintable(QString::number(alpha(shown, x, 20))));
        QVERIFY2(std::abs(alpha(result, x, 20) - 64) <= 1, qPrintable(QString::number(alpha(result, x, 20))));
    }
}

void BrushSessionTests::importedImageLayerExpandsAcrossCanvasWithoutMovingImageOrMask()
{
    for (double rotation : {0.0, 37.0, 90.0}) {
        const auto session = makeSession(600, 200);
        QImage image = BrushRaster::context(40, 20, false);
        image.fill(Qt::red);
        session->insert(ImportedImage(image, image, "Imported"));
        const QUuid id = session->activeLayerID().value();
        rewrite(*session, [&](ProjectSnapshot &snapshot) {
            record(snapshot, id).transform = LayerTransform{.origin = {240, 80}, .size = {80, 40}, .rotation = rotation, .flipX = true, .sampling = LayerSampling::nearest};
            setMask(snapshot, id, gray(128));
        });
        session->selectLayerTarget(id, false);
        session->selectTool(NavigationTool::brush);
        const CanvasDocument original = session->document().value();
        session->setBrushSettings(brush(16, 1, 0, 1, 0));
        session->beginBrush(QPointF(12, 12));
        session->continueBrush(QPointF(580, 12));
        const QImage shown = live(*session);
        QVERIFY(pixel(shown, 12, 12)[1] > 240 && pixel(shown, 580, 12)[1] > 240);
        session->finishBrush();
        QVERIFY(!session->brushError().has_value());
        const QImage result = render(*session);
        QVERIFY(pixel(result, 12, 12)[1] > 240 && pixel(result, 580, 12)[1] > 240);
        const std::vector<int> centre = pixel(result, 280, 100);
        QVERIFY2(std::abs(centre[0] - 128) <= 1 && std::abs(centre[3] - 128) <= 1, qPrintable(QStringLiteral("%1 %2").arg(centre[0]).arg(centre[3])));
        const ImageLayer layer = session->activeLayer().value();
        QVERIFY(layer.transform.rotation == rotation && layer.transform.flipX);
        session->undo();
        QVERIFY(session->document().value() == original);
        session->redo();
        QTemporaryDir folder;
        const QString path = folder.filePath(QStringLiteral("Expanded.comp"));
        ProjectStore::save(session->projectSnapshot().value(), path);
        QVERIFY(pixel(ImageExporter::render(ProjectStore::load(path)).image, 12, 12)[1] > 240);
    }
}

void BrushSessionTests::paintedBoundsTrimTilePaddingAndKeepSoftEdges()
{
    for (double hardness : {0.0, 1.0}) {
        const auto session = makeSession(600, 200);
        session->setBrushSettings(withTip(session->brushSettings(), 20, hardness));
        session->beginBrush(QPointF(300, 100));
        const QImage shown = live(*session);
        session->finishBrush();
        const ImageLayer layer = session->activeLayer().value();
        const QImage image = layer.asset.value().image();
        QVERIFY(image.width() <= 20 && image.height() <= 20);
        QVERIFY(layer.transform.origin.x() >= 290 && layer.transform.origin.y() >= 90);
        const QImage result = render(*session);
        for (int x = 288; x <= 312; ++x)
            QCOMPARE(pixel(result, x, 100), pixel(shown, x, 100));
        session->undo();
        QVERIFY(!session->activeLayer().value().asset.has_value());
        session->redo();
        QCOMPARE(session->activeLayer().value().transform, layer.transform);
    }
}

void BrushSessionTests::opacityCapsTheWholeStrokeEvenWhereItOverlapsItself()
{
    const auto session = makeSession(200, 80);
    session->setBrushSettings(brush(20, 1, 1, 0, 0, 0.5));
    session->beginBrush(QPointF(20, 40));
    for (int x : {180, 20, 180, 20, 100})
        session->continueBrush(QPointF(x, 40));
    const std::vector<int> shown = pixel(live(*session), 100, 40);
    QVERIFY(std::abs(shown[3] - 128) <= 1 && std::abs(shown[0] - 128) <= 1 && shown[1] == 0);
    session->finishBrush();
    const QImage result = render(*session);
    QCOMPARE(pixel(result, 100, 40), shown);
    QCOMPARE(alpha(result, 100, 0), 0);
}

void BrushSessionTests::liveStrokeReachesNewestSampleAndTailIsReplacedExactly()
{
    const auto session = makeSession(300, 120);
    session->setBrushSettings(withTip(session->brushSettings(), 8, 1));
    session->beginBrush(QPointF(20, 60));
    session->continueBrush(QPointF(150, 20));
    session->continueBrush(QPointF(280, 60));
    QCOMPARE(alpha(live(*session), 278, 60), 255);
    session->finishBrush();
    const QImage result = render(*session);
    QCOMPARE(alpha(result, 215, 40), 0);
    QCOMPARE(alpha(result, 278, 60), 255);
}

void BrushSessionTests::opacityAppliesToMaskPainting()
{
    const auto session = makeSession(80, 80);
    session->setBrushSettings(withTip(session->brushSettings(), 200, 1));
    session->beginBrush(QPointF(40, 40));
    session->continueBrush(QPointF(41, 40));
    session->finishBrush();
    session->addLayerMask(true);
    session->selectLayerTarget(session->activeLayerID().value(), true);
    session->setBrushSettings(brush(20, 1, 1, 0, 0, 0.5));
    session->setMaskPaintWhite(false);
    session->beginBrush(QPointF(40, 40));
    session->continueBrush(QPointF(42, 40));
    session->finishBrush();
    const QImage result = render(*session);
    QVERIFY(std::abs(alpha(result, 40, 40) - 128) <= 1);
    QCOMPARE(alpha(result, 5, 5), 255);
}

void BrushSessionTests::shiftBracketsStepHardnessByQuarters()
{
    const auto session = makeSession();
    session->setBrushSettings(withTip(session->brushSettings(), 20, 0.8));
    session->changeBrushHardness(true);
    QCOMPARE(session->brushSettings().hardness, 1.0);
    session->changeBrushHardness(true);
    QCOMPARE(session->brushSettings().hardness, 1.0);
    session->setBrushSettings(withTip(session->brushSettings(), 20, 0.8));
    session->changeBrushHardness(false);
    QCOMPARE(session->brushSettings().hardness, 0.75);
    for (int i = 0; i < 5; ++i)
        session->changeBrushHardness(false);
    QCOMPARE(session->brushSettings().hardness, 0.0);
    session->changeBrushHardness(true);
    QCOMPARE(session->brushSettings().hardness, 0.25);
    // A fifth a step, at least a pixel, in 1…2000.
    session->setBrushSettings(withTip(session->brushSettings(), 2, 1));
    session->changeBrushSize(false);
    QCOMPARE(session->brushSettings().diameter, 1.0);
    session->changeBrushSize(false);
    QCOMPARE(session->brushSettings().diameter, 1.0);
    session->changeBrushSize(true);
    QCOMPARE(session->brushSettings().diameter, 2.0);
    session->setBrushSettings(withTip(session->brushSettings(), 100, 1));
    session->changeBrushSize(true);
    QCOMPARE(session->brushSettings().diameter, 120.0);
    session->setBrushSettings(withTip(session->brushSettings(), 1990, 1));
    session->changeBrushSize(true);
    QCOMPARE(session->brushSettings().diameter, 2000.0);
}

void BrushSessionTests::numberKeysSetBrushOpacity()
{
    const auto session = makeSession();
    session->typeOpacityDigit(5, 10);
    QCOMPARE(session->brushSettings().opacity, 0.5);
    session->typeOpacityDigit(0, 20);
    QCOMPARE(session->brushSettings().opacity, 1.0);
    session->typeOpacityDigit(4, 30);
    session->typeOpacityDigit(5, 30.3);
    QCOMPARE(session->brushSettings().opacity, 0.45);
    session->typeOpacityDigit(0, 40);
    session->typeOpacityDigit(5, 40.2);
    QCOMPARE(session->brushSettings().opacity, 0.05);
    // Move's digits go to the layers, not the tip.
    session->selectTool(NavigationTool::move);
    session->typeOpacityDigit(3, 60);
    QCOMPARE(session->brushSettings().opacity, 0.05);
    QCOMPARE(session->activeLayer().value().opacity, 0.3);
    session->selectTool(NavigationTool::lasso);
    session->typeOpacityDigit(7, 70);
    QCOMPARE(session->activeLayer().value().opacity, 0.3);
    // Without a time the clock is the session's own.
    session->selectTool(NavigationTool::brush);
    session->typeOpacityDigit(9);
    QCOMPARE(session->brushSettings().opacity, 0.9);
}

void BrushSessionTests::largeBlankCanvasOnlyAllocatesTouchedTilesUntilCommit()
{
    const auto session = makeSession(10000, 10000);
    session->beginBrush(QPointF(100, 100));
    const std::vector<BrushPatch> patches = session->brushStroke()->patches();
    QCOMPARE(patches.size(), size_t(1));
    QVERIFY(patches[0].image.sizeInBytes() <= 256 * 256 * 4);
    QVERIFY(!session->activeLayer().value().asset.has_value());
    session->cancelBrush();
}

void BrushSessionTests::foldersHiddenLayersAndDisabledMasksRejectPainting()
{
    const auto session = makeSession();
    session->addGroup();
    session->beginBrush(QPointF(10, 10));
    QVERIFY(!session->brushStroke());
    session->selectLayer(session->document().value().layers.front().id);
    session->toggleLayerVisibility(session->activeLayerID().value());
    QVERIFY(!session->canPaint());
    session->toggleLayerVisibility(session->activeLayerID().value());
    QVERIFY(session->canPaint());
    session->addLayerMask();
    session->toggleLayerMask();
    QVERIFY(!session->canPaint());
    // Another tool paints nothing; an empty selection neither.
    session->toggleLayerMask();
    session->selectTool(NavigationTool::move);
    session->beginBrush(QPointF(10, 10));
    QVERIFY(!session->brushStroke());
    session->selectTool(NavigationTool::brush);
    session->applySelection(rectPath(QRectF(0, 0, 10, 10)), SelectionMode::replace, "Select");
    session->applySelection(rectPath(QRectF(0, 0, 600, 80)), SelectionMode::subtract, "Subtract");
    QVERIFY(!session->canPaint());
}

QTEST_GUILESS_MAIN(BrushSessionTests)
#include "BrushSessionTests.moc"
