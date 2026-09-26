#include "IO/ImageExporter.h"
#include "AddressSpaceLimit.h"
#include "Document/LayerMask.h"
#include "RenderFixtures.h"
#include <QColorSpace>
#include <QImageReader>
#include <QTemporaryDir>
#include <QtTest>

namespace {
ImportedImage asset(const QImage &image, const QString &name)
{
    return ImportedImage(image, image, name);
}

// A 2x2 image, left column red, 4x4 at (1,1).
ProjectSnapshot snapshot(double rotation = 0, bool flip = false)
{
    QImage image = BrushRaster::context(2, 2, false);
    image.setPixel(0, 0, qRgba(255, 0, 0, 255));
    image.setPixel(0, 1, qRgba(255, 0, 0, 255));
    const QUuid id = QUuid::createUuid();
    const LayerTransform transform{.origin = {1, 1}, .size = {4, 4}, .rotation = rotation, .flipX = flip, .sampling = LayerSampling::nearest};
    const ProjectLayerRecord record{.id = id, .name = "Red", .isVisible = true, .transform = transform, .imageFile = id.toString(QUuid::WithoutBraces) + ".png"};
    return {.manifest = {.documentID = QUuid::createUuid(), .width = 6, .height = 6, .activeLayerID = id, .layers = {record}},
            .images = {{id, asset(image, "Red")}}};
}

QImage decoded(const QByteArray &data)
{
    return QImage::fromData(data).convertToFormat(QImage::Format_RGBA8888);
}
}

class ExportTests : public QObject {
    Q_OBJECT
private slots:
    void pngPreservesDimensionsAlphaOrientationAndTransforms_data();
    void pngPreservesDimensionsAlphaOrientationAndTransforms();
    void orderVisibilityClippingAndAtomicOverwrite();
    void blankCanvasAndOversizedCanvas();
    void resolutionTravelsWithThePixels();
    void missingPixelsAndCyclesAreRefused();
    void masksFoldersAndClippingReachTheExport();
    void aFileThatCannotBeWrittenThrowsAndKeepsTheOldOne();
    void blendModesAndPlacedMasksReachTheExport();
    void aSnapshotsMaskFollowsItsRecord();
    void aCanvasThatCannotBeAllocatedThrows();
    void exportsAreLogged();
};

void ExportTests::pngPreservesDimensionsAlphaOrientationAndTransforms_data()
{
    QTest::addColumn<double>("rotation");
    QTest::addColumn<bool>("flip");
    QTest::addColumn<QPoint>("red");
    QTest::addColumn<QPoint>("clear");
    QTest::newRow("upright") << 0.0 << false << QPoint(1, 1) << QPoint(4, 1);
    QTest::newRow("flipped") << 0.0 << true << QPoint(4, 1) << QPoint(1, 1);
    QTest::newRow("a right angle") << 90.0 << false << QPoint(1, 1) << QPoint(1, 4);
}

void ExportTests::pngPreservesDimensionsAlphaOrientationAndTransforms()
{
    QFETCH(double, rotation);
    QFETCH(bool, flip);
    QFETCH(QPoint, red);
    QFETCH(QPoint, clear);
    const QByteArray data = ImageExporter::pngData(snapshot(rotation, flip));
    QVERIFY(data.startsWith("\x89PNG"));
    const QImage image = decoded(data);
    QCOMPARE(image.size(), QSize(6, 6));
    QCOMPARE(QImage::fromData(data).colorSpace(), QColorSpace(QColorSpace::SRgb));
    QCOMPARE(image.pixel(red), qRgba(255, 0, 0, 255));
    QCOMPARE(qAlpha(image.pixel(clear)), 0);
    QCOMPARE(qAlpha(image.pixel(0, 0)), 0);
}

void ExportTests::orderVisibilityClippingAndAtomicOverwrite()
{
    const ProjectSnapshot original = snapshot();
    const QUuid id = QUuid::createUuid();
    QTemporaryDir directory;
    const QString path = directory.filePath("Export.png");
    for (const bool visible : {true, false}) {
        ProjectSnapshot stacked = original;
        stacked.images.insert({id, asset(solid(1, 1, qRgba(0, 0, 255, 255)), "Blue")});
        // The blue layer reaches past the canvas on every side.
        stacked.manifest.layers.push_back({.id = id, .name = "Blue", .isVisible = visible,
                                           .transform = {.origin = {-2, -2}, .size = {10, 10}}, .imageFile = "blue.png"});
        ImageExporter::exportPNG(stacked, path);
        const QImage written = QImage(path).convertToFormat(QImage::Format_RGBA8888);
        QCOMPARE(written.size(), QSize(6, 6));
        QCOMPARE(written.pixel(1, 1), visible ? qRgba(0, 0, 255, 255) : qRgba(255, 0, 0, 255));
        QCOMPARE(written.pixel(5, 5), visible ? qRgba(0, 0, 255, 255) : qRgba(0, 0, 0, 0));
    }
    QCOMPARE(QDir(directory.path()).entryList(QDir::Files), QStringList{"Export.png"});
}

void ExportTests::blankCanvasAndOversizedCanvas()
{
    const ProjectSnapshot blank{.manifest = {.documentID = QUuid::createUuid(), .width = 2, .height = 2, .activeLayerID = std::nullopt, .layers = {}}, .images = {}};
    QCOMPARE(decoded(ImageExporter::pngData(blank)).pixel(1, 1), qRgba(0, 0, 0, 0));
    const auto sized = [](int width, int height) {
        return ProjectSnapshot{.manifest = {.documentID = QUuid::createUuid(), .width = width, .height = height, .activeLayerID = std::nullopt, .layers = {}}, .images = {}};
    };
    for (const QSize &size : {QSize(30'000, 30'000), QSize(30'001, 1), QSize(1, 30'001), QSize(0, 5), QSize(5, 0), QSize(10'001, 10'000)}) {
        try {
            ImageExporter::render(sized(size.width(), size.height()));
            QFAIL("an oversized or empty canvas must be refused");
        } catch (const ExportError &error) {
            QCOMPARE(error.kind, ExportError::Kind::tooLarge);
        }
    }
    QCOMPARE(ImageExporter::render(sized(30'000, 1)).image.size(), QSize(30'000, 1));
    QCOMPARE(ImageExporter::render(sized(1, 30'000)).image.size(), QSize(1, 30'000));
}

void ExportTests::resolutionTravelsWithThePixels()
{
    ProjectSnapshot print = snapshot();
    QCOMPARE(ImageExporter::render(print).resolution, 72.0);
    QCOMPARE(QImage::fromData(ImageExporter::pngData(print)).dotsPerMeterX(), 2835);
    print.manifest.resolution = 300;
    QCOMPARE(ImageExporter::render(print).resolution, 300.0);
    const QImage png = QImage::fromData(ImageExporter::pngData(print));
    QCOMPARE(png.dotsPerMeterX(), 11811);
    QCOMPARE(png.dotsPerMeterY(), 11811);
    const QImage jpeg = QImage::fromData(ImageExporter::jpeg(ImageExporter::render(print), {}).data);
    QCOMPARE(qRound(jpeg.dotsPerMeterX() * 0.0254), 300);
    QCOMPARE(qRound(jpeg.dotsPerMeterY() * 0.0254), 300);
}

void ExportTests::missingPixelsAndCyclesAreRefused()
{
    const auto kind = [](const ProjectSnapshot &broken) -> std::optional<ProjectError::Kind> {
        try {
            ImageExporter::render(broken);
        } catch (const ProjectError &error) {
            return error.kind;
        }
        return std::nullopt;
    };
    ProjectSnapshot noPixels = snapshot();
    noPixels.images.clear();
    QCOMPARE(kind(noPixels), std::optional(ProjectError::Kind::missingImage));
    ProjectSnapshot noMask = snapshot();
    noMask.manifest.layers[0].maskFile = "mask.png";
    QCOMPARE(kind(noMask), std::optional(ProjectError::Kind::missingImage));
    noMask.masks.insert({noMask.manifest.layers[0].id, asset(gray(2, 2, 255), "Mask")});
    QCOMPARE(kind(noMask), std::nullopt);
    // A folder has no image file and needs no pixels.
    ProjectSnapshot folder = snapshot();
    folder.manifest.layers.push_back({.id = QUuid::createUuid(), .name = "Folder", .isVisible = true, .transform = {}, .imageFile = std::nullopt,
                                      .isGroup = true});
    QCOMPARE(kind(folder), std::nullopt);
    ProjectSnapshot cyclic = snapshot();
    cyclic.manifest.layers[0].maskSourceID = cyclic.manifest.layers[0].id;
    QCOMPARE(kind(cyclic), std::optional(ProjectError::Kind::invalid));
}

void ExportTests::masksFoldersAndClippingReachTheExport()
{
    // The Swift live-mask fixtures, exported: red under a hidden source.
    const auto pixels = [](std::array<int, 4> alpha, int color) {
        QImage image = BrushRaster::context(2, 2, false);
        for (int index = 0; index < 4; ++index)
            image.setPixel(index % 2, index / 2, qRgba(color * alpha[index] / 255, 0, 0, alpha[index]));
        return image;
    };
    const auto alphas = [](const QImage &image) {
        return std::array<int, 4>{qAlpha(image.pixel(0, 0)), qAlpha(image.pixel(1, 0)), qAlpha(image.pixel(0, 1)), qAlpha(image.pixel(1, 1))};
    };
    const QUuid target = QUuid::createUuid(), source = QUuid::createUuid(), folder = QUuid::createUuid();
    const LayerTransform whole{.origin = {0, 0}, .size = {2, 2}, .sampling = LayerSampling::nearest};
    ProjectSnapshot linked{.manifest = {.documentID = QUuid::createUuid(), .width = 2, .height = 2, .activeLayerID = target,
                                        .layers = {{.id = target, .name = "Target", .isVisible = true, .transform = whole, .imageFile = "t.png", .maskSourceID = source},
                                                   {.id = source, .name = "Source", .isVisible = false, .transform = whole, .imageFile = "s.png"}}},
                           .images = {{target, asset(pixels({255, 255, 255, 255}, 255), "Target")}, {source, asset(pixels({255, 0, 128, 255}, 0), "Source")}}};
    QCOMPARE(alphas(ImageExporter::render(linked).image), (std::array<int, 4>{255, 0, 128, 255}));
    linked.manifest.layers[0].opacity = 0.5;
    QCOMPARE(alphas(ImageExporter::render(linked).image), (std::array<int, 4>{127, 0, 64, 127}));
    // The layer's own mask multiplies in.
    linked.manifest.layers[0].maskFile = "mask.png";
    linked.masks.insert({target, asset(gray(2, 2, 128), "Mask")});
    QCOMPARE(alphas(ImageExporter::render(linked).image), (std::array<int, 4>{64, 0, 32, 64}));
    linked.manifest.layers[0].maskEnabled = false;
    QCOMPARE(alphas(ImageExporter::render(linked).image), (std::array<int, 4>{127, 0, 64, 127}));

    // A visible neighbour clipped to its base forms a stack.
    ProjectSnapshot stack{.manifest = {.documentID = QUuid::createUuid(), .width = 2, .height = 2, .activeLayerID = target,
                                       .layers = {{.id = source, .name = "Base", .isVisible = true, .transform = whole, .imageFile = "s.png"},
                                                  {.id = target, .name = "Top", .isVisible = true, .transform = whole, .imageFile = "t.png", .maskSourceID = source}}},
                          .images = {{target, asset(pixels({255, 255, 255, 255}, 255), "Top")}, {source, asset(pixels({255, 128, 32, 0}, 0), "Base")}}};
    const QImage clipped = ImageExporter::render(stack).image;
    QCOMPARE(alphas(clipped), (std::array<int, 4>{255, 128, 32, 0}));
    QCOMPARE(qRed(clipped.pixel(1, 0)), 128);

    // A folder's mask covers what lies inside it, nothing else.
    stack.manifest.layers.insert(stack.manifest.layers.begin(), {.id = folder, .name = "Folder", .isVisible = true, .transform = whole,
                                                                  .imageFile = std::nullopt, .isGroup = true, .maskFile = "folder.png"});
    stack.masks.insert({folder, asset(gray(2, 2, 128), "Folder mask")});
    stack.manifest.layers[1].parentID = folder;
    stack.manifest.layers[2].parentID = folder;
    QCOMPARE(alphas(ImageExporter::render(stack).image), (std::array<int, 4>{128, 64, 16, 0}));
    // A disabled folder mask clips nothing.
    stack.manifest.layers[0].maskEnabled = false;
    QCOMPARE(alphas(ImageExporter::render(stack).image), (std::array<int, 4>{255, 128, 32, 0}));
    stack.manifest.layers[0].maskEnabled = true;
    stack.manifest.layers[1].parentID = std::nullopt;
    stack.manifest.layers[2].parentID = std::nullopt;
    QCOMPARE(alphas(ImageExporter::render(stack).image), (std::array<int, 4>{255, 128, 32, 0}));
}

void ExportTests::blendModesAndPlacedMasksReachTheExport()
{
    const QUuid below = QUuid::createUuid(), above = QUuid::createUuid(), child = QUuid::createUuid();
    const LayerTransform whole{.origin = {0, 0}, .size = {4, 4}, .sampling = LayerSampling::nearest};
    ProjectSnapshot blended{.manifest = {.documentID = QUuid::createUuid(), .width = 4, .height = 4, .activeLayerID = above,
                                         .layers = {{.id = below, .name = "Below", .isVisible = true, .transform = whole, .imageFile = "b.png"},
                                                    {.id = above, .name = "Above", .isVisible = true, .transform = whole, .imageFile = "a.png",
                                                     .blendMode = LayerBlendMode::multiply}}},
                            .images = {{below, asset(solid(4, 4, qRgba(200, 100, 50, 255)), "Below")}, {above, asset(solid(4, 4, qRgba(128, 128, 128, 255)), "Above")}}};
    QCOMPARE(ImageExporter::render(blended).image.pixel(1, 1), qRgba(100, 50, 25, 255));
    blended.manifest.layers[1].blendMode = std::nullopt;
    QCOMPARE(ImageExporter::render(blended).image.pixel(1, 1), qRgba(128, 128, 128, 255));

    // A stack blends with its base's mode; absent means Normal.
    blended.images.insert({child, asset(solid(4, 4, qRgba(128, 128, 128, 255)), "Child")});
    blended.manifest.layers.push_back({.id = child, .name = "Child", .isVisible = true, .transform = whole, .imageFile = "c.png", .maskSourceID = above});
    QCOMPARE(ImageExporter::render(blended).image.pixel(1, 1), qRgba(128, 128, 128, 255));
    blended.manifest.layers[1].blendMode = LayerBlendMode::multiply;
    QCOMPARE(ImageExporter::render(blended).image.pixel(1, 1), qRgba(100, 50, 25, 255));
    blended.manifest.layers.pop_back();

    // A mask moved apart hides by where it sits.
    blended.manifest.layers[1].blendMode = std::nullopt;
    blended.manifest.layers[1].maskFile = "mask.png";
    QImage halves = gray(2, 1, 255);
    halves.scanLine(0)[1] = 0;
    blended.masks.insert({above, asset(halves, "Mask")});
    // Over the whole layer its right half hides.
    const QImage whole4 = ImageExporter::render(blended).image;
    QCOMPARE(whole4.pixel(1, 1), qRgba(128, 128, 128, 255));
    QCOMPARE(whole4.pixel(2, 1), qRgba(200, 100, 50, 255));
    // Placed right, only the last column hides; white borders show.
    blended.manifest.layers[1].maskPlacement = LayerTransform{.origin = {2, 0}, .size = {2, 4}, .sampling = LayerSampling::nearest};
    const QImage placed = ImageExporter::render(blended).image;
    QCOMPARE(placed.pixel(0, 1), qRgba(128, 128, 128, 255));
    QCOMPARE(placed.pixel(2, 1), qRgba(128, 128, 128, 255));
    QCOMPARE(placed.pixel(3, 1), qRgba(200, 100, 50, 255));
}

void ExportTests::aSnapshotsMaskFollowsItsRecord()
{
    ProjectSnapshot masked = snapshot();
    ProjectLayerRecord &layer = masked.manifest.layers[0];
    masked.masks.insert({layer.id, asset(gray(2, 2, 77), "Mask")});
    // No mask file, no mask, whatever the snapshot holds.
    QVERIFY(!masked.mask(layer).has_value());
    layer.maskFile = "mask.png";
    const LayerMask plain = masked.mask(layer).value();
    QCOMPARE(plain.asset.image(), gray(2, 2, 77));
    QCOMPARE(plain.isEnabled, true);
    QCOMPARE(plain.isLinked, true);
    QVERIFY(!plain.placement.has_value());
    // The two flags travel apart.
    layer.maskEnabled = false;
    layer.maskLinked = true;
    layer.maskPlacement = LayerTransform{.origin = {3, 4}, .size = {5, 6}};
    const LayerMask disabled = masked.mask(layer).value();
    QCOMPARE(disabled.isEnabled, false);
    QCOMPARE(disabled.isLinked, true);
    QCOMPARE(disabled.placement.value(), (LayerTransform{.origin = {3, 4}, .size = {5, 6}}));
    layer.maskEnabled = true;
    layer.maskLinked = false;
    const LayerMask unlinked = masked.mask(layer).value();
    QCOMPARE(unlinked.isEnabled, true);
    QCOMPARE(unlinked.isLinked, false);
    masked.masks.clear();
    QVERIFY(!masked.mask(layer).has_value());
}

void ExportTests::aFileThatCannotBeWrittenThrowsAndKeepsTheOldOne()
{
    QTemporaryDir directory;
    const QString path = directory.filePath("kept.png");
    ImageExporter::write("first", path);
    ImageExporter::write("second", path);
    QFile kept(path);
    QVERIFY(kept.open(QIODevice::ReadOnly));
    QCOMPARE(kept.readAll(), QByteArray("second"));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("could not write .*"));
    QVERIFY_EXCEPTION_THROWN(ImageExporter::write("third", directory.filePath("missing/folder/file.png")), std::runtime_error);
}

void ExportTests::aCanvasThatCannotBeAllocatedThrows()
{
    // 10,000 x 10,000 pixels need 400 MB.
    const ProjectSnapshot large{.manifest = {.documentID = QUuid::createUuid(), .width = 10'000, .height = 10'000, .activeLayerID = std::nullopt, .layers = {}},
                                .images = {}};
    std::optional<AddressSpaceLimit> limit(std::in_place, 64 * 1024 * 1024);
    std::optional<ExportError::Kind> thrown;
    try {
        ImageExporter::render(large);
    } catch (const ExportError &error) {
        thrown = error.kind;
    }
    limit.reset();
    QCOMPARE(thrown, std::optional(ExportError::Kind::render));
}

void ExportTests::exportsAreLogged()
{
    QTemporaryDir directory;
    const QString path = directory.filePath("logged.png");
    QTest::ignoreMessage(QtInfoMsg, "rendered 6 x 6 from 1 visible layers");
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression("wrote \\d+ bytes to \".*logged.png\""));
    ImageExporter::exportPNG(snapshot(), path);
}

QTEST_MAIN(ExportTests)
#include "ExportTests.moc"
