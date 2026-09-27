#include "Document/CameraRaw.h"
#include "Document/BrushStroke.h"
#include "Document/Filters.h"
#include "Document/Levels.h"
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <numbers>
extern "C" {
#include "AdjustPixels.h"
#include "LevelsPixels.h"
}

using ImageAdjustmentPixels::clamp;

namespace {
double decode(double encoded)
{
    return encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
}

// The image drawn into the kernels' premultiplied context.
QImage drawn(const QImage &image)
{
    QImage context = BrushRaster::context(image.width(), image.height(), false);
    QPainter painter(&context);
    BrushRaster::draw(image, QRectF(0, 0, image.width(), image.height()), painter);
    return context;
}
}

bool CameraRawSettings::adjustsLight() const
{
    return exposure != 0 || contrast != 0 || highlights != 0 || shadows != 0 || whites != 0 || blacks != 0;
}

bool CameraRawSettings::adjustsColor() const
{
    return temperature != 0 || tint != 0 || vibrance != 0 || saturation != 0;
}

bool CameraRawSettings::adjustsEffects() const
{
    return texture != 0 || clarity != 0 || dehaze != 0 || glow != 0 || vignetteAmount != 0 || grainAmount != 0;
}

bool CameraRawSettings::isIdentity() const
{
    return !adjustsLight() && !adjustsColor() && !adjustsEffects() && !curve.adjusts() && !mixer.adjusts() && !grading.adjusts()
        && !detail.adjusts() && !optics.adjusts() && !geometry.adjusts() && !calibration.adjusts();
}

CameraRawSettings CameraRawSettings::normalized() const
{
    CameraRawSettings result = *this;
    result.exposure = clamp(exposure, exposureLow, exposureHigh, 0);
    for (double CameraRawSettings::*tone : {&CameraRawSettings::contrast, &CameraRawSettings::highlights, &CameraRawSettings::shadows,
                                            &CameraRawSettings::whites, &CameraRawSettings::blacks, &CameraRawSettings::temperature,
                                            &CameraRawSettings::tint, &CameraRawSettings::vibrance, &CameraRawSettings::saturation,
                                            &CameraRawSettings::texture, &CameraRawSettings::clarity, &CameraRawSettings::dehaze,
                                            &CameraRawSettings::glowRange, &CameraRawSettings::glowSpread, &CameraRawSettings::glowWarmth,
                                            &CameraRawSettings::vignetteAmount, &CameraRawSettings::vignetteRoundness})
        result.*tone = clamp(this->*tone, -100, 100, 0);
    result.glow = clamp(glow, 0, 100, 0);
    result.vignetteMidpoint = clamp(vignetteMidpoint, 0, 100, 50);
    result.vignetteFeather = clamp(vignetteFeather, 0, 100, 50);
    result.vignetteHighlights = clamp(vignetteHighlights, 0, 100, 0);
    result.grainAmount = clamp(grainAmount, 0, 100, 0);
    result.grainSize = clamp(grainSize, 0, 100, 25);
    result.grainRoughness = clamp(grainRoughness, 0, 100, 50);
    result.curve = curve.normalized();
    result.mixer = mixer.normalized();
    result.grading = grading.normalized();
    result.detail = detail.normalized();
    result.optics = optics.normalized();
    result.geometry = geometry.normalized();
    result.calibration = calibration.normalized();
    return result;
}

CameraRawSettings CameraRawSettings::applying(const CameraRawGroups &shown) const
{
    CameraRawSettings result = *this;
    if (!shown.light)
        result.exposure = result.contrast = result.highlights = result.shadows = result.whites = result.blacks = 0;
    if (!shown.color)
        result.temperature = result.tint = result.vibrance = result.saturation = 0;
    if (!shown.effects)
        result.texture = result.clarity = result.dehaze = result.glow = result.vignetteAmount = result.grainAmount = 0;
    if (!shown.curve)
        result.curve = CameraRawCurveSettings();
    if (!shown.mixer)
        result.mixer = CameraRawMixerSettings();
    if (!shown.grading)
        result.grading = CameraRawGradingSettings();
    if (!shown.detail)
        result.detail = CameraRawDetailSettings();
    if (!shown.optics)
        result.optics = CameraRawOpticsSettings();
    if (!shown.geometry)
        result.geometry = CameraRawGeometrySettings();
    if (!shown.calibration)
        result.calibration = CameraRawCalibrationSettings();
    return result;
}

double CameraRawSettings::grainKernelSize() const
{
    return 0.5 + (grainSize / 100) * 19.5;
}

CameraRawSettings::Gains CameraRawSettings::gains() const
{
    const double warm = temperature / 100, magenta = tint / 100;
    return {1 + temperatureGain * warm + tintRedBlue * magenta, 1 - tintGreen * magenta, 1 - temperatureGain * warm + tintRedBlue * magenta};
}

QImage CameraRawSettings::apply(const QImage &image, std::optional<CameraRawClipping> clipping, double scale, quint32 seed, int visualizePointColor,
                                bool sharpenMask) const
{
    const CameraRawSettings settings = normalized();
    if (settings.isIdentity() && !clipping && visualizePointColor < 0 && !sharpenMask)
        return image;
    const Gains gain = settings.gains();
    const int mode = clipping ? int(*clipping) : 0;
    const double pixelScale = scale > 0 ? scale : 1;
    const bool paintColor = !clipping && !sharpenMask
        && (settings.curve.adjusts() || settings.mixer.adjusts() || settings.grading.adjusts() || visualizePointColor >= 0);
    const bool paintEffects = !clipping && !sharpenMask && settings.adjustsEffects();
    const bool paintDetailOptics = !clipping && (settings.detail.adjusts() || settings.optics.adjusts() || sharpenMask);
    QImage source = image;
    if (!clipping && !sharpenMask && visualizePointColor < 0 && settings.geometry.adjusts())
        source = settings.geometry.apply(source);
    return ImageAdjustmentPixels::run(source, [&](uchar *pixels, int width, int height, qsizetype stride) {
        const size_t w = size_t(width), h = size_t(height), s = size_t(stride);
        if (!clipping && !sharpenMask && settings.calibration.adjusts())
            settings.applyCalibration(pixels, width, height, stride);
        if (settings.adjustsLight() || settings.adjustsColor() || clipping)
            adjust_camera_raw(pixels, w, h, s, gain.red, gain.green, gain.blue, settings.exposure, settings.contrast, settings.highlights,
                              settings.shadows, settings.whites, settings.blacks, settings.vibrance, settings.saturation, mode);
        if (paintColor)
            settings.applyCurveColor(pixels, width, height, stride, visualizePointColor);
        if (paintEffects) {
            if (settings.texture != 0 || settings.clarity != 0 || settings.dehaze != 0 || settings.glow != 0 || settings.vignetteAmount != 0)
                adjust_camera_raw_effects(pixels, w, h, s, settings.texture, settings.clarity, settings.dehaze, settings.glow, int(settings.glowStyle),
                                          settings.glowRange, settings.glowSpread, settings.glowWarmth, settings.vignetteAmount,
                                          settings.vignetteMidpoint, settings.vignetteRoundness, settings.vignetteFeather,
                                          settings.vignetteHighlights, int(settings.vignetteStyle), pixelScale);
            if (settings.grainAmount > 0)
                adjust_grain(pixels, w, h, s, settings.grainAmount, settings.grainKernelSize(), settings.grainRoughness, seed, 0, 0, 1 / pixelScale);
        }
        if (paintDetailOptics)
            settings.applyDetailOptics(pixels, width, height, stride, pixelScale, PixelFilter::lensStrength, sharpenMask);
    });
}

std::optional<CameraRawSettings::Balance> CameraRawSettings::neutralizeLinear(double red, double green, double blue)
{
    if (!(red > 1e-4 && green > 1e-4 && blue > 1e-4))
        return std::nullopt;
    const double a1 = temperatureGain * red, b1 = tintRedBlue * red + tintGreen * green, c1 = green - red;
    const double a2 = -temperatureGain * blue, b2 = tintRedBlue * blue + tintGreen * green, c2 = green - blue;
    const double determinant = a1 * b2 - a2 * b1;
    if (!(std::abs(determinant) > 1e-8))
        return std::nullopt;
    const double warm = (c1 * b2 - c2 * b1) / determinant, magenta = (a1 * c2 - a2 * c1) / determinant;
    if (!std::isfinite(warm) || !std::isfinite(magenta))
        return std::nullopt;
    return Balance{warm * 100, magenta * 100};
}

std::optional<CameraRawSettings::Balance> CameraRawSettings::neutralizeStraight(double red, double green, double blue)
{
    return neutralizeLinear(decode(red), decode(green), decode(blue));
}

std::optional<CameraRawSettings::Balance> CameraRawSettings::autoBalance(const QImage &image)
{
    if (image.isNull())
        return std::nullopt;
    const QImage pixels = drawn(image);
    double red = 0, green = 0, blue = 0, count = 0;
    for (int y = 0; y < pixels.height(); ++y) {
        const uchar *row = pixels.constScanLine(y);
        for (int x = 0; x < pixels.width(); ++x) {
            const uchar *pixel = row + x * 4;
            const double alpha = pixel[3];
            if (alpha == 0)
                continue;
            red += decode(std::min(1.0, pixel[0] / alpha));
            green += decode(std::min(1.0, pixel[1] / alpha));
            blue += decode(std::min(1.0, pixel[2] / alpha));
            count += 1;
        }
    }
    if (count == 0)
        return std::nullopt;
    return neutralizeLinear(red / count, green / count, blue / count);
}

double CameraRawScope::peak() const
{
    return std::max({LevelsHistogramDisplay::scale(red), LevelsHistogramDisplay::scale(green), LevelsHistogramDisplay::scale(blue)});
}

std::optional<CameraRawScope> CameraRawScope::make(const QImage &image)
{
    if (image.isNull())
        return std::nullopt;
    const QImage pixels = drawn(image);
    std::array<double, binCount * 4> bins{};
    // Row by row: Qt may pad rows; the kernel counts.
    for (int y = 0; y < pixels.height(); ++y)
        levels_histogram(pixels.constScanLine(y), nullptr, size_t(pixels.width()), bins.data());
    CameraRawScope scope;
    std::copy_n(bins.begin() + binCount, binCount, scope.red.begin());
    std::copy_n(bins.begin() + 2 * binCount, binCount, scope.green.begin());
    std::copy_n(bins.begin() + 3 * binCount, binCount, scope.blue.begin());
    for (int y = 0; y < pixels.height(); ++y) {
        const uchar *row = pixels.constScanLine(y);
        for (int x = 0; x < pixels.width(); ++x) {
            const uchar *pixel = row + x * 4;
            const double alpha = pixel[3];
            if (alpha == 0)
                continue;
            const double r = std::min(1.0, pixel[0] / alpha), g = std::min(1.0, pixel[1] / alpha), b = std::min(1.0, pixel[2] / alpha);
            const double high = std::max({r, g, b}), low = std::min({r, g, b}), chroma = high - low;
            if (!(chroma > 1e-4 && high > 1e-4))
                continue;
            double hue = high == r ? (g - b) / chroma : high == g ? 2 + (b - r) / chroma : 4 + (r - g) / chroma;
            // A negative hue needs no wrap: the angle turns round.
            hue /= 6;
            const double angle = hue * 2 * std::numbers::pi, reach = chroma / high;
            const double plotX = 0.5 + std::cos(angle) * reach * 0.48, plotY = 0.5 + std::sin(angle) * reach * 0.48;
            const int column = std::min(scopeSide - 1, std::max(0, int(plotX * scopeSide)));
            const int line = std::min(scopeSide - 1, std::max(0, int(plotY * scopeSide)));
            scope.vectorscope[size_t(line * scopeSide + column)] += alpha / 255;
        }
    }
    return scope;
}

QImage CameraRawScope::overlay(const QImage &image, bool shadows, bool highlights)
{
    if (!shadows && !highlights)
        return image;
    return ImageAdjustmentPixels::run(image, [&](uchar *pixels, int width, int height, qsizetype stride) {
        adjust_camera_raw_clip_overlay(pixels, size_t(width), size_t(height), size_t(stride), shadows ? 1 : 0, highlights ? 1 : 0);
    });
}

std::pair<QImage, CameraRawScope> CameraRawScope::preview(const FilterJob &job)
{
    FilterJob grade = job;
    grade.cameraRawClipping = std::nullopt;
    grade.showsShadowClipping = false;
    grade.showsHighlightClipping = false;
    grade.visualizesPointColor = -1;
    grade.showsSharpenMask = false;
    const QImage graded = PixelFilter::run(grade);
    const CameraRawScope scope = make(graded).value_or(CameraRawScope());
    // Swift leaves the point colour's view out; shown here.
    if (job.cameraRawClipping || job.showsSharpenMask || job.visualizesPointColor >= 0)
        return {PixelFilter::run(job), scope};
    if (job.showsShadowClipping || job.showsHighlightClipping)
        return {overlay(graded, job.showsShadowClipping, job.showsHighlightClipping), scope};
    return {graded, scope};
}

int CameraRawPanel::pointColorVisualizeIndex(const CameraRawSettings &settings) const
{
    const std::vector<CameraRawPointColor> &points = settings.mixer.points;
    if (pointIndex < 0 || size_t(pointIndex) >= points.size() || !points[size_t(pointIndex)].visualize)
        return -1;
    return pointIndex;
}
