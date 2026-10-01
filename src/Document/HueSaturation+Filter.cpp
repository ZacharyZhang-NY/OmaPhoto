#include "Document/BrushStroke.h"
#include "Document/HueSaturation.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <mutex>
extern "C" {
#include "LevelsPixels.h"
}

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

// The last tables built: building costs more than applying.
struct CubeCache {
    std::mutex lock;
    std::deque<std::pair<HueSaturationSettings, std::vector<float>>> cubes;
};

CubeCache &cubeCache()
{
    static CubeCache cache;
    return cache;
}

std::vector<float> built(const HueSaturationSettings &settings)
{
    const std::vector<HueSaturationFilter::HueResponse> response = HueSaturationFilter::hueResponse(settings);
    constexpr int dimension = HueSaturationFilter::dimension;
    std::vector<float> values(size_t(dimension * dimension * dimension * 4));
    size_t index = 0;
    const double step = dimension - 1;
    for (int blue = 0; blue < dimension; ++blue) {
        for (int green = 0; green < dimension; ++green) {
            for (int red = 0; red < dimension; ++red) {
                const HueSaturationFilter::Color color = HueSaturationFilter::adjust(red / step, green / step, blue / step, settings, response);
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
    CubeCache &cache = cubeCache();
    {
        const std::lock_guard<std::mutex> held(cache.lock);
        for (const auto &[known, values] : cache.cubes) {
            if (known == settings)
                return values;
        }
    }
    std::vector<float> values = built(settings);
    const std::lock_guard<std::mutex> held(cache.lock);
    std::erase_if(cache.cubes, [&](const auto &entry) { return entry.first == settings; });
    cache.cubes.emplace_front(settings, values);
    if (cache.cubes.size() > 8)
        cache.cubes.pop_back();
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
        saturation = adjustedSaturation(saturation, sampled.saturation);
    }
    // Toward white above zero, black below, reaching either at 100.
    const double amount = std::min(1.0, std::max(-1.0, lightnessAmount));
    lightness = amount >= 0 ? lightness + (1 - lightness) * amount : lightness * (1 + amount);
    return toRGB(hue, saturation, std::min(1.0, std::max(0.0, lightness)));
}

double HueSaturationFilter::adjustedSaturation(double saturation, double amount)
{
    const double share = std::min(1.0, std::max(-1.0, amount / 100));
    // Multiplied both ways, so neutral grays stay neutral.
    if (!(share > 0))
        return std::max(0.0, saturation * (1 + share));
    return share >= 1 ? (saturation > 0 ? 1 : 0) : std::min(1.0, saturation / (1 - share));
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
    // Across the cores: it runs over the view each frame.
    QImage result = BrushRaster::copy(job.image);
    std::vector<float> table;
    try {
        table = cube(job.settings);
    } catch (const std::bad_alloc &) {
        // Out of memory fails as a context would.
        throw ExportError(ExportError::Kind::render);
    }
    // The lookup unpremultiplies around itself.
    uchar *const pixels = result.bits();
    BrushRaster::inBands(qsizetype(result.width()) * result.height(),
                         [&](qsizetype start, qsizetype length) { cube_apply(pixels + start * 4, size_t(length), table.data(), dimension); });
    // Swift blends over the image drawn again: the same pixels.
    if (job.selection)
        result = PixelAdjust::blend(result, job.image, *job.selection, job.pixelToDocument, false);
    return {result, job.thumbnail ? std::optional(PixelAdjust::thumbnail(result)) : std::nullopt};
}
