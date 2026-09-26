#include "Document/BrushStroke.h"
#include "Rendering/PoolMap.h"
#include "Document/HueSaturation.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include <QPainter>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace {
struct HSL {
    double hue;
    double saturation;
    double lightness;
};

HSL toHSL(double red, double green, double blue)
{
    const double high = std::max({red, green, blue}), low = std::min({red, green, blue});
    const double lightness = (high + low) / 2, delta = high - low;
    if (!(delta > 0))
        return {0, 0, lightness};
    const double saturation = delta / (1 - std::abs(2 * lightness - 1));
    double hue = high == red ? (green - blue) / delta : high == green ? (blue - red) / delta + 2 : (red - green) / delta + 4;
    hue *= 60;
    if (hue < 0)
        hue += 360;
    return {hue, std::min(1.0, saturation), lightness};
}

HueSaturationFilter::Color toRGB(double hue, double saturation, double lightness)
{
    if (!(saturation > 0))
        return {lightness, lightness, lightness};
    const double chroma = (1 - std::abs(2 * lightness - 1)) * saturation, sector = hue / 60;
    const double second = chroma * (1 - std::abs(std::fmod(sector, 2) - 1)), base = lightness - chroma / 2;
    HueSaturationFilter::Color color{chroma, 0, second};
    switch (int(sector)) {
    case 0: color = {chroma, second, 0}; break;
    case 1: color = {second, chroma, 0}; break;
    case 2: color = {0, chroma, second}; break;
    case 3: color = {0, second, chroma}; break;
    case 4: color = {second, 0, chroma}; break;
    }
    const auto clamp = [base](double value) { return std::min(1.0, std::max(0.0, value + base)); };
    return {clamp(color.red), clamp(color.green), clamp(color.blue)};
}

// Core Image's colour cube: unpremultiplied, trilinear, premultiplied back.
void applyCube(uchar *bits, qsizetype stride, int width, const std::vector<float> &cube, const std::vector<int> &rows)
{
    constexpr int size = HueSaturationFilter::dimension, last = size - 1;
    PoolMap::blocking(rows, [&](int y) {
        uchar *pixel = bits + y * stride;
        for (int x = 0; x < width; ++x, pixel += 4) {
            const float alpha = pixel[3];
            if (alpha == 0) {
                std::fill_n(pixel, 3, uchar(0));
                continue;
            }
            float place[3], fraction[3];
            int low[3];
            for (int channel = 0; channel < 3; ++channel) {
                place[channel] = std::min(1.0f, pixel[channel] / alpha) * last;
                low[channel] = std::min(int(place[channel]), last - 1);
                fraction[channel] = place[channel] - float(low[channel]);
            }
            float out[3] = {0, 0, 0};
            for (int corner = 0; corner < 8; ++corner) {
                const int r = low[0] + (corner & 1), g = low[1] + (corner >> 1 & 1), b = low[2] + (corner >> 2 & 1);
                const float share = ((corner & 1) ? fraction[0] : 1 - fraction[0]) * ((corner >> 1 & 1) ? fraction[1] : 1 - fraction[1])
                    * ((corner >> 2 & 1) ? fraction[2] : 1 - fraction[2]);
                const float *entry = &cube[size_t(((b * size + g) * size + r) * 4)];
                for (int channel = 0; channel < 3; ++channel)
                    out[channel] += share * entry[channel];
            }
            for (int channel = 0; channel < 3; ++channel)
                pixel[channel] = uchar(std::lround(std::min(1.0f, std::max(0.0f, out[channel])) * alpha));
        }
    });
}
}

std::vector<HueSaturationFilter::HueResponse> HueSaturationFilter::hueResponse(const HueSaturationSettings &settings)
{
    std::vector<HueResponse> result(361);
    for (int degree = 0; degree <= 360; ++degree) {
        HueResponse &response = result[size_t(degree)];
        for (const auto &[colorRange, adjustment] : settings.adjustments) {
            if (adjustment == RangeAdjustment())
                continue;
            const double weight = settings.weight(colorRange, degree);
            if (!(weight > 0))
                continue;
            response.shift += adjustment.hue * weight;
            response.saturation += adjustment.saturation * weight;
            response.lightness += adjustment.lightness * weight;
        }
    }
    return result;
}

std::vector<float> HueSaturationFilter::cube(const HueSaturationSettings &settings)
{
    const std::vector<HueResponse> response = hueResponse(settings);
    std::vector<float> values(size_t(dimension * dimension * dimension * 4));
    size_t index = 0;
    const double step = dimension - 1;
    for (int blue = 0; blue < dimension; ++blue) {
        for (int green = 0; green < dimension; ++green) {
            for (int red = 0; red < dimension; ++red) {
                const Color color = adjust(red / step, green / step, blue / step, settings, response);
                values[index] = float(color.red);
                values[index + 1] = float(color.green);
                values[index + 2] = float(color.blue);
                values[index + 3] = 1;
                index += 4;
            }
        }
    }
    return values;
}

HueSaturationFilter::Color HueSaturationFilter::adjust(double red, double green, double blue, const HueSaturationSettings &settings)
{
    return adjust(red, green, blue, settings, hueResponse(settings));
}

HueSaturationFilter::Color HueSaturationFilter::adjust(double red, double green, double blue, const HueSaturationSettings &settings,
                                                       const std::vector<HueResponse> &response)
{
    auto [hue, saturation, lightness] = toHSL(red, green, blue);
    double lightnessAmount = 0;
    if (settings.colorize) {
        hue = std::fmod(settings.hue(), 360);
        saturation = std::min(1.0, std::max(0.0, settings.saturation() / 100));
        lightnessAmount = settings.lightness() / 100;
    } else {
        // Every range adds in, weighted by its claim.
        const HueResponse &sampled = response[size_t(std::min(response.size() - 1, size_t(std::max(0.0, std::round(hue)))))];
        lightnessAmount = sampled.lightness / 100;
        hue = std::fmod(hue + sampled.shift, 360);
        if (hue < 0)
            hue += 360;
        // Multiplied, so neutral grays stay neutral.
        saturation = std::min(1.0, std::max(0.0, saturation * (1 + sampled.saturation / 100)));
    }
    // Toward white above zero, black below, reaching either at 100.
    const double amount = std::min(1.0, std::max(-1.0, lightnessAmount));
    lightness = amount >= 0 ? lightness + (1 - lightness) * amount : lightness * (1 + amount);
    return toRGB(hue, saturation, std::min(1.0, std::max(0.0, lightness)));
}

double HueSaturationFilter::shiftedHue(double hue, const HueSaturationSettings &settings)
{
    double shift = 0;
    for (const auto &[colorRange, adjustment] : settings.adjustments) {
        if (adjustment.hue != 0)
            shift += adjustment.hue * settings.weight(colorRange, hue);
    }
    const double shifted = std::fmod(hue + shift, 360);
    return shifted < 0 ? shifted + 360 : shifted;
}

AdjustedPixels HueSaturationFilter::run(const HueSaturationJob &job)
{
    const int width = job.image.width(), height = job.image.height();
    QImage result = BrushRaster::context(width, height, false);
    {
        QPainter painter(&result);
        BrushRaster::draw(job.image, QRectF(0, 0, width, height), painter);
    }
    std::vector<float> table;
    std::vector<int> rows;
    try {
        table = cube(job.settings);
        rows.resize(size_t(height));
    } catch (const std::bad_alloc &) {
        // Out of memory fails as a context would.
        throw ExportError(ExportError::Kind::render);
    }
    std::iota(rows.begin(), rows.end(), 0);
    applyCube(result.bits(), result.bytesPerLine(), width, table, rows);
    // Swift blends over the image drawn again: the same pixels.
    if (job.selection)
        result = PixelAdjust::blend(result, job.image, *job.selection, job.pixelToDocument, false);
    return {result, job.thumbnail ? std::optional(PixelAdjust::thumbnail(result)) : std::nullopt};
}
