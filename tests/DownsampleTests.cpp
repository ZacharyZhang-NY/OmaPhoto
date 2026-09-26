#include "Document/BrushStroke.h"
#include "Rendering/DownsampleCache.h"
#include "Rendering/LayerRenderer.h"
#include "AddressSpaceLimit.h"
#include <QtTest>
#include <atomic>
#include <cstdlib>
#include <functional>
#include <new>
#include <latch>
#include <thread>

// operator new fails from this size up; QImage uses malloc.
static std::atomic<std::size_t> failingAllocationSize{SIZE_MAX};
// And at exactly this size, on any thread.
static std::atomic<std::size_t> failingExactSize{0};

void *operator new(std::size_t size)
{
    if (size >= failingAllocationSize || size == failingExactSize)
        throw std::bad_alloc();
    if (void *memory = std::malloc(size))
        return memory;
    throw std::bad_alloc();
}

void *operator new[](std::size_t size)
{
    return operator new(size);
}

// New is malloc, so free pairs; GCC 16 cannot tell.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
void operator delete(void *memory) noexcept
{
    std::free(memory);
}

void operator delete(void *memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void *memory) noexcept
{
    std::free(memory);
}

void operator delete[](void *memory, std::size_t) noexcept
{
    std::free(memory);
}
#pragma GCC diagnostic pop

namespace {
// An opaque RGBA image whose gray comes from gray(x, y).
QImage image(int width, int height, const std::function<int(int, int)> &gray)
{
    QImage result = BrushRaster::context(width, height, false);
    for (int y = 0; y < height; ++y) {
        uchar *row = result.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const uchar value = uchar(gray(x, y));
            row[x * 4] = row[x * 4 + 1] = row[x * 4 + 2] = value;
            row[x * 4 + 3] = 255;
        }
    }
    return result;
}

// The image drawn by the layer renderer, High quality.
QImage drawn(const QImage &source, int width, int height)
{
    QImage surface = BrushRaster::context(width, height, false);
    QPainter painter(&surface);
    const LayerTransform transform{.origin = {0, 0}, .size = QSizeF(source.size()), .sampling = LayerSampling::high};
    const double scale = double(width) / source.width();
    LayerRenderer::draw(source, transform, transform.center() * scale, painter, {.scale = scale});
    painter.end();
    return surface;
}

QImage grayMask(int width, int height, int value)
{
    QImage result = BrushRaster::context(width, height, true);
    result.fill(value);
    return result;
}
}

class DownsampleTests : public QObject {
    Q_OBJECT
private slots:
    void halvingsAreReusedAndOnlyUsedForLargeReductions();
    void aHardEdgeStaysSharpShrunkEightTimes();
    void fineStripesAverageToFlatGrayWithoutShimmer();
    void translucentEdgesStayValidAndMasksStayGray();
    void levelsFollowTheReductionFactor();
    void halvingRoundsOddSizesUp();
    void flatColorStaysFlatAndEdgesFadeIntoTransparency();
    void fineStripesFlattenAndAHardEdgeStaysSharp();
    void ringingAlphaNeverDropsBelowItsColor();
    void everyChannelKeepsItsOwnValues();
    void aFailedScratchAllocationIsNil();
    void bandsTakeTheirScratchFromTheCaller();
    void masksRepeatTheirOddLastColumnAndRow();
    void aMaskHalvesAsItsColorTwin();
    void aMaskEdgeRepeatsItsLastColumn();
    void aPieceLinesUpWithTheWholeImage();
    void aSymmetricImageStaysSymmetric();
    void deeperRequestsExtendTheChain();
    void onePixelImagesAreNeverHalved();
    void leastRecentlyUsedCopiesLeaveFirst();
    void theRequestedCopyOutlivesItsBudget();
    void concurrentRequestsAgree();
    void aFailedHalvingStopsTheChain();
};

void DownsampleTests::halvingsAreReusedAndOnlyUsedForLargeReductions()
{
    const QImage source = image(1024, 512, [](int x, int) { return x % 2 == 0 ? 0 : 255; });
    DownsampleCache &cache = DownsampleCache::shared();
    QCOMPARE(cache.imageDrawnAt(source, 0.6).cacheKey(), source.cacheKey());
    const QImage eighth = cache.imageDrawnAt(source, 0.125);
    QCOMPARE(eighth.size(), QSize(128, 64));
    QCOMPARE(cache.imageDrawnAt(source, 0.3).width(), 512);
    QCOMPARE(cache.imageDrawnAt(source, 0.125).cacheKey(), eighth.cacheKey());
}

void DownsampleTests::aHardEdgeStaysSharpShrunkEightTimes()
{
    const QImage source = image(4096, 64, [](int x, int) { return x < 2048 ? 0 : 255; });
    const QImage pixels = drawn(source, 512, 8);
    int soft = 0;
    for (int x = 0; x < 512; ++x) {
        const int value = pixels.constScanLine(4)[x * 4];
        soft += value > 40 && value < 215;
    }
    QVERIFY2(soft <= 3, qPrintable(QString("the edge smears across %1 pixels").arg(soft)));
    QVERIFY(pixels.constScanLine(4)[250 * 4] < 10);
    QVERIFY(pixels.constScanLine(4)[262 * 4] > 245);
}

void DownsampleTests::fineStripesAverageToFlatGrayWithoutShimmer()
{
    const QImage source = image(2048, 256, [](int x, int) { return x % 2 == 0 ? 0 : 255; });
    const QImage pixels = drawn(source, 256, 32);
    // The outermost pixels fade into the transparent edge.
    double sum = 0, squares = 0;
    for (int x = 4; x < 252; ++x)
        sum += pixels.constScanLine(16)[x * 4];
    const double mean = sum / 248;
    for (int x = 4; x < 252; ++x)
        squares += std::pow(pixels.constScanLine(16)[x * 4] - mean, 2);
    const double spread = std::sqrt(squares / 248);
    QVERIFY2(std::abs(mean - 127.5) < 8, qPrintable(QString("mean %1").arg(mean)));
    QVERIFY2(spread < 6, qPrintable(QString("stripes shimmer after shrinking: spread %1").arg(spread)));
}

void DownsampleTests::translucentEdgesStayValidAndMasksStayGray()
{
    QImage translucent = BrushRaster::context(512, 512, false);
    QPainter painter(&translucent);
    painter.fillRect(128, 128, 256, 256, QColor(255, 255, 255, 128));
    painter.end();
    const QImage shrunk = DownsampleCache::shared().imageDrawnAt(translucent, 0.25);
    QCOMPARE(shrunk.size(), QSize(128, 128));
    QCOMPARE(shrunk.format(), QImage::Format_RGBA8888_Premultiplied);
    int ringing = 0;
    for (int y = 0; y < shrunk.height(); ++y) {
        const uchar *row = shrunk.constScanLine(y);
        for (int x = 0; x < shrunk.width(); ++x)
            ringing += row[x * 4] > row[x * 4 + 3] || row[x * 4 + 1] > row[x * 4 + 3] || row[x * 4 + 2] > row[x * 4 + 3];
    }
    QCOMPARE(ringing, 0);
    QCOMPARE(shrunk.constScanLine(64)[64 * 4 + 3], uchar(128));

    QImage mask = grayMask(512, 512, 0);
    QPainter masking(&mask);
    masking.fillRect(0, 0, 256, 512, Qt::white);
    masking.end();
    const QImage level = DownsampleCache::shared().imageDrawnAt(mask, 0.25);
    QCOMPARE(level.size(), QSize(128, 128));
    QCOMPARE(level.format(), QImage::Format_Grayscale8);
    QCOMPARE(level.constScanLine(64)[20], uchar(255));
    QCOMPARE(level.constScanLine(64)[108], uchar(0));
}

void DownsampleTests::levelsFollowTheReductionFactor()
{
    QCOMPARE(DownsampleCache::level(1), 0);
    QCOMPARE(DownsampleCache::level(0.5), 0);
    QCOMPARE(DownsampleCache::level(0.499), 1);
    QCOMPARE(DownsampleCache::level(0.26), 1);
    QCOMPARE(DownsampleCache::level(0.25), 2);
    QCOMPARE(DownsampleCache::level(0.125), 3);
    QCOMPARE(DownsampleCache::level(1.0 / 64), 6);
    QCOMPARE(DownsampleCache::level(0.0001), 6);
    QCOMPARE(DownsampleCache::level(0), 0);
    QCOMPARE(DownsampleCache::level(-0.25), 0);
    QCOMPARE(DownsampleCache::level(std::nan("")), 0);
    QCOMPARE(DownsampleCache::level(INFINITY), 0);
}

void DownsampleTests::halvingRoundsOddSizesUp()
{
    QCOMPARE(DownsampleCache::halve(image(9, 5, [](int, int) { return 200; })).value().size(), QSize(5, 3));
    QCOMPARE(DownsampleCache::halve(grayMask(9, 5, 200)).value().size(), QSize(5, 3));
    QCOMPARE(DownsampleCache::halve(image(8, 4, [](int, int) { return 200; })).value().size(), QSize(4, 2));
    QCOMPARE(DownsampleCache::halve(image(1, 8, [](int, int) { return 200; })).value().size(), QSize(1, 4));
}

void DownsampleTests::flatColorStaysFlatAndEdgesFadeIntoTransparency()
{
    const QImage half = DownsampleCache::halve(image(64, 64, [](int, int) { return 200; })).value();
    QCOMPARE(half.format(), QImage::Format_RGBA8888_Premultiplied);
    const uchar *middle = half.constScanLine(16) + 16 * 4;
    QCOMPARE(middle[0], uchar(200));
    QCOMPARE(middle[1], uchar(200));
    QCOMPARE(middle[2], uchar(200));
    QCOMPARE(middle[3], uchar(255));
    const uchar *corner = half.constScanLine(0);
    QVERIFY(corner[3] > 100 && corner[3] < 255);
    QVERIFY(corner[0] <= corner[3]);
}

void DownsampleTests::fineStripesFlattenAndAHardEdgeStaysSharp()
{
    const QImage stripes = DownsampleCache::halve(image(256, 32, [](int x, int) { return x % 2 == 0 ? 0 : 255; })).value();
    for (int x = 8; x < 120; ++x)
        QVERIFY(std::abs(stripes.constScanLine(8)[x * 4] - 127) <= 3);
    const QImage edge = DownsampleCache::halve(image(256, 32, [](int x, int) { return x < 128 ? 0 : 255; })).value();
    int soft = 0;
    for (int x = 0; x < 128; ++x) {
        const int value = edge.constScanLine(8)[x * 4];
        soft += value > 40 && value < 215;
    }
    QVERIFY(soft <= 2);
    QVERIFY(edge.constScanLine(8)[60 * 4] < 10);
    QVERIFY(edge.constScanLine(8)[67 * 4] > 245);
}

void DownsampleTests::ringingAlphaNeverDropsBelowItsColor()
{
    QImage step = BrushRaster::context(64, 16, false);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 64; ++x)
            step.setPixel(x, y, qRgba(100, 100, 100, x < 32 ? 255 : 100));
    }
    const QImage half = DownsampleCache::halve(step).value();
    int lowestAlpha = 255, brightest = 0;
    for (int x = 12; x < 28; ++x) {
        const uchar *pixel = half.constScanLine(4) + x * 4;
        QVERIFY(pixel[0] <= pixel[3] && pixel[1] <= pixel[3] && pixel[2] <= pixel[3]);
        lowestAlpha = std::min<int>(lowestAlpha, pixel[3]);
        brightest = std::max<int>(brightest, pixel[0]);
    }
    QVERIFY(lowestAlpha < 100);
    QCOMPARE(brightest, 100);
}

void DownsampleTests::everyChannelKeepsItsOwnValues()
{
    QImage colored = BrushRaster::context(64, 64, false);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x)
            colored.setPixel(x, y, x < 32 ? qRgba(10, 120, 200, 255) : qRgba(90, 30, 60, 240));
    }
    const QImage half = DownsampleCache::halve(colored).value();
    const uchar *left = half.constScanLine(16) + 6 * 4, *right = half.constScanLine(16) + 26 * 4;
    QCOMPARE(left[0], uchar(10));
    QCOMPARE(left[1], uchar(120));
    QCOMPARE(left[2], uchar(200));
    QCOMPARE(left[3], uchar(255));
    QCOMPARE(right[0], uchar(90));
    QCOMPARE(right[1], uchar(30));
    QCOMPARE(right[2], uchar(60));
    QCOMPARE(right[3], uchar(240));
}

void DownsampleTests::aFailedScratchAllocationIsNil()
{
    const QImage strip = grayMask(30000, 2, 255);
    QCOMPARE(DownsampleCache::halve(strip).value().size(), QSize(15000, 1));
    failingAllocationSize = 12 * 15000 * sizeof(float);
    bool halved = true, threw = false;
    try {
        halved = DownsampleCache::halve(strip).has_value();
    } catch (...) {
        threw = true;
    }
    failingAllocationSize = SIZE_MAX;
    QVERIFY(!threw);
    QVERIFY(!halved);
    DownsampleCache cache;
    failingAllocationSize = 12 * 15000 * sizeof(float);
    const DownsampleCache::Reduced kept = cache.imageAtLevel(strip, 1);
    failingAllocationSize = SIZE_MAX;
    QCOMPARE(kept.level, 0);
    QCOMPARE(kept.image.cacheKey(), strip.cacheKey());
}

void DownsampleTests::bandsTakeTheirScratchFromTheCaller()
{
    // Ten threaded bands; any lone row of sums is refused.
    const QImage colour = image(2006, 640, [](int x, int y) { return (x * 7 + y * 3) % 256; });
    failingExactSize = 1011 * 4 * sizeof(float);
    std::optional<QImage> half;
    bool threw = false;
    try {
        half = DownsampleCache::halve(colour, true);
    } catch (...) {
        threw = true;
    }
    failingExactSize = 0;
    QVERIFY(!threw);
    QCOMPARE(half.value().size(), QSize(1003, 320));
}

void DownsampleTests::masksRepeatTheirOddLastColumnAndRow()
{
    const QImage half = DownsampleCache::halve(grayMask(9, 7, 255)).value();
    QCOMPARE(half.format(), QImage::Format_Grayscale8);
    QCOMPARE(half.size(), QSize(5, 4));
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 5; ++x)
            QCOMPARE(half.constScanLine(y)[x], uchar(255));
    }
}

void DownsampleTests::aMaskHalvesAsItsColorTwin()
{
    // Uneven columns, alike on every row, gray and colour.
    QImage gray(66, 20, QImage::Format_Grayscale8);
    QImage color(66, 20, QImage::Format_RGBA8888_Premultiplied);
    for (int y = 0; y < gray.height(); ++y) {
        for (int x = 0; x < gray.width(); ++x) {
            const int value = (x * 37) % 200 + 20;
            gray.scanLine(y)[x] = uchar(value);
            color.setPixelColor(x, y, QColor(value, value, value));
        }
    }
    for (const bool threaded : {false, true}) {
        const QImage halfGray = DownsampleCache::halve(gray, threaded).value();
        const QImage halfColor = DownsampleCache::halve(color, threaded).value();
        QCOMPARE(halfGray.size(), QSize(33, 10));
        // Nothing carries from one row into the next.
        for (int y = 1; y < 10; ++y)
            QVERIFY(std::equal(halfGray.constScanLine(0), halfGray.constScanLine(0) + 33, halfGray.constScanLine(y)));
        // Inside, where colour's padding never reaches, both agree.
        for (int y = 3; y <= 6; ++y) {
            for (int x = 3; x <= 29; ++x)
                QCOMPARE(int(halfGray.constScanLine(y)[x]), int(halfColor.constScanLine(y)[x * 4]));
        }
    }
}

void DownsampleTests::aMaskEdgeRepeatsItsLastColumn()
{
    // Black, a white last column: half the kernel lands there.
    QImage mask(16, 4, QImage::Format_Grayscale8);
    mask.fill(0);
    for (int y = 0; y < mask.height(); ++y)
        mask.scanLine(y)[15] = 255;
    const QImage half = DownsampleCache::halve(mask).value();
    QCOMPARE(half.size(), QSize(8, 2));
    for (int y = 0; y < 2; ++y) {
        QVERIFY2(half.constScanLine(y)[7] >= 127 && half.constScanLine(y)[7] <= 128, qPrintable(QString::number(half.constScanLine(y)[7])));
        QCOMPARE(half.constScanLine(y)[0], uchar(0));
    }
}

void DownsampleTests::aPieceLinesUpWithTheWholeImage()
{
    const auto pattern = [](int x, int y) { return (x * 37 + y * 101 + (x / 8) * (y / 8) * 53) % 256; };
    const QImage whole = DownsampleCache::halve(image(128, 128, pattern)).value();
    const QImage piece = DownsampleCache::halve(image(64, 64, [&](int x, int y) { return pattern(x + 32, y + 32); })).value();
    for (int y = 8; y < 24; ++y) {
        for (int x = 8; x < 24; ++x)
            QCOMPARE(piece.pixel(x, y), whole.pixel(x + 16, y + 16));
    }
}

void DownsampleTests::aSymmetricImageStaysSymmetric()
{
    const auto bars = [](int x, int y) { return (x >= 20 && x < 44) != (y >= 28 && y < 36) ? 255 : 0; };
    const QImage half = DownsampleCache::halve(image(64, 64, bars)).value();
    QCOMPARE(half.size(), QSize(32, 32));
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            QCOMPARE(half.pixel(x, y), half.pixel(31 - x, y));
            QCOMPARE(half.pixel(x, y), half.pixel(x, 31 - y));
        }
    }
    QVERIFY(qRed(half.pixel(16, 4)) > 245);
    QVERIFY(qRed(half.pixel(4, 16)) > 245);
    QVERIFY(qRed(half.pixel(4, 4)) < 10);
}

void DownsampleTests::deeperRequestsExtendTheChain()
{
    DownsampleCache cache;
    const QImage source = image(64, 64, [](int x, int y) { return (x * 3 + y * 5) % 256; });
    const DownsampleCache::Reduced half = cache.imageAtLevel(source, 1);
    QCOMPARE(half.level, 1);
    QCOMPARE(half.image.size(), QSize(32, 32));
    const DownsampleCache::Reduced eighth = cache.imageAtLevel(source, 3);
    QCOMPARE(eighth.level, 3);
    QCOMPARE(eighth.image.size(), QSize(8, 8));
    QCOMPARE(eighth.image, DownsampleCache::halve(DownsampleCache::halve(half.image).value()).value());
    QCOMPARE(cache.imageAtLevel(source, 1).image.cacheKey(), half.image.cacheKey());
    QCOMPARE(cache.imageAtLevel(source, 3).image.cacheKey(), eighth.image.cacheKey());
    const DownsampleCache::Reduced capped = cache.imageAtLevel(source, 9);
    QCOMPARE(capped.level, 6);
    QCOMPARE(capped.image.size(), QSize(1, 1));
}

void DownsampleTests::onePixelImagesAreNeverHalved()
{
    DownsampleCache cache;
    const QImage dot = image(1, 1, [](int, int) { return 9; });
    const DownsampleCache::Reduced same = cache.imageAtLevel(dot, 3);
    QCOMPARE(same.level, 0);
    QCOMPARE(same.image.cacheKey(), dot.cacheKey());
    const DownsampleCache::Reduced stopped = cache.imageAtLevel(image(4, 1, [](int, int) { return 9; }), 5);
    QCOMPARE(stopped.level, 2);
    QCOMPARE(stopped.image.size(), QSize(1, 1));
    const QImage wide = image(8, 8, [](int, int) { return 9; });
    const DownsampleCache::Reduced none = cache.imageAtLevel(wide, 0);
    QCOMPARE(none.level, 0);
    QCOMPARE(none.image.cacheKey(), wide.cacheKey());
}

void DownsampleTests::leastRecentlyUsedCopiesLeaveFirst()
{
    DownsampleCache cache(150);
    const QImage a = image(16, 16, [](int, int) { return 1; }), b = image(16, 16, [](int, int) { return 2; });
    const QImage c = image(16, 16, [](int, int) { return 3; });
    const qint64 firstA = cache.imageAtLevel(a, 1).image.cacheKey();
    const qint64 firstB = cache.imageAtLevel(b, 1).image.cacheKey();
    QCOMPARE(cache.imageAtLevel(a, 1).image.cacheKey(), firstA);
    cache.imageAtLevel(c, 1);
    QCOMPARE(cache.imageAtLevel(a, 1).image.cacheKey(), firstA);
    QVERIFY(cache.imageAtLevel(b, 1).image.cacheKey() != firstB);
}

void DownsampleTests::theRequestedCopyOutlivesItsBudget()
{
    DownsampleCache cache(10);
    const QImage large = image(16, 16, [](int, int) { return 1; });
    const DownsampleCache::Reduced first = cache.imageAtLevel(large, 2);
    QCOMPARE(first.level, 2);
    QCOMPARE(first.image.size(), QSize(4, 4));
    QCOMPARE(cache.imageAtLevel(large, 2).image.cacheKey(), first.image.cacheKey());
    QCOMPARE(cache.imageAtLevel(large, 1).image.size(), QSize(8, 8));
}

void DownsampleTests::concurrentRequestsAgree()
{
    DownsampleCache cache;
    const QImage source = image(512, 512, [](int x, int y) { return (x ^ y) % 256; });
    const QImage expected = DownsampleCache::halve(DownsampleCache::halve(source).value()).value();
    constexpr int workers = 8;
    std::latch start(1);
    std::vector<QImage> results(workers);
    std::vector<std::thread> threads;
    for (int index = 0; index < workers; ++index) {
        threads.emplace_back([&, index] {
            start.wait();
            results[index] = cache.imageAtLevel(source, 2).image;
        });
    }
    start.count_down();
    for (std::thread &thread : threads)
        thread.join();
    for (const QImage &result : results)
        QCOMPARE(result, expected);
    QCOMPARE(cache.imageAtLevel(source, 2).image, expected);
}

void DownsampleTests::aFailedHalvingStopsTheChain()
{
    const QImage large = BrushRaster::context(8192, 8192, false);
    std::optional<AddressSpaceLimit> limit(std::in_place, 16 * 1024 * 1024);
    const bool halved = DownsampleCache::halve(large).has_value();
    DownsampleCache cache;
    const DownsampleCache::Reduced reduced = cache.imageAtLevel(large, 2);
    limit.reset();
    QVERIFY(!halved);
    QCOMPARE(reduced.level, 0);
    QCOMPARE(reduced.image.cacheKey(), large.cacheKey());
}

QTEST_APPLESS_MAIN(DownsampleTests)
#include "DownsampleTests.moc"
