#include "Document/DocumentLimits.h"
#include "AddressSpaceLimit.h"
#include "Document/EditorSession.h"
#include "Document/LayerMask.h"
#include "IO/ImageExporter.h"
#include "IO/ImageResizer.h"
#include "ProjectFixtures.h"
#include <QBuffer>
#include <QImageReader>
#include <QTemporaryDir>
#include <QtTest>

namespace {
// A 64x32 layer, red left, clear right, filling its document.
ProjectSnapshot imported()
{
    const QUuid id = QUuid::createUuid();
    const ProjectLayerRecord record{.id = id, .name = "fixture", .isVisible = true, .transform = {.origin = {0, 0}, .size = {64, 32}},
                                    .imageFile = uuidString(id) + ".png"};
    return {.manifest = {.documentID = QUuid::createUuid(), .width = 64, .height = 32, .activeLayerID = id, .layers = {record}},
            .images = {{id, asset(halfRed(), "fixture")}}};
}

std::optional<ProjectError::Kind> resizeError(const ProjectSnapshot &snapshot, const ImageSizeOptions &options)
{
    return projectError([&] { ImageResizer::resize(snapshot, options); });
}
}

class ImageSizeTests : public QObject {
    Q_OBJECT
private slots:
    void resizePreservesLayerIdentityAndUndoRestoresSource();
    void resolutionOnlyRetainsPixelsAndSurvivesSaveAndExport();
    void rotatedHiddenLayerScalesInDocumentAxesAndInvalidSizeIsRejected();
    void limitsAreJudgedAtTheirEdges();
    void budgetsCountEveryLayer();
    void missingPixelsAreRefused();
    void masksScaleWithTheirLayers();
    void solidAndPlacedMasksKeepTheirPixels();
    void recordsKeepEveryField_data();
    void recordsKeepEveryField();
    void thumbnailsTruncate_data();
    void thumbnailsTruncate();
    void aSurfaceThatCannotBeAllocatedThrows();
};

void ImageSizeTests::resizePreservesLayerIdentityAndUndoRestoresSource()
{
    const ProjectSnapshot input = imported();
    const QUuid id = input.manifest.layers[0].id;
    QTest::ignoreMessage(QtInfoMsg, "resized 64 x 32 to 128 x 96");
    const ProjectSnapshot result = ImageResizer::resize(input, {.width = 128, .height = 96, .resolution = 300, .sampling = LayerSampling::nearest});
    QCOMPARE(result.manifest.width, qint64(128));
    QCOMPARE(result.manifest.height, qint64(96));
    QCOMPARE(result.manifest.resolution, std::optional(300.0));
    QCOMPARE(result.manifest.documentID, input.manifest.documentID);
    QCOMPARE(result.manifest.activeLayerID, std::optional(id));
    const ProjectLayerRecord &layer = result.manifest.layers[0];
    QCOMPARE(layer.id, id);
    QCOMPARE(layer.transform, (LayerTransform{.origin = {0, 0}, .size = {128, 96}, .sampling = LayerSampling::nearest}));
    const ImportedImage &scaled = result.images.at(id);
    QCOMPARE(scaled.name, QString("fixture"));
    QCOMPARE(scaled.size(), QSize(128, 96));
    QCOMPARE(scaled.image().format(), QImage::Format_RGBA8888_Premultiplied);
    // Nearest keeps the edge hard: red to 63, clear after.
    QCOMPARE(scaled.image().pixelColor(0, 0), QColor(255, 0, 0));
    QCOMPARE(scaled.image().pixelColor(63, 95), QColor(255, 0, 0));
    QCOMPARE(scaled.image().pixelColor(64, 0).alpha(), 0);
    QCOMPARE(scaled.image().pixelColor(127, 0).alpha(), 0);
    // The source is untouched.
    QCOMPARE(input.images.at(id).size(), QSize(64, 32));
    // In the session: one step; undo gives the source back.
    EditorSession session;
    session.installProject(input, "fixture.comp");
    const std::optional<CanvasDocument> original = session.document();
    session.applyImageSize(result);
    QCOMPARE(session.document().value().size(), QSizeF(128, 96));
    QCOMPARE(session.document().value().resolution, 300.0);
    QCOMPARE(session.activeLayerID(), std::optional(id));
    QCOMPARE(session.document().value().layers.front().asset.value().size(), QSize(128, 96));
    QCOMPARE(session.history.undoName(), QString("Image Size"));
    session.undo();
    QCOMPARE(session.document(), original);
    QVERIFY(session.document().value().layers.front().asset.value().identity() == input.images.at(id).identity());
    session.redo();
    QCOMPARE(session.document().value().resolution, 300.0);
    // One side alone is a resize too.
    QCOMPARE(ImageResizer::resize(input, {.width = 128, .height = 32, .resolution = 72}).images.at(id).size(), QSize(128, 32));
    QCOMPARE(ImageResizer::resize(input, {.width = 64, .height = 48, .resolution = 72}).images.at(id).size(), QSize(64, 48));
}

void ImageSizeTests::resolutionOnlyRetainsPixelsAndSurvivesSaveAndExport()
{
    ProjectSnapshot before = imported();
    before.manifest.layers[0].transform = {.origin = {3.5, -2}, .size = {40, 20}, .rotation = 30, .flipX = true, .sampling = LayerSampling::smooth};
    const QUuid id = before.manifest.layers[0].id;
    before.manifest.layers[0].maskFile = uuidString(id) + ".mask.png";
    before.masks.insert({id, LayerMask::assetFrom(gray(64, 32, 200))});
    const ProjectSnapshot resized = ImageResizer::resize(before, {.width = 64, .height = 32, .resolution = 300});
    QCOMPARE(resized.manifest.layers[0].transform, before.manifest.layers[0].transform);
    QCOMPARE(resized.images.at(id).identity(), before.images.at(id).identity());
    // The mask and its record come through untouched too.
    QCOMPARE(resized.masks.at(id).identity(), before.masks.at(id).identity());
    QCOMPARE(resized.manifest.layers[0].maskFile, before.manifest.layers[0].maskFile);
    QCOMPARE(resized.manifest.resolution, std::optional(300.0));
    // In the session a new resolution alone is one step.
    EditorSession session;
    session.installProject(before, "fixture.comp");
    QCOMPARE(session.document().value().resolution, 72.0);
    session.applyImageSize(resized);
    QCOMPARE(session.document().value().resolution, 300.0);
    QCOMPARE(session.document().value().size(), QSizeF(64, 32));
    QCOMPARE(session.history.undoCount(), 1);
    session.undo();
    QCOMPARE(session.document().value().resolution, 72.0);
    QTemporaryDir folder;
    ProjectStore::save(resized, folder.filePath("Size.comp"));
    const ProjectSnapshot loaded = ProjectStore::load(folder.filePath("Size.comp"));
    QCOMPARE(loaded.manifest.resolution, std::optional(300.0));
    QByteArray png = ImageExporter::pngData(loaded);
    QBuffer buffer(&png);
    QImageReader reader(&buffer);
    // 300 pixels an inch is 11,811 dots a metre.
    QCOMPARE(reader.read().dotsPerMeterX(), 11'811);
}

void ImageSizeTests::rotatedHiddenLayerScalesInDocumentAxesAndInvalidSizeIsRejected()
{
    ProjectSnapshot input = imported();
    ProjectLayerRecord &record = input.manifest.layers[0];
    record.isVisible = false;
    record.transform = {.origin = {-16, 4}, .size = {64, 32}, .rotation = 90};
    const ProjectSnapshot result = ImageResizer::resize(input, {.width = 128, .height = 96, .resolution = 72, .sampling = LayerSampling::nearest});
    const ProjectLayerRecord &output = result.manifest.layers[0];
    QVERIFY(!output.isVisible);
    // Turned: 32x64 at (0, -12); Swift allows a pixel.
    QCOMPARE(output.transform.rotation, 0.0);
    QVERIFY(qAbs(output.transform.size.width() - 64) <= 1 && qAbs(output.transform.size.height() - 192) <= 1);
    QVERIFY(qAbs(output.transform.origin.x()) <= 1 && qAbs(output.transform.origin.y() + 36) <= 1);
    const QImage image = result.images.at(record.id).image();
    QCOMPARE(QSizeF(image.size()), output.transform.size);
    // A quarter turn clockwise: the red half is on top.
    QCOMPARE(image.pixelColor(32, 10), QColor(255, 0, 0));
    QCOMPARE(image.pixelColor(32, 90), QColor(255, 0, 0));
    QCOMPARE(image.pixelColor(32, 102).alpha(), 0);
    // Off the right angles the box is exact: floor, ceil.
    record.transform = {.origin = {10, 10}, .size = {40, 20}, .rotation = 30};
    const ProjectSnapshot leaning = ImageResizer::resize(input, {.width = 128, .height = 96, .resolution = 72});
    QCOMPARE(leaning.manifest.layers[0].transform, (LayerTransform{.origin = {15, 4}, .size = {90, 112}}));
    QCOMPARE(resizeError(input, {.width = 30'000, .height = 30'000, .resolution = 72}), std::optional(ProjectError::Kind::tooLarge));
}

void ImageSizeTests::limitsAreJudgedAtTheirEdges()
{
    ProjectSnapshot input = imported();
    const auto tooLarge = std::optional(ProjectError::Kind::tooLarge);
    QCOMPARE(resizeError(input, {.width = 0, .height = 10, .resolution = 72}), tooLarge);
    QCOMPARE(resizeError(input, {.width = 10, .height = 0, .resolution = 72}), tooLarge);
    QCOMPARE(resizeError(input, {.width = 30'001, .height = 10, .resolution = 72}), tooLarge);
    QCOMPARE(resizeError(input, {.width = 10, .height = 30'001, .resolution = 72}), tooLarge);
    for (const double wild : {0.99, 9600.01, std::nan(""), double(INFINITY)})
        QCOMPARE(resizeError(input, {.width = 10, .height = 10, .resolution = wild}), tooLarge);
    QCOMPARE(resizeError(input, {.width = 10, .height = 10, .resolution = 1}), std::nullopt);
    QCOMPARE(resizeError(input, {.width = 10, .height = 10, .resolution = 9600}), std::nullopt);
    // An empty document scales to the limits for nothing.
    input.manifest.layers.clear();
    input.images.clear();
    // No layer's own rule stands in for the document's here.
    QCOMPARE(resizeError(input, {.width = 0, .height = 10, .resolution = 72}), tooLarge);
    QCOMPARE(resizeError(input, {.width = 10, .height = 0, .resolution = 72}), tooLarge);
    QCOMPARE(resizeError(input, {.width = 30'001, .height = 10, .resolution = 72}), tooLarge);
    QCOMPARE(resizeError(input, {.width = 10, .height = 30'001, .resolution = 72}), tooLarge);
    QCOMPARE(resizeError(input, {.width = 20'000, .height = 10'000, .resolution = 72}), std::nullopt);
    QCOMPARE(resizeError(input, {.width = 20'000, .height = 10'001, .resolution = 72}), tooLarge);
    QCOMPARE(resizeError(input, {.width = 30'000, .height = 1, .resolution = 72}), std::nullopt);
    QCOMPARE(resizeError(input, {.width = 1, .height = 30'000, .resolution = 72}), std::nullopt);
    // The same size skips every other rule: only resolution changes.
    ProjectSnapshot wide = imported();
    wide.manifest.width = 20'000;
    wide.manifest.height = 20'000;
    QCOMPARE(ImageResizer::resize(wide, {.width = 20'000, .height = 20'000, .resolution = 144}).manifest.resolution, std::optional(144.0));
}

void ImageSizeTests::budgetsCountEveryLayer()
{
    // Widened twice: a narrow layer, the remainder; wide, rows.
    const qint64 budget = DocumentLimits::documentPixelBudget();
    const qint64 rows = (budget - 1) / 30'000, rest = budget - rows * 30'000;
    ProjectSnapshot input = imported();
    input.manifest.width = 100;
    input.manifest.height = 100;
    ProjectLayerRecord narrow = input.manifest.layers[0];
    narrow.transform = {.origin = {0, 0}, .size = {double(rest) / 2, 1}};
    ProjectLayerRecord wide = narrow;
    wide.id = QUuid::createUuid();
    wide.imageFile = uuidString(wide.id) + ".png";
    wide.transform = {.origin = {0, 0}, .size = {15'000, double(rows)}};
    const ImageSizeOptions widened{.width = 200, .height = 100, .resolution = 72};
    const auto tooLarge = std::optional(ProjectError::Kind::tooLarge);
    // Exactly the budget passes: the wide layer's missing image fails.
    input.manifest.layers = {narrow, wide};
    QCOMPARE(resizeError(input, widened), std::optional(ProjectError::Kind::missingImage));
    // One pixel more, counted across both layers, does not.
    input.manifest.layers[0].transform.size = {double(rest + 1) / 2, 1};
    QCOMPARE(resizeError(input, widened), tooLarge);
    input.manifest.layers = {wide};
    QCOMPARE(resizeError(input, widened), std::optional(ProjectError::Kind::missingImage));
    // Masks have their own budget, counted the same way.
    input.manifest.layers = {narrow, wide};
    input.manifest.layers[0].transform.size = {double(rest + 1) / 2, 1};
    for (ProjectLayerRecord &layer : input.manifest.layers) {
        layer.imageFile = std::nullopt;
        layer.maskFile = uuidString(layer.id) + ".mask.png";
        input.masks.insert({layer.id, LayerMask::assetFrom(gray(4, 4, 200))});
    }
    QCOMPARE(resizeError(input, widened), tooLarge);
    // Exactly the budget passes: its surface then finds no memory.
    input.manifest.layers[0].transform.size = {double(rest) / 2, 1};
    std::optional<ExportError::Kind> kind;
    {
        const AddressSpaceLimit limit(16ll * 1024 * 1024);
        try {
            ImageResizer::resize(input, widened);
        } catch (const ExportError &error) {
            kind = error.kind;
        }
    }
    QCOMPARE(kind, std::optional(ExportError::Kind::render));
    // Past one surface, within the document's: a mask still fits.
    if (budget > DocumentLimits::maxSurfacePixels + 30'000) {
        const int past = int(DocumentLimits::maxSurfacePixels / 30'000) + 1;
        input.manifest.layers = {input.manifest.layers[1]};
        input.manifest.layers[0].transform.size = {15'000, double(past)};
        QCOMPARE(ImageResizer::resize(input, widened).masks.at(wide.id).size(), QSize(30'000, past));
    }
    // A side past 30,000 is refused though the pixels fit.
    ProjectSnapshot strip = imported();
    strip.manifest.layers[0].transform = {.origin = {0, 0}, .size = {64, 1}};
    strip.manifest.layers.push_back(strip.manifest.layers[0]);
    QCOMPARE(resizeError(strip, {.width = 30'000, .height = 32, .resolution = 72}), std::nullopt);
    strip.manifest.layers[0].transform.size = {64.01, 1};
    QCOMPARE(resizeError(strip, {.width = 30'000, .height = 32, .resolution = 72}), tooLarge);
    // 300 times 100.003 is 30,000.9: one pixel past, either way.
    ProjectSnapshot edge = imported();
    edge.manifest.width = 100;
    edge.manifest.height = 100;
    edge.manifest.layers[0].transform = {.origin = {0, 0}, .size = {100.003, 1}};
    QCOMPARE(resizeError(edge, {.width = 30'000, .height = 100, .resolution = 72}), tooLarge);
    edge.manifest.layers[0].transform = {.origin = {0, 0}, .size = {1, 100.003}};
    QCOMPARE(resizeError(edge, {.width = 100, .height = 30'000, .resolution = 72}), tooLarge);
    edge.manifest.layers[0].transform = {.origin = {0, 0}, .size = {1, 100}};
    QCOMPARE(resizeError(edge, {.width = 100, .height = 30'000, .resolution = 72}), std::nullopt);
}

void ImageSizeTests::missingPixelsAreRefused()
{
    ProjectSnapshot input = imported();
    input.images.clear();
    QCOMPARE(resizeError(input, {.width = 128, .height = 64, .resolution = 72}), std::optional(ProjectError::Kind::missingImage));
    input = imported();
    input.manifest.layers[0].maskFile = "mask.png";
    QCOMPARE(resizeError(input, {.width = 128, .height = 64, .resolution = 72}), std::optional(ProjectError::Kind::missingImage));
    // A layer scaled out of reach fails its own rule.
    input = imported();
    input.manifest.layers[0].transform.origin = {900'000, 0};
    QCOMPARE(resizeError(input, {.width = 128, .height = 32, .resolution = 72}), std::optional(ProjectError::Kind::tooLarge));
}

void ImageSizeTests::masksScaleWithTheirLayers()
{
    ProjectSnapshot input = imported();
    const QUuid id = input.manifest.layers[0].id;
    input.manifest.layers[0].maskFile = uuidString(id) + ".mask.png";
    // White on the left half, 100 on the right.
    QImage mask = gray(64, 32, 100);
    for (int y = 0; y < 32; ++y)
        std::fill_n(mask.scanLine(y), 32, uchar(255));
    input.masks.insert({id, LayerMask::assetFrom(mask)});
    const ProjectSnapshot result = ImageResizer::resize(input, {.width = 128, .height = 96, .resolution = 72, .sampling = LayerSampling::nearest});
    const QImage scaled = result.masks.at(id).image();
    QCOMPARE(scaled.format(), QImage::Format_Grayscale8);
    QCOMPARE(scaled.size(), QSize(128, 96));
    QCOMPARE(int(scaled.constScanLine(0)[0]), 255);
    QCOMPARE(int(scaled.constScanLine(95)[63]), 255);
    QCOMPARE(int(scaled.constScanLine(0)[64]), 100);
    QCOMPARE(int(scaled.constScanLine(95)[127]), 100);
    QCOMPARE(result.manifest.layers[0].maskFile, input.manifest.layers[0].maskFile);
    // Turned, the box's corners hold no mask: coverage zero.
    input.manifest.layers[0].transform.rotation = 45;
    const QImage turned = ImageResizer::resize(input, {.width = 128, .height = 64, .resolution = 72}).masks.at(id).image();
    QCOMPARE(int(turned.constScanLine(0)[0]), 0);
    QCOMPARE(int(turned.constScanLine(turned.height() / 2)[turned.width() / 2 - 8]), 255);
}

void ImageSizeTests::solidAndPlacedMasksKeepTheirPixels()
{
    ProjectSnapshot input = imported();
    const QUuid id = input.manifest.layers[0].id;
    input.manifest.layers[0].maskFile = uuidString(id) + ".mask.png";
    input.masks.insert({id, LayerMask::solid(false).asset});
    const ProjectSnapshot solid = ImageResizer::resize(input, {.width = 128, .height = 96, .resolution = 72});
    QCOMPARE(solid.masks.at(id).identity(), input.masks.at(id).identity());
    // A mask placed apart keeps its pixels; its placement scales.
    input.masks.clear();
    input.masks.insert({id, LayerMask::assetFrom(gray(30, 15, 200))});
    input.manifest.layers[0].maskPlacement = LayerTransform{.origin = {10, 4}, .size = {30, 15}, .flipX = true};
    const ProjectSnapshot placed = ImageResizer::resize(input, {.width = 128, .height = 96, .resolution = 72});
    QCOMPARE(placed.masks.at(id).identity(), input.masks.at(id).identity());
    const LayerTransform placement = placed.manifest.layers[0].maskPlacement.value();
    QCOMPARE(placement.origin, QPointF(20, 12));
    QCOMPARE(placement.size, QSizeF(60, 45));
    QVERIFY(placement.flipX);
}

void ImageSizeTests::recordsKeepEveryField_data()
{
    QTest::addColumn<bool>("enabled");
    QTest::addColumn<bool>("linked");
    QTest::newRow("a disabled, linked mask") << false << true;
    QTest::newRow("an enabled, unlinked mask") << true << false;
}

void ImageSizeTests::recordsKeepEveryField()
{
    QFETCH(bool, enabled);
    QFETCH(bool, linked);
    ProjectSnapshot input = imported();
    const QUuid folder = QUuid::createUuid(), source = QUuid::createUuid();
    ProjectLayerRecord &layer = input.manifest.layers[0];
    layer.parentID = folder;
    layer.opacity = 0.25;
    layer.blendMode = LayerBlendMode::overlay;
    layer.maskFile = "mask.png";
    layer.maskEnabled = enabled;
    layer.maskSourceID = source;
    layer.maskLinked = linked;
    input.masks.insert({layer.id, LayerMask::solid(true).asset});
    const std::optional<QString> imageFile = layer.imageFile;
    // A folder has no pixels; its box still scales.
    input.manifest.layers.push_back({.id = folder, .name = "Folder", .isVisible = true, .transform = {.origin = {8, 4}, .size = {16, 8}},
                                     .imageFile = std::nullopt, .isGroup = true});
    const ProjectSnapshot output = ImageResizer::resize(input, {.width = 128, .height = 96, .resolution = 72});
    const ProjectLayerRecord &kept = output.manifest.layers[0];
    QCOMPARE(kept.name, QString("fixture"));
    QCOMPARE(kept.imageFile, imageFile);
    QCOMPARE(kept.parentID, std::optional(folder));
    QCOMPARE(kept.isGroup, std::nullopt);
    QCOMPARE(kept.opacity, std::optional(0.25));
    QCOMPARE(kept.blendMode, std::optional(LayerBlendMode::overlay));
    QCOMPARE(kept.maskFile, std::optional(QString("mask.png")));
    QCOMPARE(kept.maskEnabled, std::optional(enabled));
    QCOMPARE(kept.maskSourceID, std::optional(source));
    QCOMPARE(kept.maskLinked, std::optional(linked));
    QCOMPARE(kept.maskPlacement, std::nullopt);
    const ProjectLayerRecord &group = output.manifest.layers[1];
    QCOMPARE(group.isGroup, std::optional(true));
    QCOMPARE(group.transform, (LayerTransform{.origin = {16, 12}, .size = {32, 24}}));
    QCOMPARE(int(output.images.size()), 1);
}

void ImageSizeTests::thumbnailsTruncate_data()
{
    QTest::addColumn<QSize>("size");
    QTest::addColumn<QSize>("thumbnail");
    QTest::newRow("small layers keep their size") << QSize(96, 40) << QSize(96, 40);
    QTest::newRow("smaller still, never enlarged") << QSize(48, 20) << QSize(48, 20);
    QTest::newRow("28.8 loses its fraction") << QSize(100, 30) << QSize(96, 28);
    QTest::newRow("tall") << QSize(30, 100) << QSize(28, 96);
    QTest::newRow("a strip stays one pixel high") << QSize(2400, 1) << QSize(96, 1);
}

void ImageSizeTests::thumbnailsTruncate()
{
    QFETCH(QSize, size);
    QFETCH(QSize, thumbnail);
    const ProjectSnapshot input = imported();
    const ProjectSnapshot output = ImageResizer::resize(input, {.width = size.width(), .height = size.height(), .resolution = 72});
    QCOMPARE(output.images.begin()->second.thumbnail.size(), thumbnail);
    // Inside the red half; halved edges fade, as on macOS.
    QCOMPARE(output.images.begin()->second.thumbnail.pixelColor(thumbnail.width() / 4, thumbnail.height() / 2), QColor(255, 0, 0));
}

void ImageSizeTests::aSurfaceThatCannotBeAllocatedThrows()
{
    const ProjectSnapshot input = imported();
    std::optional<ExportError::Kind> kind;
    {
        // 9000 x 9000 asks for 324 MB.
        const AddressSpaceLimit limit(200ll * 1024 * 1024);
        try {
            ImageResizer::resize(input, {.width = 9000, .height = 9000, .resolution = 72});
        } catch (const ExportError &error) {
            kind = error.kind;
        }
    }
    QCOMPARE(kind, std::optional(ExportError::Kind::render));
    // A mask's surface fails the same way.
    ProjectSnapshot masked = imported();
    masked.manifest.layers[0].imageFile = std::nullopt;
    masked.manifest.layers[0].maskFile = "mask.png";
    masked.masks.insert({masked.manifest.layers[0].id, LayerMask::assetFrom(gray(64, 32, 200))});
    masked.images.clear();
    kind.reset();
    {
        // 9000 x 9000 gray asks for 81 MB.
        const AddressSpaceLimit limit(40ll * 1024 * 1024);
        try {
            ImageResizer::resize(masked, {.width = 9000, .height = 9000, .resolution = 72});
        } catch (const ExportError &error) {
            kind = error.kind;
        }
    }
    QCOMPARE(kind, std::optional(ExportError::Kind::render));
}

QTEST_MAIN(ImageSizeTests)
#include "ImageSizeTests.moc"
