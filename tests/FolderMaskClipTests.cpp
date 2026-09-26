#include "Document/LayerMask.h"
#include "IO/ImageExporter.h"
#include "Rendering/LayerRenderer.h"
#include "RenderFixtures.h"
#include "AddressSpaceLimit.h"
#include <QtTest>

namespace {
int value(const QImage &image, int x, int y)
{
    return image.constScanLine(y)[x];
}
}

class FolderMaskClipTests : public QObject {
    Q_OBJECT
private slots:
    void applyMultipliesThePlacedMaskIntoCoverage();
    void applyRotatesSamplesAndAntialiasesLikeALayerMask();
    void coverageThatCannotBeAllocatedThrows();
    void sharedCoverageThatCannotBeCopiedThrows();
    void theSixtyFourthFolderStillClipsAndTheNextDoesNot();
    void drawHandsEachLayerItsFoldersCoverage();
    void nestedFoldersMultiplyAndAreAskedOnce();
    void aFolderClipMultipliesALayersAlpha();
    void aFolderClipMustBeDeviceSizedAlpha();
};

void FolderMaskClipTests::applyMultipliesThePlacedMaskIntoCoverage()
{
    QImage device = BrushRaster::context(40, 20, false);
    QPainter painter(&device);
    painter.translate(10, 0);
    QImage coverage(40, 20, QImage::Format_Alpha8);
    coverage.fill(255);
    QImage halves = gray(2, 1, 255);
    halves.scanLine(0)[1] = 128;
    const FolderMaskClip clip{halves, LayerTransform{.origin = {0, 0}, .size = {20, 10}, .sampling = LayerSampling::nearest}};
    clip.apply(clip.transform.center(), painter, coverage);
    QCOMPARE(value(coverage, 12, 5), 255);
    QCOMPARE(value(coverage, 25, 5), 128);
    QCOMPARE(value(coverage, 5, 5), 0);
    QCOMPARE(value(coverage, 35, 5), 0);
    QCOMPARE(value(coverage, 12, 15), 0);
    clip.apply(clip.transform.center(), painter, coverage);
    QCOMPARE(value(coverage, 12, 5), 255);
    QCOMPARE(value(coverage, 25, 5), 64);

    QImage scaled(40, 20, QImage::Format_Alpha8);
    scaled.fill(255);
    LayerTransform turned = clip.transform;
    turned.flipX = true;
    FolderMaskClip{halves, turned}.apply(turned.center() * 0.5, painter, scaled, 0.5);
    QCOMPARE(value(scaled, 12, 2), 128);
    QCOMPARE(value(scaled, 17, 2), 255);
    QCOMPARE(value(scaled, 22, 2), 0);
    QCOMPARE(value(scaled, 12, 7), 0);
}

void FolderMaskClipTests::applyRotatesSamplesAndAntialiasesLikeALayerMask()
{
    QImage device = BrushRaster::context(40, 40, false);
    QPainter painter(&device);
    QImage halves = gray(2, 1, 255);
    halves.scanLine(0)[1] = 128;
    QImage turned(40, 40, QImage::Format_Alpha8);
    turned.fill(255);
    const LayerTransform quarter{.origin = {10, 15}, .size = {20, 10}, .rotation = 90, .sampling = LayerSampling::nearest};
    FolderMaskClip{halves, quarter}.apply(quarter.center(), painter, turned);
    QCOMPARE(value(turned, 20, 12), 255);
    QCOMPARE(value(turned, 20, 27), 128);
    QCOMPARE(value(turned, 12, 20), 0);

    const auto row = [&](LayerSampling sampling) {
        QImage coverage(40, 40, QImage::Format_Alpha8);
        coverage.fill(255);
        QImage step = gray(2, 1, 0);
        step.scanLine(0)[1] = 255;
        const LayerTransform wide{.origin = {0, 0}, .size = {40, 40}, .sampling = sampling};
        FolderMaskClip{step, wide}.apply(wide.center(), painter, coverage);
        QList<int> values;
        for (int x = 0; x < 40; x += 5)
            values << value(coverage, x, 20);
        return values;
    };
    QCOMPARE(row(LayerSampling::nearest), QList<int>({0, 0, 0, 0, 255, 255, 255, 255}));
    const QList<int> smooth = row(LayerSampling::smooth);
    QVERIFY(std::is_sorted(smooth.begin(), smooth.end()));
    QVERIFY(std::count_if(smooth.begin(), smooth.end(), [](int level) { return level > 0 && level < 255; }) >= 2);

    QImage corner = gray(2, 2, 0);
    corner.scanLine(0)[0] = 255;
    const LayerTransform upright{.origin = {0, 0}, .size = {40, 40}, .sampling = LayerSampling::nearest};
    const auto bright = [&](bool flipX, bool flipY) {
        QImage coverage(40, 40, QImage::Format_Alpha8);
        coverage.fill(255);
        LayerTransform flipped = upright;
        flipped.flipX = flipX;
        flipped.flipY = flipY;
        FolderMaskClip{corner, flipped}.apply(flipped.center(), painter, coverage);
        return QList<int>({value(coverage, 10, 10), value(coverage, 30, 10), value(coverage, 10, 30), value(coverage, 30, 30)});
    };
    QCOMPARE(bright(false, false), QList<int>({255, 0, 0, 0}));
    QCOMPARE(bright(true, false), QList<int>({0, 255, 0, 0}));
    QCOMPARE(bright(false, true), QList<int>({0, 0, 255, 0}));
    QCOMPARE(bright(true, true), QList<int>({0, 0, 0, 255}));

    QImage tilted(40, 40, QImage::Format_Alpha8);
    tilted.fill(255);
    const LayerTransform tilt{.origin = {10, 10}, .size = {20, 20}, .rotation = 30, .sampling = LayerSampling::nearest};
    FolderMaskClip{gray(1, 1, 255), tilt}.apply(tilt.center(), painter, tilted);
    int partial = 0;
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 40; ++x)
            partial += value(tilted, x, y) > 0 && value(tilted, x, y) < 255;
    }
    QVERIFY(partial > 10);
    QCOMPARE(value(tilted, 20, 20), 255);
}

void FolderMaskClipTests::coverageThatCannotBeAllocatedThrows()
{
    QImage device = BrushRaster::context(16384, 16384, true);
    QPainter painter(&device);
    QImage coverage(16384, 16384, QImage::Format_Alpha8);
    const FolderMaskClip clip{gray(1, 1, 255), LayerTransform{.origin = {0, 0}, .size = {16384, 16384}}};
    const QUuid folder = QUuid::createUuid(), inside = QUuid::createUuid();
    std::optional<AddressSpaceLimit> limit(std::in_place, 16 * 1024 * 1024);
    std::optional<ExportError::Kind> applyThrew, drawThrew;
    try {
        clip.apply(clip.transform.center(), painter, coverage);
    } catch (const ExportError &error) {
        applyThrew = error.kind;
    }
    try {
        FolderMaskClip::draw({inside}, [&](QUuid id) { return id == inside ? std::optional(folder) : std::nullopt; },
                             [&](QUuid) -> std::optional<FolderMaskClip::Applier> { return [](const QPainter &, QImage &) {}; },
                             painter, [](QUuid, const QImage &) {});
    } catch (const ExportError &error) {
        drawThrew = error.kind;
    }
    limit.reset();
    QCOMPARE(applyThrew, std::optional(ExportError::Kind::render));
    QCOMPARE(drawThrew, std::optional(ExportError::Kind::render));
}

void FolderMaskClipTests::sharedCoverageThatCannotBeCopiedThrows()
{
    QImage device = BrushRaster::context(4, 4, false);
    QPainter painter(&device);
    QImage coverage(16384, 16384, QImage::Format_Alpha8);
    const QImage keeper = coverage;
    const qint64 coverageBytes = coverage.sizeInBytes();
    const FolderMaskClip clip{gray(1, 1, 255), LayerTransform{.origin = {0, 0}, .size = {4, 4}}};
    // Room for the veil, not for the copy as well.
    std::optional<AddressSpaceLimit> limit(std::in_place, coverageBytes + coverageBytes / 2);
    std::optional<ExportError::Kind> thrown;
    try {
        clip.apply(clip.transform.center(), painter, coverage);
    } catch (const ExportError &error) {
        thrown = error.kind;
    }
    limit.reset();
    QCOMPARE(thrown, std::optional(ExportError::Kind::render));
    QCOMPARE(keeper.size(), QSize(16384, 16384));
}

void FolderMaskClipTests::theSixtyFourthFolderStillClipsAndTheNextDoesNot()
{
    QImage device = BrushRaster::context(4, 4, false);
    QPainter painter(&device);
    const QUuid layerID = QUuid::createUuid();
    QList<QUuid> folders;
    for (int index = 0; index < 65; ++index)
        folders << QUuid::createUuid();
    const auto parent = [&](QUuid id) -> std::optional<QUuid> {
        if (id == layerID)
            return folders[0];
        const int index = folders.indexOf(id);
        return index + 1 < folders.size() ? std::optional(folders[index + 1]) : std::nullopt;
    };
    const LayerTransform whole{.origin = {0, 0}, .size = {4, 4}};
    const FolderMaskClip half{gray(1, 1, 128), whole}, hidden{gray(1, 1, 0), whole};
    const auto clip = [&](QUuid id) -> std::optional<FolderMaskClip::Applier> {
        const FolderMaskClip *mask = id == folders[63] ? &half : id == folders[64] ? &hidden : nullptr;
        if (!mask)
            return std::nullopt;
        return [&, mask](const QPainter &context, QImage &coverage) { mask->apply(whole.center(), context, coverage); };
    };
    int seen = -1;
    FolderMaskClip::draw({layerID}, parent, clip, painter, [&](QUuid, const QImage &coverage) { seen = coverage.isNull() ? -2 : value(coverage, 1, 1); });
    QCOMPARE(seen, 128);
}

void FolderMaskClipTests::drawHandsEachLayerItsFoldersCoverage()
{
    QImage device = BrushRaster::context(8, 4, false);
    QPainter painter(&device);
    const QUuid folder = QUuid::createUuid(), plain = QUuid::createUuid(), outside = QUuid::createUuid();
    const QUuid inside = QUuid::createUuid(), insidePlain = QUuid::createUuid();
    const auto parent = [&](QUuid id) -> std::optional<QUuid> {
        if (id == inside)
            return folder;
        if (id == insidePlain)
            return plain;
        return std::nullopt;
    };
    const FolderMaskClip mask{gray(1, 1, 100), LayerTransform{.origin = {0, 0}, .size = {8, 4}}};
    const auto clip = [&](QUuid id) -> std::optional<FolderMaskClip::Applier> {
        if (id != folder)
            return std::nullopt;
        return [&](const QPainter &context, QImage &coverage) { mask.apply(mask.transform.center(), context, coverage); };
    };
    QList<QUuid> order;
    QHash<QUuid, QImage> clips;
    FolderMaskClip::draw({outside, inside, insidePlain}, parent, clip, painter, [&](QUuid id, const QImage &coverage) {
        order << id;
        clips.insert(id, coverage);
    });
    QCOMPARE(order, QList<QUuid>({outside, inside, insidePlain}));
    QVERIFY(clips[outside].isNull());
    QVERIFY(clips[insidePlain].isNull());
    QCOMPARE(clips[inside].format(), QImage::Format_Alpha8);
    QCOMPARE(clips[inside].size(), QSize(8, 4));
    QCOMPARE(value(clips[inside], 3, 2), 100);
}

void FolderMaskClipTests::nestedFoldersMultiplyAndAreAskedOnce()
{
    QImage device = BrushRaster::context(8, 4, false);
    QPainter painter(&device);
    const QUuid outer = QUuid::createUuid(), middle = QUuid::createUuid(), inner = QUuid::createUuid();
    const QUuid deep = QUuid::createUuid(), deepToo = QUuid::createUuid(), shallow = QUuid::createUuid();
    const auto parent = [&](QUuid id) -> std::optional<QUuid> {
        if (id == deep || id == deepToo)
            return inner;
        if (id == inner)
            return middle;
        if (id == middle || id == shallow)
            return outer;
        return std::nullopt;
    };
    const FolderMaskClip half{gray(1, 1, 128), LayerTransform{.origin = {0, 0}, .size = {8, 4}}};
    QHash<QUuid, int> asked;
    const auto clip = [&](QUuid id) -> std::optional<FolderMaskClip::Applier> {
        asked[id] += 1;
        if (id == middle)
            return std::nullopt;
        return [&](const QPainter &context, QImage &coverage) { half.apply(half.transform.center(), context, coverage); };
    };
    QHash<QUuid, int> seen;
    FolderMaskClip::draw({deep, shallow, deepToo}, parent, clip, painter,
                         [&](QUuid id, const QImage &coverage) { seen.insert(id, value(coverage, 3, 2)); });
    QCOMPARE(seen[deep], 64);
    QCOMPARE(seen[deepToo], 64);
    QCOMPARE(seen[shallow], 128);
    QCOMPARE(asked[outer], 1);
    QCOMPARE(asked[middle], 1);
    QCOMPARE(asked[inner], 1);
}

void FolderMaskClipTests::aFolderClipMultipliesALayersAlpha()
{
    QImage clip(8, 4, QImage::Format_Alpha8);
    clip.fill(255);
    for (int y = 0; y < 4; ++y) {
        clip.scanLine(y)[4] = clip.scanLine(y)[5] = 128;
        clip.scanLine(y)[6] = clip.scanLine(y)[7] = 0;
    }
    QImage surface = BrushRaster::context(8, 4, false);
    QPainter painter(&surface);
    const LayerTransform transform = placedAt({2, 0}, {6, 4});
    QImage mask = gray(6, 4, 255);
    for (int y = 0; y < 4; ++y)
        mask.scanLine(y)[0] = mask.scanLine(y)[2] = 128;
    LayerRenderer::draw(solid(6, 4, qRgba(255, 255, 255, 255)), transform, transform.center(), painter,
                        {.mask = mask, .clip = clip});
    painter.end();
    QCOMPARE(qAlpha(surface.pixel(1, 1)), 0);
    QVERIFY(std::abs(qAlpha(surface.pixel(2, 1)) - 128) <= 1);
    QCOMPARE(qAlpha(surface.pixel(3, 1)), 255);
    QVERIFY(std::abs(qAlpha(surface.pixel(4, 1)) - 64) <= 1);
    QVERIFY(std::abs(qAlpha(surface.pixel(5, 1)) - 128) <= 1);
    QCOMPARE(qAlpha(surface.pixel(6, 1)), 0);

    QImage unmasked = BrushRaster::context(8, 4, false);
    QPainter second(&unmasked);
    LayerRenderer::draw(solid(6, 4, qRgba(255, 255, 255, 255)), transform, transform.center(), second, {.clip = clip});
    second.end();
    QCOMPARE(qAlpha(unmasked.pixel(2, 1)), 255);
    QVERIFY(std::abs(qAlpha(unmasked.pixel(4, 1)) - 128) <= 1);
    QCOMPARE(qAlpha(unmasked.pixel(7, 1)), 0);
}

void FolderMaskClipTests::aFolderClipMustBeDeviceSizedAlpha()
{
    QImage surface = BrushRaster::context(8, 4, false);
    QPainter painter(&surface);
    const LayerTransform transform = placedAt({0, 0}, {8, 4});
    const QImage image = solid(8, 4, qRgba(9, 9, 9, 255));
    QVERIFY_THROWS_EXCEPTION(std::logic_error, LayerRenderer::draw(image, transform, transform.center(), painter, {.clip = gray(8, 4, 255)}));
    QVERIFY_THROWS_EXCEPTION(std::logic_error, LayerRenderer::draw(image, transform, transform.center(), painter,
                                                                   {.clip = QImage(4, 4, QImage::Format_Alpha8)}));
}

QTEST_MAIN(FolderMaskClipTests)
#include "FolderMaskClipTests.moc"
