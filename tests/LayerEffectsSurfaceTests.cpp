#include "Document/LayerEffects+Renderer.h"
#include "Rendering/LayerEffectsSurface.h"
#include "RenderFixtures.h"
#include <QPainter>
#include <QtTest>

// Swift's LayerEffectsSurface: a painted layer's effects, kept up.
namespace {
LayerEffects everyKind()
{
    LayerEffects effects;
    effects.stroke = StrokeEffect{.size = 5, .red = 1, .green = 1, .blue = 0, .opacity = 1};
    effects.shadow = ShadowEffect{.angle = 45, .distance = 4, .blur = 16, .red = 1, .green = 0, .blue = 0, .opacity = 0.8};
    effects.colorOverlay = ColorOverlayEffect{.red = 0, .green = 1, .blue = 0, .opacity = 0.5};
    effects.innerShadow = InnerShadowEffect{.angle = 120, .distance = 3, .blur = 60, .red = 0, .green = 0, .blue = 0, .opacity = 0.7};
    return effects;
}

void addRows()
{
    QTest::addColumn<LayerEffects>("effects");
    const LayerEffects all = everyKind();
    LayerEffects outside, inside, cast, overlay, inner, glow;
    outside.stroke = all.stroke;
    inside.stroke = StrokeEffect{.size = 10, .red = 1, .green = 0, .blue = 1, .opacity = 0.9, .inside = true};
    cast.shadow = all.shadow;
    overlay.colorOverlay = all.colorOverlay;
    inner.innerShadow = all.innerShadow;
    glow.outerGlow = OuterGlowEffect{.size = 12, .red = 0, .green = 1, .blue = 1, .opacity = 0.9};
    QTest::newRow("outside stroke") << outside;
    QTest::newRow("inside stroke") << inside;
    QTest::newRow("shadow") << cast;
    QTest::newRow("overlay") << overlay;
    QTest::newRow("inner shadow") << inner;
    QTest::newRow("outer glow") << glow;
    QTest::newRow("every kind") << all;
}

// A soft disc well inside a clear layer.
QImage disc()
{
    QImage image = BrushRaster::context(80, 60, false);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(40, 90, 200));
    painter.drawEllipse(QPointF(40, 30), 15.5, 13);
    return image;
}

// Opaque and translucent cells: a window cut short shows.
QImage checker(int width, int height)
{
    QImage image = BrushRaster::context(width, height, false);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int alpha = (x / 8 + y / 8) % 2 ? 255 : 96;
            image.setPixel(x, y, qRgba(40 * alpha / 255, 90 * alpha / 255, 200 * alpha / 255, alpha));
        }
    }
    return image;
}

// A tile of `base` with a clear hole in it.
BrushPatch holed(const QImage &base, const QRect &tile, const QRect &hole)
{
    QImage image = base.copy(tile);
    QPainter painter(&image);
    painter.setCompositionMode(QPainter::CompositionMode_Clear);
    painter.fillRect(hole.translated(-tile.topLeft()), Qt::transparent);
    return {QRectF(tile), image};
}

std::unique_ptr<LayerEffectsSurface> surfaceFor(const QImage &image, const LayerEffects &effects)
{
    return LayerEffectsSurface::make(QUuid::createUuid(), effects, QSizeF(image.size()), QRectF(image.rect()));
}

ImportedImage asset(const QImage &image)
{
    return ImportedImage(image, QImage(), QStringLiteral("Layer"));
}
}

class LayerEffectsSurfaceTests : public QObject {
    Q_OBJECT
private slots:
    void theFirstUpdateDrawsWhatTheRendererDraws_data() { addRows(); }
    void theFirstUpdateDrawsWhatTheRendererDraws();
    void newPaintIsRedoneAsFarAsItReaches_data() { addRows(); }
    void newPaintIsRedoneAsFarAsItReaches();
    void tilesAlreadyTakenRedoNothing();
    void paintPastTheLayerShowsUnmasked();
    void aSmallerMaskScalesSmoothlyAsTheRenderersDoes();
    void aSurfaceHoldsEightyMillionPixelsAtMost();
    void aSurfaceFitsTheStrokeItWasMadeFor();
};

void LayerEffectsSurfaceTests::theFirstUpdateDrawsWhatTheRendererDraws()
{
    QFETCH(LayerEffects, effects);
    const QImage image = disc();
    const std::unique_ptr<LayerEffectsSurface> surface = surfaceFor(image, effects);
    QVERIFY(surface && !surface->image());
    surface->update(asset(image), {}, std::nullopt);
    const LayerEffectsRenderer::Rendered expected = LayerEffectsRenderer::render(image, std::nullopt, effects);
    QCOMPARE(surface->margin, expected.inset);
    QVERIFY(surface->image().value() == expected.image);
    // A painted base draws from its tiles, alike, never flattened.
    const std::unique_ptr<LayerEffectsSurface> tiled = surfaceFor(image, effects);
    const std::shared_ptr<const RasterSnapshot> raster = painted(image, {});
    tiled->update(ImportedImage(raster, QImage(), QStringLiteral("Painted")), {}, std::nullopt);
    QVERIFY(tiled->image().value() == expected.image && !raster->hasMaterializedPixels());
}

void LayerEffectsSurfaceTests::newPaintIsRedoneAsFarAsItReaches()
{
    QFETCH(LayerEffects, effects);
    // Two tiles painted at once, each with a hole.
    const QImage base = checker(900, 900);
    const std::vector<BrushPatch> patches{holed(base, QRect(0, 0, 256, 256), QRect(200, 180, 56, 76)),
                                          holed(base, QRect(256, 0, 256, 256), QRect(266, 40, 20, 30))};
    const std::unique_ptr<LayerEffectsSurface> surface = surfaceFor(base, effects);
    surface->update(asset(base), {}, std::nullopt);
    surface->update(asset(base), patches, std::nullopt);
    // Everything the hole reaches, past its tile too, is redone.
    QVERIFY(surface->image().value() == LayerEffectsRenderer::render(composite(base, patches), std::nullopt, effects).image);
}

void LayerEffectsSurfaceTests::tilesAlreadyTakenRedoNothing()
{
    const QImage base = solid(300, 300, qRgba(40, 90, 200, 255));
    const std::unique_ptr<LayerEffectsSurface> surface = surfaceFor(base, everyKind());
    const BrushPatch red{QRectF(0, 0, 256, 256), solid(256, 256, qRgba(255, 0, 0, 255))};
    surface->update(asset(base), {red}, std::nullopt);
    const qint64 first = surface->image().value().cacheKey();
    surface->update(asset(base), {red}, std::nullopt);
    QCOMPARE(surface->image().value().cacheKey(), first);
    // A tile published anew is taken in again.
    const BrushPatch green{red.rect, solid(256, 256, qRgba(0, 255, 0, 255))};
    surface->update(asset(base), {green}, std::nullopt);
    QVERIFY(surface->image().value() == LayerEffectsRenderer::render(composite(base, {green}), std::nullopt, everyKind()).image);
}

void LayerEffectsSurfaceTests::paintPastTheLayerShowsUnmasked()
{
    // The layer, wholly masked, sits inside a larger grid.
    LayerEffects effects;
    effects.colorOverlay = ColorOverlayEffect{.red = 0, .green = 1, .blue = 0, .opacity = 1};
    const std::unique_ptr<LayerEffectsSurface> surface = LayerEffectsSurface::make(QUuid::createUuid(), effects, QSizeF(40, 30), QRectF(10, 10, 20, 10));
    const BrushPatch paint{QRectF(0, 0, 40, 30), solid(40, 30, qRgba(255, 0, 0, 255))};
    surface->update(asset(solid(20, 10, qRgba(0, 0, 255, 255))), {paint}, gray(20, 10, 0));
    const QImage image = surface->image().value();
    QVERIFY(surface->margin == 2 && image.size() == QSize(44, 34));
    QCOMPARE(image.pixel(4, 4), qRgba(0, 255, 0, 255));
    QCOMPARE(image.pixel(29, 29), qRgba(0, 255, 0, 255));
    QCOMPARE(qAlpha(image.pixel(12, 12)), 0);
    QCOMPARE(qAlpha(image.pixel(31, 21)), 0);
}

void LayerEffectsSurfaceTests::aSmallerMaskScalesSmoothlyAsTheRenderersDoes()
{
    const QImage image = solid(64, 16, qRgba(40, 90, 200, 255));
    QImage mask(2, 1, QImage::Format_Grayscale8);
    mask.scanLine(0)[0] = 0;
    mask.scanLine(0)[1] = 255;
    const std::unique_ptr<LayerEffectsSurface> surface = surfaceFor(image, everyKind());
    surface->update(asset(image), {}, mask);
    QVERIFY(surface->image().value() == LayerEffectsRenderer::render(image, mask, everyKind()).image);
}

void LayerEffectsSurfaceTests::aSurfaceHoldsEightyMillionPixelsAtMost()
{
    LayerEffects effects;
    effects.colorOverlay = ColorOverlayEffect();
    // A margin of two: 8,000 by 10,000 fits exactly.
    QVERIFY(LayerEffectsSurface::make(QUuid::createUuid(), effects, QSizeF(7996, 9996), QRectF(0, 0, 7996, 9996)));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("an effects surface passes its pixel budget"));
    QVERIFY(!LayerEffectsSurface::make(QUuid::createUuid(), effects, QSizeF(7997, 9996), QRectF(0, 0, 7997, 9996)));
}

void LayerEffectsSurfaceTests::aSurfaceFitsTheStrokeItWasMadeFor()
{
    const QUuid id = QUuid::createUuid();
    const std::unique_ptr<LayerEffectsSurface> surface = LayerEffectsSurface::make(id, everyKind(), QSizeF(40, 30), QRectF(5, 5, 20, 10));
    QVERIFY(surface->matches(id, everyKind(), QSizeF(40, 30), QRectF(5, 5, 20, 10)));
    QVERIFY(!surface->matches(QUuid::createUuid(), everyKind(), QSizeF(40, 30), QRectF(5, 5, 20, 10)));
    QVERIFY(!surface->matches(id, LayerEffects(), QSizeF(40, 30), QRectF(5, 5, 20, 10)));
    QVERIFY(!surface->matches(id, everyKind(), QSizeF(41, 30), QRectF(5, 5, 20, 10)));
    QVERIFY(!surface->matches(id, everyKind(), QSizeF(40, 30), QRectF(6, 5, 20, 10)));
}

QTEST_GUILESS_MAIN(LayerEffectsSurfaceTests)
#include "LayerEffectsSurfaceTests.moc"
