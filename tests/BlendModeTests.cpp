#include "IO/ImageExporter.h"
#include "Rendering/HslBlend.h"
#include "Rendering/LayerRenderer.h"
#include "RenderFixtures.h"
#include <QPixmap>
#include <QRandomGenerator>
#include "AddressSpaceLimit.h"
#include <QtTest>
#include <algorithm>
#include <array>

namespace {
using Rgb = std::array<double, 3>;





double lum(const Rgb &color)
{
    return 0.3 * color[0] + 0.59 * color[1] + 0.11 * color[2];
}

Rgb clipColor(Rgb color)
{
    const double l = lum(color), low = *std::min_element(color.begin(), color.end()),
                 high = *std::max_element(color.begin(), color.end());
    for (double &channel : color) {
        if (low < 0)
            channel = l + (channel - l) * l / (l - low);
        if (high > 1)
            channel = l + (channel - l) * (1 - l) / (high - l);
    }
    return color;
}

Rgb setLum(Rgb color, double l)
{
    const double shift = l - lum(color);
    for (double &channel : color)
        channel += shift;
    return clipColor(color);
}

double sat(const Rgb &color)
{
    return *std::max_element(color.begin(), color.end()) - *std::min_element(color.begin(), color.end());
}

Rgb setSat(Rgb color, double s)
{
    const auto [low, high] = std::minmax_element(color.begin(), color.end());
    const double floor = *low, ceiling = *high;
    for (double &channel : color)
        channel = ceiling > floor ? (channel - floor) * s / (ceiling - floor) : 0;
    return color;
}

// The PDF and W3C blend function B(backdrop, source) per mode.
Rgb reference(LayerBlendMode mode, const Rgb &backdrop, const Rgb &source)
{
    Rgb result{};
    for (int channel = 0; channel < 3; ++channel) {
        const double b = backdrop[channel], s = source[channel];
        const auto hardLight = [](double below, double above) {
            return above <= 0.5 ? below * 2 * above : below + (2 * above - 1) - below * (2 * above - 1);
        };
        switch (mode) {
        case LayerBlendMode::normal: result[channel] = s; break;
        case LayerBlendMode::multiply: result[channel] = b * s; break;
        case LayerBlendMode::screen: result[channel] = b + s - b * s; break;
        case LayerBlendMode::overlay: result[channel] = hardLight(s, b); break;
        case LayerBlendMode::softLight: {
            const double d = b <= 0.25 ? ((16 * b - 12) * b + 4) * b : std::sqrt(b);
            result[channel] = s <= 0.5 ? b - (1 - 2 * s) * b * (1 - b) : b + (2 * s - 1) * (d - b);
            break;
        }
        case LayerBlendMode::darken: result[channel] = std::min(b, s); break;
        case LayerBlendMode::lighten: result[channel] = std::max(b, s); break;
        case LayerBlendMode::difference: result[channel] = std::abs(b - s); break;
        case LayerBlendMode::colorDodge: result[channel] = b == 0 ? 0 : s >= 1 ? 1 : std::min(1.0, b / (1 - s)); break;
        case LayerBlendMode::colorBurn: result[channel] = b >= 1 ? 1 : s <= 0 ? 0 : 1 - std::min(1.0, (1 - b) / s); break;
        default: break;
        }
    }
    switch (mode) {
    case LayerBlendMode::hue: return setLum(setSat(source, sat(backdrop)), lum(backdrop));
    case LayerBlendMode::saturation: return setLum(setSat(backdrop, sat(source)), lum(backdrop));
    case LayerBlendMode::color: return setLum(source, lum(backdrop));
    case LayerBlendMode::luminosity: return setLum(backdrop, lum(source));
    default: return result;
    }
}

// Expected premultiplied bytes, from the premultiplied bytes drawn.
std::array<int, 4> composited(LayerBlendMode mode, QRgb backdropPixel, QRgb sourcePixel)
{
    const auto color = [](QRgb pixel) {
        const double alpha = std::max(1, qAlpha(pixel));
        return Rgb{qRed(pixel) / alpha, qGreen(pixel) / alpha, qBlue(pixel) / alpha};
    };
    const Rgb backdrop = color(backdropPixel), source = color(sourcePixel);
    const double backdropAlpha = qAlpha(backdropPixel) / 255.0, sourceAlpha = qAlpha(sourcePixel) / 255.0;
    const Rgb both = reference(mode, backdrop, source);
    std::array<int, 4> bytes{};
    for (int channel = 0; channel < 3; ++channel) {
        const double value = source[channel] * sourceAlpha * (1 - backdropAlpha)
            + backdrop[channel] * backdropAlpha * (1 - sourceAlpha) + both[channel] * sourceAlpha * backdropAlpha;
        bytes[channel] = int(std::lround(value * 255));
    }
    bytes[3] = int(std::lround((sourceAlpha + backdropAlpha - sourceAlpha * backdropAlpha) * 255));
    return bytes;
}

QRgb premultiplied(const Rgb &color, double alpha)
{
    return qRgba(int(std::lround(color[0] * alpha * 255)), int(std::lround(color[1] * alpha * 255)),
                 int(std::lround(color[2] * alpha * 255)), int(std::lround(alpha * 255)));
}
}

class BlendModeTests : public QObject {
    Q_OBJECT
private slots:
    void everyBlendModeMatchesThePdfFormulas_data();
    void everyBlendModeMatchesThePdfFormulas();
    void hslModesHonorLayerOpacity();
    void hslModesIgnoreThePaintersOwnOpacity();
    void hslResultsStayValidPremultipliedPixels();
    void hslModesRespectThePainterClip();
    void hslModesNeedAnImageBackedPainter();
    void hslBlendChecksItsInputs();
};

void BlendModeTests::everyBlendModeMatchesThePdfFormulas_data()
{
    QTest::addColumn<LayerBlendMode>("mode");
    const std::pair<const char *, LayerBlendMode> modes[] = {
        {"normal", LayerBlendMode::normal}, {"multiply", LayerBlendMode::multiply}, {"screen", LayerBlendMode::screen},
        {"overlay", LayerBlendMode::overlay}, {"softLight", LayerBlendMode::softLight}, {"darken", LayerBlendMode::darken}, {"lighten", LayerBlendMode::lighten},
        {"difference", LayerBlendMode::difference}, {"colorDodge", LayerBlendMode::colorDodge},
        {"colorBurn", LayerBlendMode::colorBurn}, {"hue", LayerBlendMode::hue}, {"saturation", LayerBlendMode::saturation},
        {"color", LayerBlendMode::color}, {"luminosity", LayerBlendMode::luminosity}};
    for (const auto &[name, mode] : modes)
        QTest::newRow(name) << mode;
}

void BlendModeTests::everyBlendModeMatchesThePdfFormulas()
{
    QFETCH(LayerBlendMode, mode);
    const Rgb colors[] = {{0.9, 0.2, 0.4}, {0.1, 0.6, 0.3}, {0.5, 0.5, 0.5}, {1, 1, 1}, {0, 0, 0}, {0.25, 0.75, 1}};
    const double alphas[] = {1, 0.6, 0.2, 0};
    const LayerTransform transform = placedAt({0, 0}, {2, 2});
    for (const Rgb &backdrop : colors) {
        for (const Rgb &source : colors) {
            for (double backdropAlpha : alphas) {
                for (double sourceAlpha : alphas) {
                    const QRgb below = premultiplied(backdrop, backdropAlpha), above = premultiplied(source, sourceAlpha);
                    QImage surface = solid(2, 2, below);
                    QPainter painter(&surface);
                    LayerRenderer::draw(solid(2, 2, above), transform, transform.center(), painter, {.blendMode = mode});
                    painter.end();
                    const std::array<int, 4> expected = composited(mode, below, above);
                    const uchar *pixel = surface.constScanLine(1) + 4;
                    for (int channel = 0; channel < 4; ++channel) {
                        QVERIFY2(std::abs(pixel[channel] - expected[channel]) <= 2,
                                 qPrintable(QString("channel %1: %2 not %3, backdrop alpha %4, source alpha %5")
                                                .arg(channel).arg(pixel[channel]).arg(expected[channel])
                                                .arg(backdropAlpha).arg(sourceAlpha)));
                    }
                }
            }
        }
    }
}

void BlendModeTests::hslModesHonorLayerOpacity()
{
    const QRgb below = qRgba(200, 40, 40, 255), above = qRgba(30, 90, 220, 255);
    const LayerTransform transform = placedAt({0, 0}, {2, 2});
    for (LayerBlendMode mode : {LayerBlendMode::hue, LayerBlendMode::saturation, LayerBlendMode::color, LayerBlendMode::luminosity}) {
        QImage surface = solid(2, 2, below);
        QPainter painter(&surface);
        LayerRenderer::draw(solid(2, 2, above), transform, transform.center(), painter, {.opacity = 0.4, .blendMode = mode});
        painter.end();
        const std::array<int, 4> expected = composited(mode, below, qRgba(12, 36, 88, 102));
        const uchar *pixel = surface.constScanLine(0);
        for (int channel = 0; channel < 4; ++channel)
            QVERIFY(std::abs(pixel[channel] - expected[channel]) <= 2);
        QVERIFY(surface.pixel(0, 0) != below);
    }
}

void BlendModeTests::hslModesRespectThePainterClip()
{
    QImage surface = solid(8, 8, qRgba(200, 40, 40, 255));
    QPainter painter(&surface);
    painter.setClipRect(QRect(0, 0, 4, 8));
    const LayerTransform transform = placedAt({0, 0}, {8, 8});
    LayerRenderer::draw(solid(8, 8, qRgba(40, 40, 200, 255)), transform, transform.center(), painter,
                        {.blendMode = LayerBlendMode::color});
    painter.end();
    QVERIFY(surface.pixel(1, 1) != qRgba(200, 40, 40, 255));
    QCOMPARE(surface.pixel(5, 5), qRgba(200, 40, 40, 255));
}

void BlendModeTests::hslModesNeedAnImageBackedPainter()
{
    QPixmap pixmap(4, 4);
    QPainter painter(&pixmap);
    const LayerTransform transform = placedAt({0, 0}, {4, 4});
    QVERIFY_THROWS_EXCEPTION(std::logic_error, LayerRenderer::draw(solid(4, 4, qRgba(9, 9, 9, 255)), transform,
                                                                   transform.center(), painter,
                                                                   {.blendMode = LayerBlendMode::luminosity}));
}

void BlendModeTests::hslBlendChecksItsInputs()
{
    const QImage color = solid(2, 2, qRgba(9, 9, 9, 255));
    QVERIFY_THROWS_EXCEPTION(std::logic_error, HslBlend::blend(color, solid(3, 2, qRgba(9, 9, 9, 255)), LayerBlendMode::hue, 1));
    QVERIFY_THROWS_EXCEPTION(std::logic_error, HslBlend::blend(color.convertToFormat(QImage::Format_ARGB32), color, LayerBlendMode::hue, 1));
    QVERIFY_THROWS_EXCEPTION(std::logic_error, HslBlend::blend(color, color.convertToFormat(QImage::Format_ARGB32), LayerBlendMode::hue, 1));
    QVERIFY_THROWS_EXCEPTION(std::logic_error, HslBlend::blend(color, color, LayerBlendMode::multiply, 1));
    QVERIFY(HslBlend::handles(LayerBlendMode::hue) && HslBlend::handles(LayerBlendMode::saturation));
    QVERIFY(HslBlend::handles(LayerBlendMode::color) && HslBlend::handles(LayerBlendMode::luminosity));
    QVERIFY(!HslBlend::handles(LayerBlendMode::normal) && !HslBlend::handles(LayerBlendMode::colorBurn));

    const QImage large(8192, 8192, QImage::Format_RGBA8888_Premultiplied), other(8192, 8192, QImage::Format_RGBA8888_Premultiplied);
    std::optional<AddressSpaceLimit> limit(std::in_place, 16 * 1024 * 1024);
    std::optional<ExportError::Kind> thrown;
    try {
        HslBlend::blend(large, other, LayerBlendMode::hue, 1);
    } catch (const ExportError &error) {
        thrown = error.kind;
    }
    limit.reset();
    QCOMPARE(thrown, std::optional(ExportError::Kind::render));
}

void BlendModeTests::hslModesIgnoreThePaintersOwnOpacity()
{
    QImage surface = BrushRaster::context(2, 2, false);
    QPainter painter(&surface);
    painter.setOpacity(0.25);
    const LayerTransform transform = placedAt({0, 0}, {2, 2});
    LayerRenderer::draw(solid(2, 2, qRgba(30, 90, 220, 255)), transform, transform.center(), painter,
                        {.blendMode = LayerBlendMode::color});
    QCOMPARE(painter.opacity(), 0.25);
    painter.end();
    QCOMPARE(surface.pixel(0, 0), qRgba(30, 90, 220, 255));
}

void BlendModeTests::hslResultsStayValidPremultipliedPixels()
{
    const QImage worst = HslBlend::blend(solid(1, 1, qRgba(30, 30, 30, 30)), solid(1, 1, qRgba(17, 17, 17, 17)),
                                         LayerBlendMode::hue, 0.9);
    const uchar *pixel = worst.constScanLine(0);
    QVERIFY(pixel[0] <= pixel[3] && pixel[1] <= pixel[3] && pixel[2] <= pixel[3]);
    QCOMPARE(pixel[3], uchar(43));

    QRandomGenerator random(7);
    QImage backdrop = BrushRaster::context(64, 64, false), source = backdrop;
    for (QImage *image : {&backdrop, &source}) {
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                const int alpha = random.bounded(256);
                image->setPixel(x, y, qRgba(random.bounded(alpha + 1), random.bounded(alpha + 1), random.bounded(alpha + 1), alpha));
            }
        }
    }
    for (LayerBlendMode mode : {LayerBlendMode::hue, LayerBlendMode::saturation, LayerBlendMode::color, LayerBlendMode::luminosity}) {
        for (double opacity : {1.0, 0.9, 0.37}) {
            const QImage blended = HslBlend::blend(backdrop, source, mode, opacity);
            int invalid = 0;
            for (int y = 0; y < 64; ++y) {
                const uchar *row = blended.constScanLine(y);
                for (int x = 0; x < 64; ++x, row += 4)
                    invalid += row[0] > row[3] || row[1] > row[3] || row[2] > row[3];
            }
            QCOMPARE(invalid, 0);
        }
    }
}

QTEST_MAIN(BlendModeTests)
#include "BlendModeTests.moc"
