#include "Document/BrushStroke.h"
#include "IO/ImageExporter.h"
#include "Document/LayerEffects+Renderer.h"
#include "Rendering/LayerEffectsKernel.h"
#include "AddressSpaceLimit.h"
#include <QPainter>
#include <QtTest>
#include <cmath>

// The CPU kernel against Metal's shader, replayed loop for loop.
namespace {
// Swift's MetalLayerEffects source, one kernel a function, naively.
struct Shader {
    int width, height;
    std::vector<float> shape;
    float at(const std::vector<float> &values, int x, int y) const { return values[size_t(y * width + x)]; }

    std::vector<float> spread(const std::vector<float> &source, int reach, bool smallest, bool rows) const
    {
        std::vector<float> result(source.size());
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float best = smallest ? 1.f : 0.f;
                for (int offset = -reach; offset <= reach; ++offset) {
                    const int sample = (rows ? x : y) + offset;
                    const bool outside = sample < 0 || sample >= (rows ? width : height);
                    const float value = outside ? 0.f : rows ? at(source, sample, y) : at(source, x, sample);
                    best = smallest ? std::min(best, value) : std::max(best, value);
                }
                result[size_t(y * width + x)] = best;
            }
        }
        return result;
    }

    std::vector<float> shift(const std::vector<float> &source, float dx, float dy) const
    {
        std::vector<float> result(source.size());
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const float sx = float(x) - dx, sy = float(y) - dy;
                float value = 0.f;
                if (sx >= 0.f && sy >= 0.f && sx <= float(width - 1) && sy <= float(height - 1)) {
                    const int x0 = int(std::floor(sx)), y0 = int(std::floor(sy));
                    const int x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
                    const float fx = sx - float(x0), fy = sy - float(y0);
                    const float top = at(source, x0, y0) + (at(source, x1, y0) - at(source, x0, y0)) * fx;
                    const float bottom = at(source, x0, y1) + (at(source, x1, y1) - at(source, x0, y1)) * fx;
                    value = top + (bottom - top) * fy;
                }
                result[size_t(y * width + x)] = value;
            }
        }
        return result;
    }

    std::vector<float> blur(const std::vector<float> &source, float sigma, bool rows) const
    {
        const int radius = std::max(1, int(std::lround(sigma * 3)));
        std::vector<float> result(source.size());
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float total = 0.f, weightSum = 0.f;
                for (int offset = -radius; offset <= radius; ++offset) {
                    const float weight = std::exp(-float(offset * offset) / (2.f * sigma * sigma));
                    const int sample = std::clamp((rows ? x : y) + offset, 0, (rows ? width : height) - 1);
                    total += weight * (rows ? at(source, sample, y) : at(source, x, sample));
                    weightSum += weight;
                }
                result[size_t(y * width + x)] = total / weightSum;
            }
        }
        return result;
    }

    std::vector<float> softened(const std::vector<float> &source, float sigma) const
    {
        return sigma > 0.01f ? blur(blur(source, sigma, true), sigma, false) : source;
    }
};

QImage replayed(const QImage &pixels, const LayerEffects &effects)
{
    Shader shader{pixels.width(), pixels.height(), {}};
    for (int y = 0; y < pixels.height(); ++y) {
        for (int x = 0; x < pixels.width(); ++x)
            shader.shape.push_back(float(pixels.constScanLine(y)[x * 4 + 3]) / 255.f);
    }
    const auto on = [](const auto &effect) { return effect && effect->isEnabled() && effect->opacity > 0; };
    const bool stroke = on(effects.stroke) && effects.stroke->size > 0;
    std::vector<float> ring(shader.shape.size()), cast(shader.shape.size()), inner(shader.shape.size()), glow(shader.shape.size()),
        innerGlow(shader.shape.size());
    if (stroke) {
        const int reach = std::max(1, int(std::lround(effects.stroke->size)));
        const std::vector<float> moved = shader.spread(shader.spread(shader.shape, reach, effects.stroke->inside, true), reach, effects.stroke->inside, false);
        for (size_t index = 0; index < ring.size(); ++index)
            ring[index] = std::clamp(effects.stroke->inside ? shader.shape[index] - moved[index] : moved[index] - shader.shape[index], 0.f, 1.f);
    }
    if (on(effects.shadow))
        cast = shader.softened(shader.shift(shader.shape, float(effects.shadow->offset().width()), float(effects.shadow->offset().height())),
                               float(effects.shadow->blur / 2));
    const bool glowing = on(effects.outerGlow) && effects.outerGlow->size > 0;
    if (glowing)
        glow = shader.softened(shader.shape, float(effects.outerGlow->size / 2));
    const bool glowingInside = on(effects.innerGlow) && effects.innerGlow->size > 0;
    if (glowingInside) {
        const std::vector<float> softened = shader.softened(shader.shape, float(effects.innerGlow->size / 2));
        for (size_t index = 0; index < innerGlow.size(); ++index)
            innerGlow[index] = std::clamp(shader.shape[index] * (1.f - softened[index]), 0.f, 1.f);
    }
    if (on(effects.innerShadow)) {
        const std::vector<float> moved = shader.softened(shader.shift(shader.shape, float(effects.innerShadow->offset().width()),
                                                                      float(effects.innerShadow->offset().height())),
                                                         float(effects.innerShadow->blur / 2));
        for (size_t index = 0; index < inner.size(); ++index)
            inner[index] = std::clamp(shader.shape[index] * (1.f - moved[index]), 0.f, 1.f);
    }
    QImage result(pixels.size(), QImage::Format_RGBA8888_Premultiplied);
    for (int y = 0; y < pixels.height(); ++y) {
        for (int x = 0; x < pixels.width(); ++x) {
            const size_t index = size_t(y * pixels.width() + x);
            float colour[3] = {0, 0, 0}, alpha = 0;
            const auto blend = [&](const PaletteColor &paint, float coverage) {
                const float mix[3] = {float(paint.red), float(paint.green), float(paint.blue)};
                for (int channel = 0; channel < 3; ++channel)
                    colour[channel] = mix[channel] * coverage + colour[channel] * (1.f - coverage);
                alpha = coverage + alpha * (1.f - coverage);
            };
            if (on(effects.shadow))
                blend(effects.shadow->color(), std::clamp(cast[index] * float(effects.shadow->opacity), 0.f, 1.f));
            if (glowing)
                blend(effects.outerGlow->color(), std::clamp(glow[index] * (1.f - shader.shape[index]) * float(effects.outerGlow->opacity), 0.f, 1.f));
            const float strokeCoverage = stroke ? std::clamp(ring[index] * float(effects.stroke->opacity), 0.f, 1.f) : 0.f;
            if (stroke && !effects.stroke->inside)
                blend(effects.stroke->color(), strokeCoverage);
            const uchar *source = pixels.constScanLine(y) + x * 4;
            const float sourceAlpha = float(source[3]) / 255.f;
            for (int channel = 0; channel < 3; ++channel)
                colour[channel] = float(source[channel]) / 255.f + colour[channel] * (1.f - sourceAlpha);
            alpha = sourceAlpha + alpha * (1.f - sourceAlpha);
            if (on(effects.colorOverlay))
                blend(effects.colorOverlay->color(), std::clamp(shader.shape[index] * float(effects.colorOverlay->opacity), 0.f, 1.f));
            if (glowingInside)
                blend(effects.innerGlow->color(), std::clamp(innerGlow[index] * float(effects.innerGlow->opacity), 0.f, 1.f));
            if (on(effects.innerShadow))
                blend(effects.innerShadow->color(), std::clamp(inner[index] * float(effects.innerShadow->opacity), 0.f, 1.f));
            if (stroke && effects.stroke->inside)
                blend(effects.stroke->color(), strokeCoverage);
            uchar *out = result.scanLine(y) + x * 4;
            for (int channel = 0; channel < 3; ++channel)
                out[channel] = uchar(std::clamp(colour[channel], 0.f, 1.f) * 255.f + 0.5f);
            out[3] = uchar(std::clamp(alpha, 0.f, 1.f) * 255.f + 0.5f);
        }
    }
    return result;
}

// A soft disc and a hard bar touching the edge.
QImage figure(int width, int height)
{
    QImage image = BrushRaster::context(width, height, false);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(200, 40, 90));
    painter.drawEllipse(QRectF(width * 0.2, height * 0.2, width * 0.45, height * 0.5));
    painter.fillRect(QRectF(width * 0.7, 0, width * 0.15, height * 0.6), QColor(20, 180, 60, 160));
    // Wide shapes on every edge, alpha ramping to it.
    painter.end();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool bottom = y >= height - 5 && x < 14, right = x >= width - 6 && y >= 8 && y < 22, left = x < 6 && y >= 10 && y < 24;
            const int alpha = bottom ? 255 - (height - 1 - y) * 30 : right ? 255 - (width - 1 - x) * 25 : left ? 250 - x * 30 : 0;
            if (alpha > 0)
                std::fill_n(image.scanLine(y) + x * 4, 4, uchar(alpha));
        }
    }
    return image;
}

int gap(const QImage &lhs, const QImage &rhs)
{
    int worst = 0;
    for (int y = 0; y < lhs.height(); ++y) {
        for (int x = 0; x < lhs.width() * 4; ++x)
            worst = std::max(worst, std::abs(int(lhs.constScanLine(y)[x]) - int(rhs.constScanLine(y)[x])));
    }
    return worst;
}
}

class LayerEffectsKernelTests : public QObject {
    Q_OBJECT
private slots:
    void everyEffectMatchesTheShader_data();
    void everyEffectMatchesTheShader();
    void switchedOffOrEmptyEffectsLeaveThePixels();
    void theKernelRefusesWhatItCannotHold();
    void aStarvedKernelThrowsWhereItsCallerCatches();
    void thinLayersAndTallOnesAreCheap();
};

void LayerEffectsKernelTests::everyEffectMatchesTheShader_data()
{
    QTest::addColumn<LayerEffects>("effects");
    LayerEffects outside, inside, shadow, overlay, inner, glow, hair, all, wide, innerGlow, innerHair;
    outside.stroke = StrokeEffect{.size = 3, .red = 1, .green = 0.5, .blue = 0.25, .opacity = 0.8};
    inside.stroke = StrokeEffect{.size = 2.5, .red = 0.1, .green = 0.2, .blue = 0.9, .opacity = 1, .inside = true};
    shadow.shadow = ShadowEffect{.angle = 45, .distance = 3.5, .blur = 4, .red = 0.3, .green = 0.1, .blue = 0.6, .opacity = 0.75};
    overlay.colorOverlay = ColorOverlayEffect{.red = 0.9, .green = 0.8, .blue = 0.1, .opacity = 0.5};
    inner.innerShadow = InnerShadowEffect{.angle = -60, .distance = 2, .blur = 3, .red = 0, .green = 0.4, .blue = 0.2, .opacity = 0.9};
    glow.outerGlow = OuterGlowEffect{.size = 6, .red = 0.2, .green = 0.9, .blue = 0.4, .opacity = 0.8};
    hair.outerGlow = OuterGlowEffect{.size = 0.01, .opacity = 1};
    innerGlow.innerGlow = InnerGlowEffect{.size = 7, .red = 0.8, .green = 0.3, .blue = 0.1, .opacity = 0.7};
    innerHair.innerGlow = InnerGlowEffect{.size = 0.01, .red = 0, .opacity = 1};
    all = outside;
    all.shadow = shadow.shadow;
    all.colorOverlay = overlay.colorOverlay;
    all.innerShadow = inner.innerShadow;
    all.outerGlow = glow.outerGlow;
    all.innerGlow = innerGlow.innerGlow;
    all.shadow->blur = 0;
    wide.stroke = StrokeEffect{.size = 9, .red = 1, .opacity = 1};
    wide.shadow = ShadowEffect{.angle = 200, .distance = 0.5, .blur = 0.01, .opacity = 1};
    QTest::newRow("outside stroke") << outside;
    QTest::newRow("inside stroke") << inside;
    QTest::newRow("drop shadow") << shadow;
    QTest::newRow("colour overlay") << overlay;
    QTest::newRow("inner shadow") << inner;
    QTest::newRow("outer glow") << glow;
    QTest::newRow("a glow a hair wide") << hair;
    QTest::newRow("inner glow") << innerGlow;
    QTest::newRow("an inner glow a hair wide") << innerHair;
    QTest::newRow("all, a sharp shadow") << all;
    QTest::newRow("wide stroke, a barely blurred shadow") << wide;
}

void LayerEffectsKernelTests::everyEffectMatchesTheShader()
{
    QFETCH(LayerEffects, effects);
    const QImage pixels = figure(48, 34);
    const QImage made = LayerEffectsKernel::render(pixels, effects);
    QCOMPARE(made.format(), QImage::Format_RGBA8888_Premultiplied);
    QCOMPARE(made.size(), pixels.size());
    const QImage expected = replayed(pixels, effects);
    QVERIFY2(gap(made, expected) == 0, qPrintable(QString::number(gap(made, expected))));
    QVERIFY(made != pixels);
    // Another format reads as premultiplied RGBA.
    QVERIFY(gap(LayerEffectsKernel::render(pixels.convertToFormat(QImage::Format_ARGB32_Premultiplied), effects), expected) == 0);
}

void LayerEffectsKernelTests::switchedOffOrEmptyEffectsLeaveThePixels()
{
    const QImage pixels = figure(30, 20);
    LayerEffects hidden;
    hidden.stroke = StrokeEffect{.enabled = false, .size = 3, .red = 1};
    hidden.shadow = ShadowEffect{.enabled = false, .distance = 2, .blur = 0, .opacity = 1};
    hidden.colorOverlay = ColorOverlayEffect{.enabled = false, .red = 1};
    hidden.innerShadow = InnerShadowEffect{.enabled = false};
    hidden.outerGlow = OuterGlowEffect{.enabled = false};
    hidden.innerGlow = InnerGlowEffect{.enabled = false};
    LayerEffects dim, flat, dimInside, flatInside;
    dim.outerGlow = OuterGlowEffect{.opacity = 0};
    flat.outerGlow = OuterGlowEffect{.size = 0};
    dimInside.innerGlow = InnerGlowEffect{.opacity = 0};
    flatInside.innerGlow = InnerGlowEffect{.size = 0};
    LayerEffects thin;
    thin.stroke = StrokeEffect{.size = 0, .red = 1};
    LayerEffects clear;
    clear.shadow = ShadowEffect{.opacity = 0};
    for (const LayerEffects &effects : {LayerEffects(), hidden, thin, clear, dim, flat, dimInside, flatInside})
        QCOMPARE(gap(LayerEffectsKernel::render(pixels, effects), pixels), 0);
    // Stroke sizes round to whole pixels, one at least.
    LayerEffects fraction, one;
    fraction.stroke = StrokeEffect{.size = 0.4, .red = 1};
    one.stroke = StrokeEffect{.size = 1, .red = 1};
    QCOMPARE(gap(LayerEffectsKernel::render(pixels, fraction), LayerEffectsKernel::render(pixels, one)), 0);
    QVERIFY(gap(LayerEffectsKernel::render(pixels, one), pixels) > 100);
}

void LayerEffectsKernelTests::theKernelRefusesWhatItCannotHold()
{
    // Past one surface's pixels, before any pixel is read.
    const QImage vast(20'001, 10'000, QImage::Format_Mono);
    LayerEffects effects;
    effects.stroke = StrokeEffect();
    try {
        LayerEffectsKernel::render(vast, effects);
        QFAIL("the kernel took a vast image");
    } catch (const ExportError &error) {
        QCOMPARE(error.kind, ExportError::Kind::tooLarge);
    }
    try {
        LayerEffectsKernel::render(QImage(), effects);
        QFAIL("the kernel took an empty image");
    } catch (const ExportError &error) {
        QCOMPARE(error.kind, ExportError::Kind::tooLarge);
    }
}

void LayerEffectsKernelTests::aStarvedKernelThrowsWhereItsCallerCatches()
{
    const QImage pixels = BrushRaster::context(4'000, 4'000, false);
    LayerEffects effects;
    effects.stroke = StrokeEffect();
    effects.shadow = ShadowEffect();
    effects.innerShadow = InnerShadowEffect();
    std::optional<AddressSpaceLimit> limit(std::in_place, 16 * 1024 * 1024);
    std::optional<ExportError::Kind> failure;
    try {
        LayerEffectsKernel::render(pixels, effects);
    } catch (const ExportError &error) {
        failure = error.kind;
    }
    // The renderer's cache draws the layer bare instead.
    const bool drawn = LayerEffectsRenderer::cached(pixels, std::nullopt, effects).has_value();
    // Past Metal's 80 million the CPU still takes it.
    std::optional<ExportError::Kind> large;
    try {
        LayerEffectsKernel::render(QImage(9'000, 10'000, QImage::Format_Mono), effects);
    } catch (const ExportError &error) {
        large = error.kind;
    }
    limit.reset();
    QCOMPARE(failure, std::optional(ExportError::Kind::render));
    QVERIFY(!drawn && large == ExportError::Kind::render);
}

void LayerEffectsKernelTests::thinLayersAndTallOnesAreCheap()
{
    LayerEffects effects;
    effects.stroke = StrokeEffect{.size = 2, .red = 1};
    // Queues sized by each pass: thin layers ask little.
    const QImage thin = BrushRaster::context(30'000, 10, false);
    LayerEffectsKernel::render(thin, effects);
    std::optional<AddressSpaceLimit> limit(std::in_place, 64 * 1024 * 1024);
    bool made = true;
    try {
        LayerEffectsKernel::render(thin, effects);
    } catch (const ExportError &) {
        made = false;
    }
    limit.reset();
    QVERIFY(made);
    // Workers write through one pointer: no row detaches the image.
    const QImage tall = LayerEffectsKernel::render(BrushRaster::context(8, 4'000, false), effects);
    QVERIFY((tall.cacheKey() & 0xffffffff) < 8);
}

QTEST_GUILESS_MAIN(LayerEffectsKernelTests)
#include "LayerEffectsKernelTests.moc"
