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

// White rim, black middle: reveals beyond its own pixels.
QImage ringMask(int side)
{
    QImage mask = gray(side, side, 255);
    for (int y = 1; y < side - 1; ++y) {
        for (int x = 1; x < side - 1; ++x)
            mask.scanLine(y)[x] = 0;
    }
    return mask;
}

const LayerTransform layer{.origin = {0, 0}, .size = {100, 100}};
const LayerTransform rightHalf{.origin = {50, 0}, .size = {50, 100}};

std::optional<QImage> tiny()
{
    return gray(1, 1, 7);
}
}

class MaskPlacementTests : public QObject {
    Q_OBJECT
private slots:
    void aMaskCoveringItsLayerClipsAsItIs();
    void aPlacedMaskIsResampledIntoTheLayerGrid();
    void theBackgroundComesFromTheThumbnail();
    void aLimitShrinksThePlacedGrid();
    void placedMasksAreCached();
    void aPlacedMaskThatCannotBeRenderedIsLoggedAndNil();
    void theCacheKeysOnEveryPartOfThePlacement();
    void aFailedBuildIsNeverCached();
    void hugeGridsAreReturnedButNotKept();
    void theNinthEntryEvictsTheLeastRecentlyUsed();
    void thePixelBudgetEvictsToo();
    void fineMasksArePlacedFromHalvings();
};

void MaskPlacementTests::aMaskCoveringItsLayerClipsAsItIs()
{
    const QImage pixels = ringMask(8);
    LayerMask mask(LayerMask::assetFrom(pixels));
    QCOMPARE(mask.clipImage(std::nullopt, layer, 100, 100).value().cacheKey(), pixels.cacheKey());
    LayerTransform sameSpot = layer;
    sameSpot.sampling = LayerSampling::nearest;
    QCOMPARE(mask.clipImage(sameSpot, layer, 100, 100).value().cacheKey(), pixels.cacheKey());
    QCOMPARE(mask.clipImage(rightHalf, layer, 0, 100).value().cacheKey(), pixels.cacheKey());
    QCOMPARE(mask.clipImage(rightHalf, layer, 100, 0).value().cacheKey(), pixels.cacheKey());
    mask.isEnabled = false;
    QVERIFY(!mask.clipImage(rightHalf, layer, 100, 100).has_value());
}

void MaskPlacementTests::aPlacedMaskIsResampledIntoTheLayerGrid()
{
    const LayerMask mask(LayerMask::assetFrom(ringMask(10)));
    const QImage placed = mask.clipImage(rightHalf, layer, 200, 100).value();
    QCOMPARE(placed.format(), QImage::Format_Grayscale8);
    QCOMPARE(placed.size(), QSize(200, 100));
    QCOMPARE(value(placed, 20, 50), 255);
    QCOMPARE(value(placed, 150, 50), 0);
    QCOMPARE(value(placed, 102, 50), 255);
    QCOMPARE(value(placed, 150, 2), 255);
    const LayerMask hiding(LayerMask::assetFrom(gray(10, 10, 0)));
    QCOMPARE(value(hiding.clipImage(rightHalf, layer, 200, 100).value(), 20, 50), 0);
}

void MaskPlacementTests::theBackgroundComesFromTheThumbnail()
{
    const LayerMask whiteRimBlackThumbnail(ImportedImage(ringMask(10), gray(4, 4, 0), "Layer Mask"));
    QCOMPARE(value(whiteRimBlackThumbnail.clipImage(rightHalf, layer, 200, 100).value(), 20, 50), 0);
    const LayerMask blackMaskWhiteThumbnail(ImportedImage(gray(10, 10, 0), gray(4, 4, 255), "Layer Mask"));
    QCOMPARE(value(blackMaskWhiteThumbnail.clipImage(rightHalf, layer, 200, 100).value(), 20, 50), 255);
}

void MaskPlacementTests::aLimitShrinksThePlacedGrid()
{
    const LayerMask mask(LayerMask::assetFrom(ringMask(10)));
    QCOMPARE(mask.clipImage(rightHalf, layer, 4000, 2000, 100).value().size(), QSize(100, 50));
    QCOMPARE(mask.clipImage(rightHalf, layer, 4001, 2000, 100).value().size(), QSize(100, 50));
    QCOMPARE(mask.clipImage(rightHalf, layer, 300, 90, 100).value().size(), QSize(100, 30));
    QCOMPARE(mask.clipImage(rightHalf, layer, 80, 40, 100).value().size(), QSize(80, 40));
    QCOMPARE(mask.clipImage(rightHalf, layer, 80, 40, 0.5).value().size(), QSize(1, 1));
    const QImage shrunk = mask.clipImage(rightHalf, layer, 4000, 2000, 100).value();
    QCOMPARE(value(shrunk, 10, 25), 255);
    QCOMPARE(value(shrunk, 75, 25), 0);
}

void MaskPlacementTests::placedMasksAreCached()
{
    const LayerMask mask(LayerMask::assetFrom(ringMask(10)));
    const QImage first = mask.clipImage(rightHalf, layer, 64, 64).value();
    QCOMPARE(mask.clipImage(rightHalf, layer, 64, 64).value().cacheKey(), first.cacheKey());
    LayerTransform nudged = rightHalf;
    nudged.origin.rx() += 1;
    QVERIFY(mask.clipImage(nudged, layer, 64, 64).value().cacheKey() != first.cacheKey());
}

void MaskPlacementTests::aPlacedMaskThatCannotBeRenderedIsLoggedAndNil()
{
    const LayerMask mask(LayerMask::assetFrom(ringMask(10)));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("a placed mask could not be rendered: .*"));
    std::optional<AddressSpaceLimit> limit(std::in_place, 16 * 1024 * 1024);
    const bool rendered = mask.clipImage(rightHalf, layer, 20000, 20000).has_value();
    limit.reset();
    QVERIFY(!rendered);
}

void MaskPlacementTests::theCacheKeysOnEveryPartOfThePlacement()
{
    MaskPlacementCache cache;
    const QImage mask = gray(2, 2, 9);
    int builds = 0;
    const auto build = [&] { builds += 1; return tiny(); };
    QCOMPARE(cache.image(mask, rightHalf, layer, 10, 20, build).value().size(), QSize(1, 1));
    cache.image(mask, rightHalf, layer, 10, 20, build);
    QCOMPARE(builds, 1);
    cache.image(mask.copy(), rightHalf, layer, 10, 20, build);
    QCOMPARE(builds, 2);
    cache.image(mask, layer, layer, 10, 20, build);
    QCOMPARE(builds, 3);
    cache.image(mask, rightHalf, rightHalf, 10, 20, build);
    QCOMPARE(builds, 4);
    cache.image(mask, rightHalf, layer, 11, 20, build);
    QCOMPARE(builds, 5);
    cache.image(mask, rightHalf, layer, 10, 21, build);
    QCOMPARE(builds, 6);
    cache.image(mask, rightHalf, layer, 10, 20, build);
    QCOMPARE(builds, 6);
}

void MaskPlacementTests::aFailedBuildIsNeverCached()
{
    MaskPlacementCache cache;
    int builds = 0;
    const auto failing = [&]() -> std::optional<QImage> { builds += 1; return std::nullopt; };
    QVERIFY(!cache.image(gray(2, 2, 9), rightHalf, layer, 10, 20, failing).has_value());
    QVERIFY(!cache.image(gray(2, 2, 9), rightHalf, layer, 10, 20, failing).has_value());
    QCOMPARE(builds, 2);
}

void MaskPlacementTests::hugeGridsAreReturnedButNotKept()
{
    MaskPlacementCache cache;
    const QImage mask = gray(2, 2, 9);
    int builds = 0;
    const auto build = [&] { builds += 1; return tiny(); };
    cache.image(mask, rightHalf, layer, 10, 10, build);
    QVERIFY(cache.image(mask, rightHalf, layer, 8001, 8000, build).has_value());
    cache.image(mask, rightHalf, layer, 8001, 8000, build);
    QCOMPARE(builds, 3);
    cache.image(mask, rightHalf, layer, 10, 10, build);
    QCOMPARE(builds, 3);
    cache.image(mask, rightHalf, layer, 7000, 8000, build);
    cache.image(mask, rightHalf, layer, 7000, 8000, build);
    QCOMPARE(builds, 4);
}

void MaskPlacementTests::theNinthEntryEvictsTheLeastRecentlyUsed()
{
    MaskPlacementCache cache;
    const QImage mask = gray(2, 2, 9);
    int builds = 0;
    const auto build = [&] { builds += 1; return tiny(); };
    for (int width = 1; width <= 8; ++width)
        cache.image(mask, rightHalf, layer, width, 1, build);
    cache.image(mask, rightHalf, layer, 1, 1, build);
    QCOMPARE(builds, 8);
    cache.image(mask, rightHalf, layer, 9, 1, build);
    QCOMPARE(builds, 9);
    cache.image(mask, rightHalf, layer, 1, 1, build);
    QCOMPARE(builds, 9);
    cache.image(mask, rightHalf, layer, 2, 1, build);
    QCOMPARE(builds, 10);
}

void MaskPlacementTests::thePixelBudgetEvictsToo()
{
    MaskPlacementCache cache;
    const QImage mask = gray(2, 2, 9);
    int builds = 0;
    const auto build = [&] { builds += 1; return tiny(); };
    for (int height = 4000; height < 4003; ++height)
        cache.image(mask, rightHalf, layer, 4000, height, build);
    cache.image(mask, rightHalf, layer, 4000, 4000, build);
    QCOMPARE(builds, 3);
    cache.image(mask, rightHalf, layer, 4000, 4003, build);
    QCOMPARE(builds, 4);
    cache.image(mask, rightHalf, layer, 4000, 4000, build);
    QCOMPARE(builds, 4);
    cache.image(mask, rightHalf, layer, 4000, 4001, build);
    QCOMPARE(builds, 5);
}

void MaskPlacementTests::fineMasksArePlacedFromHalvings()
{
    QImage stripes = gray(1024, 16, 0);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 1024; ++x)
            stripes.scanLine(y)[x] = x % 4 == 1 || x % 4 == 2 ? 255 : 0;
    }
    const LayerMask mask(LayerMask::assetFrom(stripes));
    const LayerTransform wide{.origin = {0, 0}, .size = {256, 16}};
    const LayerTransform narrow{.origin = {64, 0}, .size = {128, 16}};
    const QImage placed = mask.clipImage(narrow, wide, 256, 16).value();
    for (int x = 72; x < 184; ++x)
        QVERIFY(std::abs(value(placed, x, 8) - 127) <= 12);
}

QTEST_MAIN(MaskPlacementTests)
#include "MaskPlacementTests.moc"
