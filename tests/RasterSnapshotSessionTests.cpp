#include "BrushFixtures.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore.h"
#include "Rendering/RasterSnapshot.h"
#include "SessionFixtures.h"
#include <QTemporaryDir>

// Swift's RasterSnapshotTests through the session: no mouse-up flattens.
namespace {
std::unique_ptr<EditorSession> session()
{
    auto made = std::make_unique<EditorSession>();
    made->createDocument(4000, 4000, true);
    made->addBlankLayer();
    made->selectTool(NavigationTool::brush);
    made->setBrushSettings(brush(800, 0, 1, 1, 1));
    return made;
}

QImage drawn(const ImportedImage &asset, const LayerTransform &transform, int width, int height, double opacity = 1)
{
    QImage context = BrushRaster::context(width, height, false);
    QPainter painter(&context);
    LayerRenderer::draw(asset.image(), transform, transform.center(), painter, {.opacity = opacity});
    return context;
}

int spread(const QImage &image, int y, int from, int to)
{
    int low = 255, high = 0;
    for (int x = from; x <= to; ++x) {
        low = std::min(low, alpha(image, x, y));
        high = std::max(high, alpha(image, x, y));
    }
    return high - low;
}
}

class RasterSnapshotSessionTests : public QObject {
    Q_OBJECT
private slots:
    void mouseUpAndNextStrokeNeverFlattenTheDocument();
    void snapshotsStayImmutableAndDisplayMatchesExportAcrossSuccessiveStrokes();
    void maskMouseUpIsImmediateAndPreservesTheImage();
    void theSoftStrokeStaysContinuousInASnapshot();
    void eightHundredPixelSoftStrokeHasNoPeriodicRidges();
};

void RasterSnapshotSessionTests::mouseUpAndNextStrokeNeverFlattenTheDocument()
{
    const auto made = session();
    made->beginBrush(QPointF(800, 3200));
    made->continueBrush(QPointF(800, 800));
    made->continueBrush(QPointF(3200, 800));
    QVERIFY(made->finishBrushImmediately());
    const ImportedImage first = made->activeLayer().value().asset.value();
    QVERIFY(first.raster && !first.raster->hasMaterializedPixels());
    QVERIFY(made->canPaint() && made->canEditLayers() && !made->isProjectBusy());
    const int count = made->history.undoCount();
    made->beginBrush(QPointF(1600, 1600));
    made->continueBrush(QPointF(2200, 2200));
    QVERIFY(made->finishBrushImmediately());
    const ImportedImage second = made->activeLayer().value().asset.value();
    QVERIFY(!first.raster->hasMaterializedPixels());
    QVERIFY(second.raster && !second.raster->hasMaterializedPixels());
    QCOMPARE(made->history.undoCount(), count + 1);
    made->undo();
    QCOMPARE(made->activeLayer().value().asset.value().identity(), first.identity());
    made->redo();
    QCOMPARE(made->activeLayer().value().asset.value().identity(), second.identity());
    made->selectTool(NavigationTool::move);
    QVERIFY(made->tool() == NavigationTool::move && made->canTransform());
}

void RasterSnapshotSessionTests::snapshotsStayImmutableAndDisplayMatchesExportAcrossSuccessiveStrokes()
{
    const auto made = session();
    BrushSettings settings = made->brushSettings();
    settings.opacity = 0.5;
    const std::vector<QPointF> points{QPointF(1400, 1600), QPointF(1200, 1600), QPointF(1800, 1700)};
    for (size_t index = 0; index < points.size(); ++index) {
        settings.red = index % 2;
        made->setBrushSettings(settings);
        made->beginBrush(points[index]);
        made->continueBrush(points[index] + QPointF(800, 200));
        QVERIFY(made->finishBrushImmediately());
    }
    const ImageLayer layer = made->activeLayer().value();
    const ImportedImage asset = layer.asset.value();
    QVERIFY(asset.raster);
    // Tile clips apply the opacity once past shifted crops.
    QImage display = BrushRaster::context(4000, 4000, false);
    {
        QPainter painter(&display);
        LayerRenderer::drawBrushPreview(QImage(), layer.transform, layer.transform.center(), painter, {.opacity = 0.5},
                                        {.patches = {}, .pixelWidth = asset.raster->width, .pixelHeight = asset.raster->height, .paintingMask = false, .raster = asset.raster});
    }
    QVERIFY(!asset.raster->hasMaterializedPixels());
    const QImage reference = drawn(asset, layer.transform, 4000, 4000, 0.5);
    QVERIFY(asset.raster->hasMaterializedPixels());
    QCOMPARE(display, reference);
    made->beginBrush(QPointF(1800, 1700));
    QVERIFY(made->finishBrushImmediately());
    QCOMPARE(drawn(asset, layer.transform, 4000, 4000, 0.5), reference);
    // Saving and reopening consume the lazy image the usual way.
    QTemporaryDir folder;
    const QString path = folder.filePath(QStringLiteral("SparseBrush.comp"));
    ProjectStore::save(made->projectSnapshot().value(), path);
    QCOMPARE(ImageExporter::render(ProjectStore::load(path)).image.size(), QSize(4000, 4000));
}

void RasterSnapshotSessionTests::maskMouseUpIsImmediateAndPreservesTheImage()
{
    const auto made = session();
    made->beginBrush(QPointF(2000, 2000));
    QVERIFY(made->finishBrushImmediately());
    const ImageIdentity image = made->activeLayer().value().asset.value().identity();
    made->addLayerMask();
    BrushSettings settings = made->brushSettings();
    settings.diameter = 300;
    made->setBrushSettings(settings);
    made->beginBrush(QPointF(2000, 2000));
    made->continueBrush(QPointF(2200, 2000));
    QVERIFY(made->finishBrushImmediately());
    QVERIFY(made->canPaint() && !made->isProjectBusy());
    QCOMPARE(made->activeLayer().value().asset.value().identity(), image);
    const ImportedImage mask = made->activeLayer().value().mask.value().asset;
    QVERIFY(mask.raster && !mask.raster->hasMaterializedPixels());
    made->beginBrush(QPointF(2000, 2100));
    QVERIFY(made->finishBrushImmediately());
    QVERIFY(!mask.raster->hasMaterializedPixels());
    QVERIFY(LayerMask::isValid(made->activeLayer().value().mask.value().asset.image()));
}

void RasterSnapshotSessionTests::theSoftStrokeStaysContinuousInASnapshot()
{
    const auto made = session();
    BrushStroke stroke(made->activeLayer().value(), false, brush(120, 0, 1, 0, 0), QSizeF(4000, 4000));
    stroke.append(QPointF(500, 1000));
    stroke.append(QPointF(1300, 1000));
    stroke.flush();
    const PaintSnapshot result = stroke.paintSnapshot();
    const QImage image = drawn(result.asset, result.transform, 1400, 1100);
    for (int y : {1000, 1030, 1050})
        QVERIFY2(spread(image, y, 700, 1100) <= 2, qPrintable(QStringLiteral("row %1 spread %2").arg(y).arg(spread(image, y, 700, 1100))));
}

void RasterSnapshotSessionTests::eightHundredPixelSoftStrokeHasNoPeriodicRidges()
{
    const auto made = session();
    made->beginBrush(QPointF(600, 1600));
    made->continueBrush(QPointF(3400, 1600));
    QVERIFY(made->finishBrushImmediately());
    const ImageLayer layer = made->activeLayer().value();
    const QImage image = drawn(layer.asset.value(), layer.transform, 4000, 4000);
    for (int y : {1600, 1700, 1800, 1900, 1980})
        QVERIFY2(spread(image, y, 1000, 3000) <= 1, qPrintable(QStringLiteral("row %1 spread %2").arg(y).arg(spread(image, y, 1000, 3000))));
}

QTEST_GUILESS_MAIN(RasterSnapshotSessionTests)
#include "RasterSnapshotSessionTests.moc"
