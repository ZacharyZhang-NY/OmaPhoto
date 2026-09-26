#include "Rendering/HslBlend.h"
#include "Rendering/PoolMap.h"
#include "IO/ImageExporter.h"
#include <QtConcurrent>
#include <algorithm>
#include <array>
#include <numeric>
#include <stdexcept>

namespace {
using Color = std::array<float, 3>;

float lum(const Color &color)
{
    return 0.3f * color[0] + 0.59f * color[1] + 0.11f * color[2];
}

Color clipColor(Color color)
{
    const float l = lum(color);
    const float low = std::min({color[0], color[1], color[2]}), high = std::max({color[0], color[1], color[2]});
    for (float &channel : color) {
        if (low < 0)
            channel = l + (channel - l) * l / (l - low);
        if (high > 1)
            channel = l + (channel - l) * (1 - l) / (high - l);
    }
    return color;
}

Color setLum(Color color, float l)
{
    const float shift = l - lum(color);
    for (float &channel : color)
        channel += shift;
    return clipColor(color);
}

float sat(const Color &color)
{
    return std::max({color[0], color[1], color[2]}) - std::min({color[0], color[1], color[2]});
}

Color setSat(const Color &color, float s)
{
    std::array<int, 3> order{0, 1, 2};
    std::sort(order.begin(), order.end(), [&](int lhs, int rhs) { return color[lhs] < color[rhs]; });
    const int low = order[0], middle = order[1], high = order[2];
    Color result{0, 0, 0};
    if (color[high] > color[low]) {
        result[middle] = (color[middle] - color[low]) * s / (color[high] - color[low]);
        result[high] = s;
    }
    return result;
}

Color mixed(LayerBlendMode mode, const Color &backdrop, const Color &source)
{
    switch (mode) {
    case LayerBlendMode::hue:
        return setLum(setSat(source, sat(backdrop)), lum(backdrop));
    case LayerBlendMode::saturation:
        return setLum(setSat(backdrop, sat(source)), lum(backdrop));
    case LayerBlendMode::color:
        return setLum(source, lum(backdrop));
    case LayerBlendMode::luminosity:
        return setLum(backdrop, lum(source));
    default:
        throw std::logic_error("HslBlend handles only the four non-separable modes");
    }
}
}

bool HslBlend::handles(LayerBlendMode mode)
{
    return mode == LayerBlendMode::hue || mode == LayerBlendMode::saturation || mode == LayerBlendMode::color
        || mode == LayerBlendMode::luminosity;
}

QImage HslBlend::blend(const QImage &backdrop, const QImage &source, LayerBlendMode mode, double opacity)
{
    if (backdrop.size() != source.size() || backdrop.format() != QImage::Format_RGBA8888_Premultiplied
        || source.format() != QImage::Format_RGBA8888_Premultiplied)
        throw std::logic_error("HslBlend needs two RGBA8888 premultiplied images of one size");
    // Checked here: a worker's exception would come back wrapped.
    if (!handles(mode))
        throw std::logic_error("HslBlend blends hue, saturation, color and luminosity alone");
    QImage result(backdrop.size(), QImage::Format_RGBA8888_Premultiplied);
    if (result.isNull())
        throw ExportError(ExportError::Kind::render);
    // Pointers taken first: `scanLine` detaches, which would race.
    const uchar *const lower = backdrop.constBits(), *const upper = source.constBits();
    uchar *const target = result.bits();
    const qsizetype lowerStride = backdrop.bytesPerLine(), upperStride = source.bytesPerLine(), targetStride = result.bytesPerLine();
    std::vector<int> rows;
    try {
        rows.resize(size_t(result.height()));
    } catch (const std::bad_alloc &) {
        throw ExportError(ExportError::Kind::render);
    }
    std::iota(rows.begin(), rows.end(), 0);
    // Each pixel stands alone: rows blend side by side.
    PoolMap::blocking(rows, [&](int y) {
        const uchar *below = lower + y * lowerStride, *above = upper + y * upperStride;
        uchar *out = target + y * targetStride;
        for (int x = 0; x < result.width(); ++x, below += 4, above += 4, out += 4) {
            const float sourceAlpha = above[3] / 255.0f * float(opacity), backdropAlpha = below[3] / 255.0f;
            if (sourceAlpha <= 0) {
                std::copy_n(below, 4, out);
                continue;
            }
            Color sourceColor{0, 0, 0}, backdropColor{0, 0, 0};
            for (int channel = 0; channel < 3; ++channel) {
                sourceColor[channel] = above[channel] / float(above[3]);
                backdropColor[channel] = below[3] ? below[channel] / float(below[3]) : 0;
            }
            const Color both = mixed(mode, backdropColor, sourceColor);
            const float alpha = sourceAlpha + backdropAlpha - sourceAlpha * backdropAlpha;
            out[3] = uchar(std::clamp(int(alpha * 255 + 0.5f), 0, 255));
            for (int channel = 0; channel < 3; ++channel) {
                const float value = sourceColor[channel] * sourceAlpha * (1 - backdropAlpha)
                    + backdropColor[channel] * backdropAlpha * (1 - sourceAlpha)
                    + both[channel] * sourceAlpha * backdropAlpha;
                // Premultiplied color never exceeds its alpha.
                out[channel] = uchar(std::clamp(int(value * 255 + 0.5f), 0, int(out[3])));
            }
        }
    });
    return result;
}
