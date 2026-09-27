#include "Document/ImageAdjustments.h"
#include "Document/BrushStroke.h"
#include "IO/ProjectStore.h"
#include <QPainter>
#include <algorithm>
#include <array>
#include <cmath>
extern "C" {
#include "AdjustPixels.h"
#include "LevelsPixels.h"
}

QImage ImageAdjustmentPixels::run(const QImage &image, const std::function<void(uchar *, int, int, qsizetype)> &body)
{
    QImage context = BrushRaster::context(image.width(), image.height(), false);
    {
        QPainter painter(&context);
        BrushRaster::draw(image, QRectF(0, 0, image.width(), image.height()), painter);
    }
    body(context.bits(), context.width(), context.height(), context.bytesPerLine());
    return context;
}

double ImageAdjustmentPixels::clamp(double value, double low, double high, double fallback)
{
    return std::isfinite(value) ? std::min(high, std::max(low, value)) : fallback;
}

bool AdjustmentColor::isValid() const
{
    return red >= 0 && red <= 1 && green >= 0 && green <= 1 && blue >= 0 && blue <= 1;
}

AdjustmentColor AdjustmentColor::clamped() const
{
    using ImageAdjustmentPixels::clamp;
    return {clamp(red, 0, 1, 0), clamp(green, 0, 1, 0), clamp(blue, 0, 1, 0)};
}

bool ExposureSettings::isValid() const
{
    return exposure >= exposureLow && exposure <= exposureHigh && offset >= offsetLow && offset <= offsetHigh && gamma >= gammaLow
        && gamma <= gammaHigh;
}

ExposureSettings ExposureSettings::normalized() const
{
    using ImageAdjustmentPixels::clamp;
    return {clamp(exposure, exposureLow, exposureHigh, 0), clamp(offset, offsetLow, offsetHigh, 0), clamp(gamma, gammaLow, gammaHigh, 1)};
}

std::array<float, 256> ExposureSettings::table() const
{
    const double scale = std::pow(2, exposure);
    std::array<float, 256> result;
    for (size_t index = 0; index < 256; ++index) {
        const double encoded = double(index) / 255;
        double linear = encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
        linear = std::pow(std::max(0.0, linear * scale + offset), 1 / gamma);
        const double output = linear <= 0.0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1 / 2.4) - 0.055;
        result[index] = float(std::min(1.0, std::max(0.0, output)));
    }
    return result;
}

QImage ExposureSettings::apply(const QImage &image) const
{
    if (!isValid())
        throw ProjectError(ProjectError::Kind::invalid);
    const std::array<float, 256> curve = table();
    std::array<float, 3 * 256> tables;
    for (size_t channel = 0; channel < 3; ++channel)
        std::copy(curve.begin(), curve.end(), tables.begin() + qsizetype(channel * 256));
    return ImageAdjustmentPixels::run(image, [&](uchar *pixels, int width, int height, qsizetype) {
        levels_apply(pixels, size_t(width) * size_t(height), tables.data());
    });
}

bool GradientMapSettings::isValid() const
{
    return shadows.isValid() && highlights.isValid();
}

GradientMapSettings GradientMapSettings::normalized() const
{
    return {shadows.clamped(), highlights.clamped(), reversed};
}

GradientMapSettings::Ends GradientMapSettings::ends() const
{
    return reversed ? Ends{highlights, shadows} : Ends{shadows, highlights};
}

QImage GradientMapSettings::apply(const QImage &image) const
{
    if (!isValid())
        throw ProjectError(ProjectError::Kind::invalid);
    const auto [dark, light] = ends();
    std::array<uint8_t, 3 * 256> table;
    const auto byte = [](double value) { return uint8_t(std::min(255.0, std::max(0.0, std::round(value * 255)))); };
    for (size_t index = 0; index < 256; ++index) {
        const double t = double(index) / 255;
        table[index * 3] = byte(dark.red + (light.red - dark.red) * t);
        table[index * 3 + 1] = byte(dark.green + (light.green - dark.green) * t);
        table[index * 3 + 2] = byte(dark.blue + (light.blue - dark.blue) * t);
    }
    return ImageAdjustmentPixels::run(image, [&](uchar *pixels, int width, int height, qsizetype stride) {
        adjust_gradient_map(pixels, size_t(width), size_t(height), size_t(stride), table.data());
    });
}

bool GrainSettings::isValid() const
{
    return amount >= amountLow && amount <= amountHigh && size >= sizeLow && size <= sizeHigh && roughness >= roughnessLow
        && roughness <= roughnessHigh;
}

GrainSettings GrainSettings::normalized() const
{
    using ImageAdjustmentPixels::clamp;
    return {clamp(amount, amountLow, amountHigh, 25), clamp(size, sizeLow, sizeHigh, 1.5), clamp(roughness, roughnessLow, roughnessHigh, 50), seed};
}

QImage GrainSettings::apply(const QImage &image, QPointF origin, double unitsPerPixel, std::optional<quint32> seed) const
{
    if (!isValid() || !std::isfinite(unitsPerPixel) || !(unitsPerPixel > 0))
        throw ProjectError(ProjectError::Kind::invalid);
    if (!(amount > 0))
        return image;
    const quint32 pattern = seed.value_or(this->seed);
    return ImageAdjustmentPixels::run(image, [&](uchar *pixels, int width, int height, qsizetype stride) {
        adjust_grain(pixels, size_t(width), size_t(height), size_t(stride), amount, size, roughness, pattern, origin.x(), origin.y(), unitsPerPixel);
    });
}

bool BlackWhiteSettings::isValid() const
{
    const std::array weights{reds, yellows, greens, cyans, blues, magentas};
    return std::all_of(weights.begin(), weights.end(), [](double weight) { return weight >= low && weight <= high; })
        && tintHue >= 0 && tintHue <= 360 && tintSaturation >= 0 && tintSaturation <= 100;
}

QImage BlackWhiteSettings::apply(const QImage &image) const
{
    if (!isValid())
        throw ProjectError(ProjectError::Kind::invalid);
    // The kernel's order: red, yellow, green, cyan, blue, magenta.
    const std::array<float, 6> weights{float(reds / 100), float(yellows / 100), float(greens / 100),
                                       float(cyans / 100), float(blues / 100), float(magentas / 100)};
    return ImageAdjustmentPixels::run(image, [&](uchar *pixels, int width, int height, qsizetype stride) {
        adjust_black_white(pixels, size_t(width), size_t(height), size_t(stride), weights.data(), tint ? 1 : 0, tintHue, tintSaturation / 100);
    });
}

namespace {
std::array<double, 9> amounts(const ColorBalanceSettings &settings)
{
    return {settings.shadowCyanRed, settings.shadowMagentaGreen, settings.shadowYellowBlue, settings.midCyanRed, settings.midMagentaGreen,
            settings.midYellowBlue, settings.highlightCyanRed, settings.highlightMagentaGreen, settings.highlightYellowBlue};
}
}

bool ColorBalanceSettings::isValid() const
{
    const std::array<double, 9> all = amounts(*this);
    return std::all_of(all.begin(), all.end(), [](double amount) { return amount >= low && amount <= high; });
}

bool ColorBalanceSettings::isIdentity() const
{
    const std::array<double, 9> all = amounts(*this);
    return std::all_of(all.begin(), all.end(), [](double amount) { return amount == 0; });
}

QImage ColorBalanceSettings::apply(const QImage &image) const
{
    if (!isValid())
        throw ProjectError(ProjectError::Kind::invalid);
    if (isIdentity())
        return image;
    std::array<float, 9> ranges;
    const std::array<double, 9> all = amounts(*this);
    std::transform(all.begin(), all.end(), ranges.begin(), [](double amount) { return float(amount / 100); });
    return ImageAdjustmentPixels::run(image, [&](uchar *pixels, int width, int height, qsizetype stride) {
        adjust_color_balance(pixels, size_t(width), size_t(height), size_t(stride), ranges.data(), ranges.data() + 3, ranges.data() + 6,
                             preserveLuminosity ? 1 : 0);
    });
}
