#include "BudgetFixtures.h"
#include "AddressSpaceLimit.h"
#include "Document/CanvasSize.h"
#include "Document/EditorSession.h"
#include "Document/LayerMask.h"
#include "IO/CanvasResizer.h"
#include "IO/ImageExporter.h"
#include "ProjectFixtures.h"
#include <QTemporaryDir>
#include <QtTest>

namespace {
// A 64x32 image layer, turned and flipped, filling its document.
ProjectSnapshot turned()
{
    const QUuid id = QUuid::createUuid();
    const LayerTransform transform{.origin = {3.5, -2.25}, .size = {64, 32}, .rotation = 37, .flipX = true};
    const ProjectLayerRecord record{.id = id, .name = "Turned", .isVisible = true, .transform = transform, .imageFile = uuidString(id) + ".png"};
    return {.manifest = {.documentID = QUuid::createUuid(), .width = 64, .height = 32, .activeLayerID = id, .layers = {record}},
            .images = {{id, asset(halfRed(), "Turned")}}};
}

// A 4x4 document holding one blank layer.
ProjectSnapshot blank()
{
    const QUuid id = QUuid::createUuid();
    const ProjectLayerRecord record{.id = id, .name = "Layer 1", .isVisible = true, .transform = {.origin = {0, 0}, .size = {4, 4}},
                                    .imageFile = std::nullopt};
    return {.manifest = {.documentID = QUuid::createUuid(), .width = 4, .height = 4, .activeLayerID = id, .layers = {record}}, .images = {}};
}

std::optional<ProjectError::Kind> resizeError(const ProjectSnapshot &snapshot, const CanvasSizeOptions &options)
{
    return projectError([&] { CanvasResizer::resize(snapshot, options); });
}
}

class CanvasSizeTests : public QObject {
    Q_OBJECT
private slots:
    void everyAnchorPreservesSourceAndTransformForExpansionAndShrink();
    void relativeRatioAndUnitsUseFinalDimensions();
    void unitsHaveSwiftsNames();
    void coloredExtensionPreservesOldTransparencyAndRoundTripsWithUndo();
    void transparentResizeIsAllocationFreeAndShrinkDoesNotAddFill();
    void limitsAreJudgedAtTheirEdges();
    void nothingToDoReturnsTheSnapshot();
    void masksPlacedApartMoveWithTheirLayers();
    void theExtensionCountsAgainstTheBudgets();
    void recordsKeepEveryField_data();
    void recordsKeepEveryField();
    void anExtensionThatCannotBeAllocatedThrows();
};

void CanvasSizeTests::everyAnchorPreservesSourceAndTransformForExpansionAndShrink()
{
    const ProjectSnapshot source = turned();
    const ProjectLayerRecord &layer = source.manifest.layers[0];
    for (const int delta : {5, -5}) {
        for (int anchor = 0; anchor <= 8; ++anchor) {
            const ProjectSnapshot result = CanvasResizer::resize(source, {.width = 64 + delta, .height = 32 + delta, .anchor = anchor});
            const ProjectLayerRecord &output = result.manifest.layers[0];
            // Floor: the odd pixel goes right and bottom.
            const double expected[] = {0, delta == 5 ? 2.0 : -3.0, double(delta)};
            QCOMPARE(output.transform.origin, layer.transform.origin + QPointF(expected[anchor % 3], expected[anchor / 3]));
            QCOMPARE(output.transform.size, layer.transform.size);
            QCOMPARE(output.transform.rotation, 37.0);
            QVERIFY(output.transform.flipX);
            QCOMPARE(output.id, layer.id);
            QCOMPARE(result.images.at(layer.id).identity(), source.images.at(layer.id).identity());
            QCOMPARE(result.manifest.width, qint64(64 + delta));
            QCOMPARE(result.manifest.height, qint64(32 + delta));
        }
    }
}

void CanvasSizeTests::relativeRatioAndUnitsUseFinalDimensions()
{
    CanvasSizeDraft draft(1000, 500, 100);
    draft.relative = true;
    draft.locked = true;
    draft.set(200, true);
    QCOMPARE(draft.width, 1200.0);
    QCOMPARE(draft.height, 600.0);
    QCOMPARE(draft.displayed(false), 100.0);
    draft.set(-250, false);
    QCOMPARE(draft.width, 500.0);
    QCOMPARE(draft.height, 250.0);
    draft.relative = false;
    draft.unit = CanvasUnit::inches;
    draft.set(10, true);
    QCOMPARE(draft.width, 1000.0);
    QCOMPARE(draft.height, 500.0);
    QCOMPARE(draft.displayed(true), 10.0);
    draft.unit = CanvasUnit::percent;
    draft.set(50, true);
    QCOMPARE(draft.width, 500.0);
    QCOMPARE(draft.height, 250.0);
    QCOMPARE(draft.displayed(false), 50.0);
    draft.unit = CanvasUnit::centimeters;
    QVERIFY(qAbs(draft.displayed(true) - 12.7) < 0.001);
    draft.set(25.4, false);
    QCOMPARE(draft.height, 1000.0);
    QCOMPARE(draft.width, 2000.0);
    draft.unit = CanvasUnit::pixels;
    QVERIFY(draft.valid());
    draft.set(0, true);
    QVERIFY(!draft.valid());
    // Validity rounds first: 0.5 is one pixel, 30,000.4 still fits.
    draft.locked = false;
    for (const auto &[value, valid] : {std::pair(0.5, true), std::pair(0.49, false), std::pair(30'000.4, true), std::pair(30'000.5, false),
                                       std::pair(std::nan(""), false), std::pair(double(INFINITY), false)}) {
        draft.set(value, true);
        draft.set(10, false);
        QCOMPARE(draft.valid(), valid);
        draft.set(10, true);
        draft.set(value, false);
        QCOMPARE(draft.valid(), valid);
    }
}

void CanvasSizeTests::unitsHaveSwiftsNames()
{
    QCOMPARE(rawValue(CanvasUnit::pixels), QString("Pixels"));
    QCOMPARE(rawValue(CanvasUnit::percent), QString("Percent"));
    QCOMPARE(rawValue(CanvasUnit::inches), QString("Inches"));
    QCOMPARE(rawValue(CanvasUnit::centimeters), QString("Centimeters"));
}

void CanvasSizeTests::coloredExtensionPreservesOldTransparencyAndRoundTripsWithUndo()
{
    const ProjectSnapshot input = blank();
    QTest::ignoreMessage(QtInfoMsg, "canvas 4 x 4 becomes 8 x 2");
    // Shrinks one way, grows the other: only side bands colour.
    const CanvasExtensionColor red{1, 0, 0};
    const ProjectSnapshot output = CanvasResizer::resize(input, {.width = 8, .height = 2, .fill = red});
    QCOMPARE(int(output.manifest.layers.size()), 2);
    QCOMPARE(output.manifest.activeLayerID, input.manifest.activeLayerID);
    const ProjectLayerRecord &extension = output.manifest.layers[0];
    QCOMPARE(extension.name, QString("Canvas Extension"));
    QVERIFY(extension.isVisible);
    QCOMPARE(extension.transform, (LayerTransform{.origin = {0, 0}, .size = {8, 2}}));
    QCOMPARE(extension.imageFile, std::optional(uuidString(extension.id) + ".png"));
    QCOMPARE(output.images.at(extension.id).name, QString("Canvas Extension"));
    QCOMPARE(output.images.at(extension.id).image().format(), QImage::Format_RGBA8888_Premultiplied);
    QCOMPARE(output.images.at(extension.id).thumbnail.size(), QSize(8, 2));
    // The old canvas, x 2 to 5, stays clear.
    const QImage flat = ImageExporter::render(output).image;
    QCOMPARE(flat.pixelColor(0, 0), QColor(255, 0, 0));
    QCOMPARE(flat.pixelColor(1, 1), QColor(255, 0, 0));
    QCOMPARE(flat.pixelColor(2, 0).alpha(), 0);
    QCOMPARE(flat.pixelColor(3, 0).alpha(), 0);
    QCOMPARE(flat.pixelColor(5, 1).alpha(), 0);
    QCOMPARE(flat.pixelColor(6, 0), QColor(255, 0, 0));
    QCOMPARE(flat.pixelColor(7, 1), QColor(255, 0, 0));
    QTemporaryDir folder;
    ProjectStore::save(output, folder.filePath("Canvas.comp"));
    const QImage reopened = ImageExporter::render(ProjectStore::load(folder.filePath("Canvas.comp"))).image;
    QCOMPARE(reopened.pixelColor(3, 0).alpha(), 0);
    QCOMPARE(reopened.pixelColor(0, 0), QColor(255, 0, 0));
    // In the session: one step, named by its action.
    EditorSession session;
    session.installProject(input, "blank.comp");
    const std::optional<CanvasDocument> before = session.document();
    session.applyDocumentSize(output, "Canvas Size");
    QCOMPARE(session.history.undoName(), QString("Canvas Size"));
    session.undo();
    QCOMPARE(session.document(), before);
    session.redo();
    QCOMPARE(session.document().value().size(), QSizeF(8, 2));
    QCOMPARE(int(session.document().value().layers.size()), 2);
    // Growth on either side lays a fill; shrinking lays none.
    QCOMPARE(int(CanvasResizer::resize(input, {.width = 2, .height = 8, .fill = red}).manifest.layers.size()), 2);
    QCOMPARE(int(CanvasResizer::resize(input, {.width = 8, .height = 4, .fill = red}).manifest.layers.size()), 2);
    QCOMPARE(int(CanvasResizer::resize(input, {.width = 4, .height = 2, .fill = red}).manifest.layers.size()), 1);
    QCOMPARE(int(CanvasResizer::resize(input, {.width = 2, .height = 4, .fill = red}).manifest.layers.size()), 1);
    // An old 6x4 canvas clears 6 across, 4 down.
    ProjectSnapshot oblong = input;
    oblong.manifest.width = 6;
    const ProjectSnapshot grown = CanvasResizer::resize(oblong, {.width = 10, .height = 8, .fill = red});
    const QImage around = grown.images.at(grown.manifest.layers[0].id).image();
    QCOMPARE(around.pixelColor(7, 3).alpha(), 0);
    QCOMPARE(around.pixelColor(2, 5).alpha(), 0);
    QCOMPARE(around.pixelColor(7, 6), QColor(255, 0, 0));
    QCOMPARE(around.pixelColor(8, 3), QColor(255, 0, 0));
    QCOMPARE(around.pixelColor(1, 3), QColor(255, 0, 0));
    QCOMPARE(around.pixelColor(4, 1), QColor(255, 0, 0));
    // A colour between the ends lands on Qt's nearest level.
    const ProjectSnapshot gray = CanvasResizer::resize(input, {.width = 8, .height = 4, .fill = CanvasExtensionColor{0.5, 0.25, 1}});
    QCOMPARE(gray.images.at(gray.manifest.layers[0].id).image().pixelColor(0, 0), QColor(128, 64, 255));
}

void CanvasSizeTests::transparentResizeIsAllocationFreeAndShrinkDoesNotAddFill()
{
    ProjectSnapshot input = blank();
    input.manifest.layers.clear();
    const ProjectSnapshot large = CanvasResizer::resize(input, {.width = 30'000, .height = 30'000});
    QVERIFY(large.images.empty());
    QCOMPARE(large.manifest.width, qint64(30'000));
    const CanvasExtensionColor white{1, 1, 1};
    const ProjectSnapshot small = CanvasResizer::resize(input, {.width = 2, .height = 2, .fill = white});
    QVERIFY(small.images.empty());
    QVERIFY(small.manifest.layers.empty());
    // 900 megapixels of fill pass the budget.
    QCOMPARE(resizeError(input, {.width = 30'000, .height = 30'000, .fill = white}), std::optional(ProjectError::Kind::tooLarge));
}

void CanvasSizeTests::limitsAreJudgedAtTheirEdges()
{
    const ProjectSnapshot input = turned();
    const auto tooLarge = std::optional(ProjectError::Kind::tooLarge), invalid = std::optional(ProjectError::Kind::invalid);
    QCOMPARE(resizeError(input, {.width = 0, .height = 10}), tooLarge);
    QCOMPARE(resizeError(input, {.width = 10, .height = 0}), tooLarge);
    QCOMPARE(resizeError(input, {.width = 30'001, .height = 10}), tooLarge);
    QCOMPARE(resizeError(input, {.width = 10, .height = 30'001}), tooLarge);
    QCOMPARE(resizeError(input, {.width = 30'000, .height = 1}), std::nullopt);
    QCOMPARE(resizeError(input, {.width = 1, .height = 30'000}), std::nullopt);
    QCOMPARE(resizeError(input, {.width = 10, .height = 10, .anchor = -1}), tooLarge);
    QCOMPARE(resizeError(input, {.width = 10, .height = 10, .anchor = 9}), tooLarge);
    QCOMPARE(resizeError(input, {.width = 10, .height = 10, .anchor = 0}), std::nullopt);
    QCOMPARE(resizeError(input, {.width = 10, .height = 10, .anchor = 8}), std::nullopt);
    // A crop's own offset: finite, a million at most.
    for (const double wild : {std::nan(""), double(INFINITY), -double(INFINITY), 1'000'000.5, -1'000'000.5}) {
        QCOMPARE(resizeError(input, {.width = 10, .height = 10, .contentOffset = QPointF(wild, 0)}), invalid);
        QCOMPARE(resizeError(input, {.width = 10, .height = 10, .contentOffset = QPointF(0, wild)}), invalid);
    }
    // A layer pushed past a million fails its own rule.
    QCOMPARE(resizeError(input, {.width = 10, .height = 10, .contentOffset = QPointF(1'000'000, 0)}), tooLarge);
    QCOMPARE(resizeError(input, {.width = 10, .height = 10, .contentOffset = QPointF(999'996, 0)}), std::nullopt);
    // Colours: finite, zero to one; judged only with a fill.
    for (const double wild : {std::nan(""), -0.01, 1.01, double(INFINITY)}) {
        QCOMPARE(resizeError(input, {.width = 70, .height = 40, .fill = CanvasExtensionColor{wild, 0, 0}}), invalid);
        QCOMPARE(resizeError(input, {.width = 70, .height = 40, .fill = CanvasExtensionColor{0, wild, 0}}), invalid);
        QCOMPARE(resizeError(input, {.width = 70, .height = 40, .fill = CanvasExtensionColor{0, 0, wild}}), invalid);
        QCOMPARE(resizeError(input, {.width = 60, .height = 30, .fill = CanvasExtensionColor{wild, 0, 0}}), std::nullopt);
    }
}

void CanvasSizeTests::nothingToDoReturnsTheSnapshot()
{
    const ProjectSnapshot input = turned();
    const ProjectSnapshot same = CanvasResizer::resize(input, {.width = 64, .height = 32, .fill = CanvasExtensionColor{1, 0, 0}});
    QCOMPARE(same.manifest.encoded(), input.manifest.encoded());
    QCOMPARE(same.images.begin()->second.identity(), input.images.begin()->second.identity());
    // One side alone is a change, whatever the anchor moves.
    QCOMPARE(CanvasResizer::resize(input, {.width = 70, .height = 32, .anchor = 0}).manifest.width, qint64(70));
    QCOMPARE(CanvasResizer::resize(input, {.width = 64, .height = 40, .anchor = 0}).manifest.height, qint64(40));
    // The same size with an offset is a move.
    const ProjectSnapshot moved = CanvasResizer::resize(input, {.width = 64, .height = 32, .contentOffset = QPointF(-7, 9)});
    QCOMPARE(moved.manifest.layers[0].transform.origin, input.manifest.layers[0].transform.origin + QPointF(-7, 9));
}

void CanvasSizeTests::masksPlacedApartMoveWithTheirLayers()
{
    ProjectSnapshot input = turned();
    ProjectLayerRecord &layer = input.manifest.layers[0];
    layer.maskFile = uuidString(layer.id) + ".mask.png";
    layer.maskPlacement = LayerTransform{.origin = {10, 20}, .size = {30, 15}, .rotation = 12, .flipY = true};
    input.masks.insert({layer.id, LayerMask::assetFrom(gray(30, 15, 200))});
    const ProjectSnapshot output = CanvasResizer::resize(input, {.width = 74, .height = 42, .anchor = 8});
    const LayerTransform placement = output.manifest.layers[0].maskPlacement.value();
    QCOMPARE(placement, (LayerTransform{.origin = {20, 30}, .size = {30, 15}, .rotation = 12, .flipY = true}));
    QCOMPARE(output.masks.at(layer.id).identity(), input.masks.at(layer.id).identity());
}

void CanvasSizeTests::theExtensionCountsAgainstTheBudgets()
{
    // Painted layers leave 19 megapixels of the document budget.
    ProjectSnapshot input{.manifest = {.documentID = QUuid::createUuid(), .width = 100, .height = 100, .activeLayerID = std::nullopt, .layers = {}},
                          .images = {}};
    for (const ImportedImage &layer : claiming(DocumentLimits::documentPixelBudget() - 19'000'000)) {
        const QUuid layerID = QUuid::createUuid();
        input.manifest.layers.push_back({.id = layerID, .name = "Painted", .isVisible = true, .transform = {.origin = {0, 0}, .size = QSizeF(layer.size())},
                                         .imageFile = uuidString(layerID) + ".png"});
        input.images.insert({layerID, layer});
    }
    const QUuid id = input.manifest.layers.front().id;
    input.manifest.activeLayerID = id;
    const std::shared_ptr<const RasterSnapshot> raster = input.images.at(id).raster;
    const CanvasExtensionColor white{1, 1, 1};
    QVERIFY(!raster->hasMaterializedPixels());
    QCOMPARE(resizeError(input, {.width = 4750, .height = 4001, .fill = white}), std::optional(ProjectError::Kind::tooLarge));
    const ProjectSnapshot output = CanvasResizer::resize(input, {.width = 4750, .height = 4000, .fill = white});
    QCOMPARE(output.images.at(output.manifest.layers[0].id).size(), QSize(4750, 4000));
    // 96 / 4750 of 4000 is 80.8: cut to 80.
    QCOMPARE(output.images.at(output.manifest.layers[0].id).thumbnail.size(), QSize(96, 80));
    const ProjectSnapshot strip = CanvasResizer::resize(blank(), {.width = 3000, .height = 5, .fill = white});
    QCOMPARE(strip.images.begin()->second.thumbnail.size(), QSize(96, 1));
    QCOMPARE(strip.images.begin()->second.thumbnail.pixelColor(0, 0), QColor(255, 255, 255));
    // Counting pixels flattens nothing.
    QVERIFY(output.images.at(id).raster == raster);
    QVERIFY(!raster->hasMaterializedPixels());
    // Ten thousand layers leave no room for one more.
    ProjectSnapshot crowded = blank();
    crowded.manifest.layers.resize(10'000, crowded.manifest.layers[0]);
    QCOMPARE(resizeError(crowded, {.width = 8, .height = 8, .fill = white}), std::optional(ProjectError::Kind::tooLarge));
    crowded.manifest.layers.resize(9'999);
    QCOMPARE(int(CanvasResizer::resize(crowded, {.width = 8, .height = 8, .fill = white}).manifest.layers.size()), 10'000);
}

void CanvasSizeTests::recordsKeepEveryField_data()
{
    QTest::addColumn<bool>("enabled");
    QTest::addColumn<bool>("linked");
    QTest::newRow("a disabled, linked mask") << false << true;
    QTest::newRow("an enabled, unlinked mask") << true << false;
}

void CanvasSizeTests::recordsKeepEveryField()
{
    QFETCH(bool, enabled);
    QFETCH(bool, linked);
    ProjectSnapshot input = turned();
    const QUuid folder = QUuid::createUuid(), source = QUuid::createUuid();
    ProjectLayerRecord &layer = input.manifest.layers[0];
    layer.isVisible = false;
    layer.parentID = folder;
    layer.opacity = 0.25;
    layer.blendMode = LayerBlendMode::overlay;
    layer.maskFile = "mask.png";
    layer.maskEnabled = enabled;
    layer.maskSourceID = source;
    layer.maskLinked = linked;
    const std::optional<QString> imageFile = layer.imageFile;
    input.manifest.layers.push_back({.id = folder, .name = "Folder", .isVisible = true, .transform = {.origin = {0, 0}, .size = {64, 32}},
                                     .imageFile = std::nullopt, .isGroup = true});
    input.manifest.resolution = 144;
    const ProjectSnapshot output = CanvasResizer::resize(input, {.width = 70, .height = 40, .anchor = 0});
    QCOMPARE(output.manifest.resolution, std::optional(144.0));
    QCOMPARE(output.manifest.documentID, input.manifest.documentID);
    const ProjectLayerRecord &kept = output.manifest.layers[0];
    QCOMPARE(kept.name, QString("Turned"));
    QCOMPARE(kept.isVisible, false);
    QCOMPARE(kept.imageFile, imageFile);
    QCOMPARE(kept.parentID, std::optional(folder));
    QCOMPARE(kept.isGroup, std::nullopt);
    QCOMPARE(kept.opacity, std::optional(0.25));
    QCOMPARE(kept.blendMode, std::optional(LayerBlendMode::overlay));
    QCOMPARE(kept.maskFile, std::optional(QString("mask.png")));
    QCOMPARE(kept.maskEnabled, std::optional(enabled));
    QCOMPARE(kept.maskSourceID, std::optional(source));
    QCOMPARE(kept.maskLinked, std::optional(linked));
    QCOMPARE(output.manifest.layers[1].isGroup, std::optional(true));
    QCOMPARE(output.manifest.layers[1].name, QString("Folder"));
}

void CanvasSizeTests::anExtensionThatCannotBeAllocatedThrows()
{
    std::optional<ExportError::Kind> kind;
    {
        // 9000 x 9000 asks for 324 MB.
        const AddressSpaceLimit limit(200ll * 1024 * 1024);
        try {
            CanvasResizer::resize(blank(), {.width = 9000, .height = 9000, .fill = CanvasExtensionColor{1, 1, 1}});
        } catch (const ExportError &error) {
            kind = error.kind;
        }
    }
    QCOMPARE(kind, std::optional(ExportError::Kind::render));
}

QTEST_MAIN(CanvasSizeTests)
#include "CanvasSizeTests.moc"
