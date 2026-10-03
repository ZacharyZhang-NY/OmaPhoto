#include "Document/BrushStroke.h"
#include "Document/LayerEffects+Renderer.h"
#include "Rendering/EffectsPreviewCache.h"
#include "RenderFixtures.h"
#include <QThread>
#include <QElapsedTimer>
#include <QtTest>
#include <memory>

// Swift's EffectsPreviewCache: one worker, a delay, the newest request.
namespace {
QImage filled(int width, int height, const QColor &colour)
{
    QImage image = BrushRaster::context(width, height, false);
    image.fill(colour);
    return image;
}

LayerEffects outline(double size)
{
    LayerEffects effects;
    effects.stroke = StrokeEffect{.size = size, .red = 1, .green = 1, .blue = 0, .opacity = 1};
    return effects;
}

ImageLayer layerOf(const QImage &image, const LayerEffects &effects)
{
    ImageLayer layer(ImportedImage(image, QImage(), QStringLiteral("Layer")), QPointF(0, 0));
    layer.effects = effects;
    return layer;
}

// Counts landings, and outlives the cache that calls it.
struct Landings {
    std::shared_ptr<int> count = std::make_shared<int>(0);
    std::function<void()> callback() const
    {
        return [count = count] { ++*count; };
    }
};

std::optional<EffectsPreviewCache::Result> ask(EffectsPreviewCache &cache, const ImageLayer &layer, const Landings &landings,
                                               const std::optional<QImage> &mask = std::nullopt,
                                               const std::optional<LayerTransform> &placement = std::nullopt)
{
    return cache.preview(layer, mask, layer.transform, placement, landings.callback());
}

// The result once the worker has had its turn.
EffectsPreviewCache::Result landed(EffectsPreviewCache &cache, const ImageLayer &layer, const Landings &landings, int count)
{
    if (!QTest::qWaitFor([&] { return *landings.count == count; }, 5000))
        throw std::runtime_error("no preview landed");
    return cache.rendered(layer.id).value();
}
}

class EffectsPreviewCacheTests : public QObject {
    Q_OBJECT
private slots:
    void aPreviewLandsAfterItsDelayAsTheRendererDrawsIt();
    void movingTheLayerKeepsThePreviewUnlessAMaskIsPlaced();
    void settingsKeepTheLastPreviewButNewPixelsDropIt();
    void onlyTheNewestRequestLands();
    void aResultLandsOnlyOnTheRequestThatAskedForIt();
    void seedsStandInUntilTheWorkerCatchesUp();
    void preparingDropsLayersWithoutEffectsAndSharesTheBudget();
    void aMaskHidesPixelsInTheReducedPreview();
    void aReducedPreviewScalesEveryLength();
    void aClosedCacheLandsNothing();
    void undoneStepsReuseTheirEffects();
    void renderNowRendersAtThePreviewSize();
};

void EffectsPreviewCacheTests::aPreviewLandsAfterItsDelayAsTheRendererDrawsIt()
{
    EffectsPreviewCache cache;
    const QImage image = filled(20, 10, QColor(0, 0, 255));
    const ImageLayer layer = layerOf(image, outline(3));
    const Landings landings;
    QElapsedTimer clock;
    clock.start();
    QVERIFY(!ask(cache, layer, landings) && !cache.rendered(layer.id));
    const EffectsPreviewCache::Result result = landed(cache, layer, landings, 1);
    QVERIFY(clock.elapsed() >= 60);
    // Small enough to render whole: the export's own pixels.
    const LayerEffectsRenderer::Rendered expected = LayerEffectsRenderer::render(image, std::nullopt, outline(3));
    QVERIFY(result.image == expected.image && result.inset == 5 && expected.inset == 5 && !result.placement);
    // Asked again, the same pixels come back and nothing renders.
    QCOMPARE(ask(cache, layer, landings).value().image.cacheKey(), result.image.cacheKey());
    QTest::qWait(150);
    QCOMPARE(*landings.count, 1);
    // No effects, or none shown: nothing, and the entry goes.
    ImageLayer hidden = layer;
    hidden.effects.value().stroke.value().enabled = false;
    QVERIFY(!ask(cache, hidden, landings) && !cache.rendered(layer.id));
    QTest::qWait(150);
    QVERIFY(!cache.rendered(layer.id));
    // No pixels, or effects out of bounds: nothing either.
    ImageLayer blank(QStringLiteral("Blank"), QSizeF(20, 10));
    blank.effects = outline(3);
    QVERIFY(!ask(cache, blank, landings));
    QVERIFY(!ask(cache, layerOf(image, outline(600)), landings));
    QTest::qWait(150);
    QCOMPARE(*landings.count, 1);
}

void EffectsPreviewCacheTests::movingTheLayerKeepsThePreviewUnlessAMaskIsPlaced()
{
    EffectsPreviewCache cache;
    ImageLayer layer = layerOf(filled(20, 10, QColor(0, 0, 255)), outline(3));
    layer.mask = LayerMask(LayerMask::assetFrom(gray(20, 10, 255)));
    const QImage mask = layer.mask.value().enabledImage().value();
    const Landings landings;
    ask(cache, layer, landings, mask);
    const qint64 first = landed(cache, layer, landings, 1).image.cacheKey();
    // Effects render in layer pixels: a move only moves them.
    layer.transform.origin = QPointF(7, 3);
    QCOMPARE(ask(cache, layer, landings, mask).value().image.cacheKey(), first);
    QTest::qWait(150);
    QCOMPARE(*landings.count, 1);
    // A mask placed apart resamples, the last preview shown meanwhile.
    const LayerTransform placement = placedAt(QPointF(2, 2), QSizeF(20, 10));
    QCOMPARE(ask(cache, layer, landings, mask, placement).value().image.cacheKey(), first);
    const qint64 second = landed(cache, layer, landings, 2).image.cacheKey();
    layer.transform.origin = QPointF(9, 3);
    QCOMPARE(ask(cache, layer, landings, mask, placement).value().image.cacheKey(), second);
    QVERIFY(landed(cache, layer, landings, 3).image.cacheKey() != second);
    // Without a mask the placement changes nothing.
    ImageLayer bare = layerOf(filled(20, 10, QColor(0, 0, 255)), outline(3));
    ask(cache, bare, landings, std::nullopt, placement);
    const qint64 unmasked = landed(cache, bare, landings, 4).image.cacheKey();
    bare.transform.origin = QPointF(1, 1);
    QCOMPARE(ask(cache, bare, landings, std::nullopt, placedAt(QPointF(5, 5), QSizeF(20, 10))).value().image.cacheKey(), unmasked);
    QTest::qWait(150);
    QCOMPARE(*landings.count, 4);
}

void EffectsPreviewCacheTests::settingsKeepTheLastPreviewButNewPixelsDropIt()
{
    EffectsPreviewCache cache;
    ImageLayer layer = layerOf(filled(20, 10, QColor(0, 0, 255)), outline(3));
    const Landings landings;
    ask(cache, layer, landings);
    const qint64 first = landed(cache, layer, landings, 1).image.cacheKey();
    // Same kinds: the old preview shows while the new renders.
    layer.effects = outline(5);
    QCOMPARE(ask(cache, layer, landings).value().image.cacheKey(), first);
    const EffectsPreviewCache::Result second = landed(cache, layer, landings, 2);
    QCOMPARE(second.inset, 7.0);
    // Another kind shown or hidden: the last preview stands.
    layer.effects.value().colorOverlay = ColorOverlayEffect();
    QCOMPARE(ask(cache, layer, landings).value().image.cacheKey(), second.image.cacheKey());
    landed(cache, layer, landings, 3);
    // New pixels drop it.
    layer.asset = ImportedImage(filled(20, 10, QColor(255, 0, 0)), QImage(), QStringLiteral("Red"));
    QVERIFY(!ask(cache, layer, landings));
    const qint64 red = landed(cache, layer, landings, 4).image.cacheKey();
    // A mask added or removed on those pixels keeps it.
    layer.mask = LayerMask(LayerMask::assetFrom(gray(20, 10, 255)));
    QCOMPARE(ask(cache, layer, landings, layer.mask.value().enabledImage()).value().image.cacheKey(), red);
    QVERIFY(landed(cache, layer, landings, 5).image.cacheKey() != red);
    // Switched off again, the unmasked effects are known: no render.
    layer.mask.value().isEnabled = false;
    QCOMPARE(ask(cache, layer, landings).value().image.cacheKey(), red);
    QTest::qWait(200);
    QCOMPARE(*landings.count, 5);
}

void EffectsPreviewCacheTests::undoneStepsReuseTheirEffects()
{
    EffectsPreviewCache cache;
    ImageLayer layer = layerOf(filled(20, 10, QColor(0, 0, 255)), outline(3));
    const Landings landings;
    // Four pixels in turn, each rendered and landed.
    std::vector<ImportedImage> images;
    std::vector<qint64> keys;
    for (int step = 0; step < 4; ++step) {
        images.emplace_back(filled(20, 10, QColor(0, 0, 60 + 60 * step)), QImage(), QStringLiteral("Step"));
        layer.asset = images.back();
        QVERIFY(!ask(cache, layer, landings));
        keys.push_back(landed(cache, layer, landings, step + 1).image.cacheKey());
    }
    // Undone and redone across draws: the three newest are known.
    for (const int step : {2, 1, 3}) {
        cache.prepare({layer});
        layer.asset = images[size_t(step)];
        QCOMPARE(ask(cache, layer, landings).value().image.cacheKey(), keys[size_t(step)]);
    }
    QTest::qWait(200);
    QCOMPARE(*landings.count, 4);
    // Undone and redone across draws: three newest are known.
    cache.seed(layer.id, filled(4, 4, QColor(0, 255, 0)), layer.transform);
    layer.asset = images[2];
    QCOMPARE(ask(cache, layer, landings).value().image.cacheKey(), keys[2]);
    layer.asset = images[0];
    QVERIFY(!ask(cache, layer, landings));
    // A layer without effects forgets them.
    cache.prepare({});
    layer.asset = images[3];
    QVERIFY(!ask(cache, layer, landings));
}

void EffectsPreviewCacheTests::renderNowRendersAtThePreviewSize()
{
    EffectsPreviewCache cache;
    const QImage large = filled(400, 200, QColor(0, 0, 255));
    const LayerEffectsRenderer::Rendered whole = LayerEffectsRenderer::render(large, std::nullopt, outline(3));
    // Within the default 1536 a side: the whole render.
    const EffectsPreviewCache::Result now = cache.renderNow(large, std::nullopt, outline(3)).value();
    QCOMPARE(now.image, whole.image);
    QCOMPARE(now.inset, whole.inset);
    // Sharing the budget with a thousand, as the worker reduces.
    const ImageLayer layer = layerOf(large, outline(3));
    std::vector<ImageLayer> many(999, layerOf(filled(2, 2, QColor(0, 0, 255)), outline(3)));
    for (ImageLayer &each : many)
        each.id = QUuid::createUuid();
    many.push_back(layer);
    cache.prepare(many);
    const Landings landings;
    ask(cache, layer, landings);
    const EffectsPreviewCache::Result worker = landed(cache, layer, landings, 1);
    const EffectsPreviewCache::Result reduced = cache.renderNow(large, std::nullopt, outline(3)).value();
    QCOMPARE(reduced.image, worker.image);
    QCOMPARE(reduced.inset, worker.inset);
    // 129 a side: sides at 121 over the margined width.
    const double factor = 121.0 / (400 + 2 * LayerEffectsRenderer::margin(outline(3)));
    const LayerEffects scaledOutline = outline(3 * factor);
    QCOMPARE(reduced.image.size(), LayerEffectsRenderer::render(filled(int(std::lround(400 * factor)), int(std::lround(200 * factor)), QColor(0, 0, 255)),
                                                                std::nullopt, scaledOutline).image.size());
}

void EffectsPreviewCacheTests::onlyTheNewestRequestLands()
{
    EffectsPreviewCache cache;
    ImageLayer layer = layerOf(filled(20, 10, QColor(0, 0, 255)), outline(3));
    const Landings older, newer;
    ask(cache, layer, older);
    layer.effects = outline(6);
    ask(cache, layer, newer);
    QCOMPARE(landed(cache, layer, newer, 1).inset, 8.0);
    QTest::qWait(150);
    QCOMPARE(*older.count, 0);
}

void EffectsPreviewCacheTests::aResultLandsOnlyOnTheRequestThatAskedForIt()
{
    EffectsPreviewCache cache;
    ImageLayer layer = layerOf(filled(1500, 1500, QColor(0, 0, 255)), outline(40));
    const Landings older, newer;
    ask(cache, layer, older);
    // Its render finishes while the UI thread is busy elsewhere.
    QTest::qWait(70);
    QThread::msleep(2000);
    layer.effects = outline(6);
    ask(cache, layer, newer);
    QCOMPARE(landed(cache, layer, newer, 1).inset, 8.0);
    QCOMPARE(*older.count, 0);
    // A seed meanwhile leaves the late result nowhere to land.
    layer.effects = outline(9);
    const Landings late;
    ask(cache, layer, late);
    QTest::qWait(70);
    QThread::msleep(2000);
    cache.seed(layer.id, filled(4, 4, Qt::green), placedAt(QPointF(0, 0), QSizeF(4, 4)));
    QTest::qWait(150);
    QVERIFY(*late.count == 0 && cache.rendered(layer.id).value().placement);
}

void EffectsPreviewCacheTests::seedsStandInUntilTheWorkerCatchesUp()
{
    EffectsPreviewCache cache;
    const ImageLayer layer = layerOf(filled(20, 10, QColor(0, 0, 255)), outline(3));
    const Landings dropped, landings;
    ask(cache, layer, dropped);
    // A seed replaces the render under way, which never lands.
    const QImage warped = filled(30, 12, QColor(0, 255, 0));
    const LayerTransform placement = placedAt(QPointF(4, 5), QSizeF(30, 12));
    cache.seed(layer.id, warped, placement);
    EffectsPreviewCache::Result seeded = cache.rendered(layer.id).value();
    QVERIFY(seeded.image.cacheKey() == warped.cacheKey() && seeded.inset == 0 && seeded.placement == placement);
    // It stands in for the next request, then gives way.
    seeded = ask(cache, layer, landings).value();
    QVERIFY(seeded.image.cacheKey() == warped.cacheKey() && seeded.placement == placement);
    const EffectsPreviewCache::Result fresh = landed(cache, layer, landings, 1);
    QVERIFY(fresh.image.cacheKey() != warped.cacheKey() && !fresh.placement);
    QCOMPARE(*dropped.count, 0);
    // Gone for good: new pixels find neither.
    ImageLayer repainted = layer;
    repainted.asset = ImportedImage(filled(20, 10, QColor(255, 0, 0)), QImage(), QStringLiteral("Red"));
    QVERIFY(!ask(cache, repainted, landings));
}

void EffectsPreviewCacheTests::preparingDropsLayersWithoutEffectsAndSharesTheBudget()
{
    EffectsPreviewCache cache;
    const ImageLayer wide = layerOf(filled(6000, 100, QColor(0, 0, 255)), outline(4));
    const ImageLayer small = layerOf(filled(20, 10, QColor(0, 0, 255)), outline(3));
    const Landings landings;
    cache.prepare({wide, small});
    ask(cache, wide, landings);
    ask(cache, small, landings);
    QVERIFY(QTest::qWaitFor([&] { return *landings.count == 2; }, 5000));
    // Two layers with effects: 1536 a side, margins included.
    EffectsPreviewCache::Result result = cache.rendered(wide.id).value();
    QVERIFY(result.image.size() == QSize(1533, 33) && result.inset == 4);
    // 1024 layers share 128 a side; twenty thousand get 32.
    std::vector<ImageLayer> many(1024, wide);
    for (ImageLayer &layer : many)
        layer.id = QUuid::createUuid();
    many.front() = wide;
    cache.prepare(many);
    ask(cache, wide, landings);
    result = landed(cache, wide, landings, 3);
    QVERIFY(result.image.size() == QSize(126, 8) && result.inset == 3);
    many.resize(20000, many.back());
    for (ImageLayer &layer : many)
        layer.id = QUuid::createUuid();
    many.front() = wide;
    cache.prepare(many);
    ask(cache, wide, landings);
    result = landed(cache, wide, landings, 4);
    QVERIFY(result.image.size() == QSize(30, 7) && result.inset == 3);
    // Layers without effects shown go, seeds too; none leaves 1536.
    ask(cache, small, landings);
    landed(cache, small, landings, 5);
    ImageLayer off = small;
    off.effects.value().stroke.value().enabled = false;
    cache.seed(wide.id, filled(4, 4, Qt::green), placedAt(QPointF(0, 0), QSizeF(4, 4)));
    cache.prepare({off});
    QVERIFY(!cache.rendered(small.id) && !cache.rendered(wide.id));
    ask(cache, wide, landings);
    QCOMPARE(landed(cache, wide, landings, 6).image.size(), QSize(1533, 33));
}

void EffectsPreviewCacheTests::aMaskHidesPixelsInTheReducedPreview()
{
    EffectsPreviewCache cache;
    ImageLayer layer = layerOf(filled(3000, 100, QColor(0, 0, 255)), outline(4));
    // Left half hidden: the preview shows the right half.
    QImage mask = gray(3000, 100, 255);
    for (int y = 0; y < 100; ++y)
        std::fill(mask.scanLine(y), mask.scanLine(y) + 1500, uchar(0));
    const Landings landings;
    cache.prepare({layer});
    ask(cache, layer, landings, mask);
    const QImage image = landed(cache, layer, landings, 1).image;
    QVERIFY(image.size() == QSize(1532, 61));
    QCOMPARE(qAlpha(image.pixel(300, 30)), 0);
    QCOMPARE(image.pixel(1200, 30), qRgba(0, 0, 255, 255));
    // The outline runs round what shows, at the halfway line.
    QCOMPARE(image.pixel(764, 30), qRgba(255, 255, 0, 255));
    // A mask of another size is reduced by its own.
    QImage coarse(2, 1, QImage::Format_Grayscale8);
    coarse.scanLine(0)[0] = 0;
    coarse.scanLine(0)[1] = 255;
    layer.asset = ImportedImage(filled(6000, 100, QColor(0, 0, 255)), QImage(), QStringLiteral("Wide"));
    ask(cache, layer, landings, coarse);
    const QImage wide = landed(cache, layer, landings, 2).image;
    QVERIFY(wide.size() == QSize(1533, 33));
    QCOMPARE(qAlpha(wide.pixel(104, 16)), 0);
    QCOMPARE(wide.pixel(1404, 16), qRgba(0, 0, 255, 255));
    // Stripes a row high shrink across and grow down, unaliased.
    QImage stripes(6000, 1, QImage::Format_Grayscale8);
    for (int x = 0; x < 6000; ++x)
        stripes.scanLine(0)[x] = uchar(x % 2 ? 255 : 0);
    layer.mask = LayerMask(LayerMask::assetFrom(stripes));
    ask(cache, layer, landings, stripes);
    const QImage row = landed(cache, layer, landings, 3).image;
    int low = 255, high = 0;
    for (int x = 10; x < 1523; ++x) {
        low = std::min(low, qAlpha(row.pixel(x, 16)));
        high = std::max(high, qAlpha(row.pixel(x, 16)));
    }
    QVERIFY(low == 125 && high == 129);
    layer.mask = LayerMask(LayerMask::assetFrom(stripes.scaled(6000, 100)));
    ask(cache, layer, landings, layer.mask.value().enabledImage());
    QVERIFY(landed(cache, layer, landings, 4).image == row);
}

void EffectsPreviewCacheTests::aReducedPreviewScalesEveryLength()
{
    EffectsPreviewCache cache;
    LayerEffects effects;
    effects.stroke = StrokeEffect{.size = 4, .red = 1, .green = 1, .blue = 0, .opacity = 1};
    effects.shadow = ShadowEffect{.angle = 45, .distance = 8, .blur = 3, .red = 1, .green = 0, .blue = 0, .opacity = 0.8};
    effects.colorOverlay = ColorOverlayEffect{.red = 0, .green = 1, .blue = 0, .opacity = 0.5};
    effects.innerShadow = InnerShadowEffect{.angle = 120, .distance = 30, .blur = 12, .red = 0, .green = 0, .blue = 0, .opacity = 0.7};
    effects.outerGlow = OuterGlowEffect{.size = 5, .red = 0, .green = 0, .blue = 1, .opacity = 0.6};
    effects.innerGlow = InnerGlowEffect{.size = 40, .red = 1, .green = 0, .blue = 1, .opacity = 0.8};
    const QImage image = noise(6000, 100, 7);
    const ImageLayer layer = layerOf(image, effects);
    const Landings landings;
    cache.prepare({layer});
    ask(cache, layer, landings);
    const EffectsPreviewCache::Result result = landed(cache, layer, landings, 1);
    // The renderer on the reduced pixels, every length reduced alike.
    const double factor = 1528.0 / (6000 + 2 * LayerEffectsRenderer::margin(effects));
    const QImage reduced = image.scaled(1518, 25, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    effects.stroke.value().size *= factor;
    effects.shadow.value().distance *= factor;
    effects.shadow.value().blur *= factor;
    effects.innerShadow.value().distance *= factor;
    effects.innerShadow.value().blur *= factor;
    effects.outerGlow.value().size *= factor;
    effects.innerGlow.value().size *= factor;
    QVERIFY(result.image == LayerEffectsRenderer::render(reduced, std::nullopt, effects).image);
}

void EffectsPreviewCacheTests::aClosedCacheLandsNothing()
{
    const Landings landings;
    {
        EffectsPreviewCache cache;
        ask(cache, layerOf(filled(1500, 1500, QColor(0, 0, 255)), outline(40)), landings);
        QTest::qWait(70);
    }
    QTest::qWait(150);
    QCOMPARE(*landings.count, 0);
}

QTEST_GUILESS_MAIN(EffectsPreviewCacheTests)
#include "EffectsPreviewCacheTests.moc"
