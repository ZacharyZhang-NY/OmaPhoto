#include "Document/CameraRawColor.h"
#include "Document/CameraRaw.h"
#include "Document/ImageAdjustments.h"
#include <algorithm>
#include <cmath>
extern "C" {
#include "AdjustPixels.h"
}

namespace {
using ImageAdjustmentPixels::clamp;

double point(double x, const std::vector<CurvePoint> &points)
{
    if (points.size() < 2)
        return x;
    CurvesSettings curve;
    curve.channels[0].clear();
    for (const CurvePoint &each : points)
        curve.channels[0].push_back({each.x * 255, each.y * 255});
    return curve.value(x * 255, 0) / 255;
}

// Swift's repair: sorted, ends pinned, inner points a hundredth apart.
std::vector<CurvePoint> repair(const std::vector<CurvePoint> &points)
{
    std::vector<CurvePoint> sorted;
    std::copy_if(points.begin(), points.end(), std::back_inserter(sorted), [](const CurvePoint &each) { return std::isfinite(each.x) && std::isfinite(each.y); });
    std::stable_sort(sorted.begin(), sorted.end(), [](const CurvePoint &left, const CurvePoint &right) { return left.x < right.x; });
    if (sorted.size() < 2)
        return CameraRawCurveSettings::linear();
    const auto unit = [](double value) { return std::min(1.0, std::max(0.0, value)); };
    std::vector<CurvePoint> kept{{0, unit(sorted.front().y)}};
    for (size_t index = 1; index + 1 < sorted.size(); ++index) {
        const double x = std::min(0.99, std::max(0.01, sorted[index].x));
        if (!(x > kept.back().x + 0.01))
            continue;
        kept.push_back({x, unit(sorted[index].y)});
    }
    kept.push_back({1, unit(sorted.back().y)});
    return kept;
}

std::vector<CurvePoint> &pointsOf(CameraRawCurveSettings &curve, CameraRawPointChannel channel)
{
    switch (channel) {
    case CameraRawPointChannel::rgb: return curve.rgb;
    case CameraRawPointChannel::red: return curve.red;
    case CameraRawPointChannel::green: return curve.green;
    case CameraRawPointChannel::blue: return curve.blue;
    }
    throw std::logic_error("unknown point channel");
}
}

std::vector<CurvePoint> CameraRawCurveSettings::linear()
{
    return {{0, 0}, {1, 1}};
}

std::vector<CurvePoint> CameraRawCurveSettings::mediumContrast()
{
    return {{0, 0}, {0.25, 0.18}, {0.75, 0.82}, {1, 1}};
}

std::vector<CurvePoint> CameraRawCurveSettings::strongContrast()
{
    return {{0, 0}, {0.25, 0.10}, {0.75, 0.90}, {1, 1}};
}

bool CameraRawCurveSettings::isLinear(const std::vector<CurvePoint> &points)
{
    return points == linear();
}

bool CameraRawCurveSettings::adjusts() const
{
    return shadows != 0 || darks != 0 || lights != 0 || highlights != 0 || refineSaturation != 0 || !isLinear(rgb) || !isLinear(red)
        || !isLinear(green) || !isLinear(blue);
}

double CameraRawCurveSettings::parametric(double tone) const
{
    const double shadow = shadowSplit / 100, dark = darkSplit / 100, light = lightSplit / 100;
    double amount, lo, hi;
    if (tone < shadow)
        amount = shadows, lo = 0, hi = shadow;
    else if (tone < dark)
        amount = darks, lo = shadow, hi = dark;
    else if (tone < light)
        amount = lights, lo = dark, hi = light;
    else
        amount = highlights, lo = light, hi = 1;
    const double span = std::max(0.02, hi - lo);
    const double weight = 1 - std::abs(tone - (lo + hi) / 2) / (span / 2);
    // A tone in its region weighs 0 to 1, unfloored.
    return std::min(1.0, std::max(0.0, tone + (amount / 100) * weight * 0.22));
}

std::vector<float> CameraRawCurveSettings::lumaTable() const
{
    std::vector<float> table(256);
    for (int index = 0; index < 256; ++index)
        table[size_t(index)] = float(point(parametric(index / 255.0), rgb));
    return table;
}

std::vector<float> CameraRawCurveSettings::channelTable(const std::vector<CurvePoint> &points) const
{
    std::vector<float> table(256);
    for (int index = 0; index < 256; ++index)
        table[size_t(index)] = float(point(index / 255.0, points));
    return table;
}

CameraRawCurveSettings CameraRawCurveSettings::nudged(CameraRawPointChannel channel, double tone, double delta) const
{
    CameraRawCurveSettings result = *this;
    std::vector<CurvePoint> &points = pointsOf(result, channel);
    // Swift's `min(by:)`: the first nearest point.
    size_t nearest = 0;
    for (size_t index = 1; index < points.size(); ++index)
        if (std::abs(points[index].x - tone) < std::abs(points[nearest].x - tone))
            nearest = index;
    points.at(nearest).y = std::min(1.0, std::max(0.0, points[nearest].y + delta));
    return result;
}

double &CameraRawCurveSettings::region(double tone)
{
    if (tone < shadowSplit / 100)
        return shadows;
    if (tone < darkSplit / 100)
        return darks;
    if (tone < lightSplit / 100)
        return lights;
    return highlights;
}

CameraRawCurveSettings CameraRawCurveSettings::normalized() const
{
    CameraRawCurveSettings result = *this;
    result.shadows = clamp(shadows, -100, 100, 0);
    result.darks = clamp(darks, -100, 100, 0);
    result.lights = clamp(lights, -100, 100, 0);
    result.highlights = clamp(highlights, -100, 100, 0);
    result.refineSaturation = clamp(refineSaturation, -100, 100, 0);
    result.shadowSplit = clamp(shadowSplit, 5, 90, 25);
    result.darkSplit = clamp(darkSplit, result.shadowSplit + 2, 95, 50);
    result.lightSplit = clamp(lightSplit, result.darkSplit + 2, 98, 75);
    result.rgb = repair(rgb);
    result.red = repair(red);
    result.green = repair(green);
    result.blue = repair(blue);
    return result;
}

const std::array<QString, 8> CameraRawMixerSettings::names{QStringLiteral("Reds"),   QStringLiteral("Oranges"), QStringLiteral("Yellows"),
                                                           QStringLiteral("Greens"), QStringLiteral("Aquas"),   QStringLiteral("Blues"),
                                                           QStringLiteral("Purples"), QStringLiteral("Magentas")};

bool CameraRawMixerSettings::adjusts() const
{
    const auto moved = [](const std::array<double, 8> &values) { return std::any_of(values.begin(), values.end(), [](double value) { return value != 0; }); };
    return moved(hue) || moved(saturation) || moved(luminance)
        || std::any_of(points.begin(), points.end(), [](const CameraRawPointColor &each) {
               return each.hueShift != 0 || each.saturationShift != 0 || each.luminanceShift != 0;
           });
}

std::array<double, 8> CameraRawMixerSettings::weights(double degrees)
{
    std::array<double, 8> result{};
    for (size_t index = 0; index < 8; ++index) {
        double distance = std::abs(degrees - centers[index]);
        if (distance > 180)
            distance = 360 - distance;
        result[index] = std::max(0.0, 1 - distance / 40);
    }
    return result;
}

std::vector<float> CameraRawMixerSettings::mixerFloats() const
{
    std::vector<float> result;
    for (const std::array<double, 8> *values : {&hue, &saturation, &luminance})
        for (const double value : *values)
            result.push_back(float(value / 100));
    return result;
}

std::vector<float> CameraRawMixerSettings::pointFloats() const
{
    std::vector<float> result;
    for (const CameraRawPointColor &each : points)
        for (const double value : {each.hue / 360, each.saturation, each.luminance, each.hueShift / 100, each.saturationShift / 100,
                                   each.luminanceShift / 100, each.hueRange / 360, each.saturationRange, each.luminanceRange})
            result.push_back(float(value));
    return result;
}

CameraRawMixerSettings CameraRawMixerSettings::normalized() const
{
    CameraRawMixerSettings result = *this;
    for (std::array<double, 8> *values : {&result.hue, &result.saturation, &result.luminance})
        for (double &value : *values)
            value = clamp(value, -100, 100, 0);
    result.points.clear();
    for (size_t index = 0; index < std::min<size_t>(points.size(), 8); ++index)
        result.points.push_back(points[index].normalized());
    return result;
}

CameraRawPointColor CameraRawPointColor::normalized() const
{
    CameraRawPointColor result = *this;
    result.hue = clamp(hue, 0, 360, 0);
    result.saturation = clamp(saturation, 0, 1, 0);
    result.luminance = clamp(luminance, 0, 1, 0);
    result.hueShift = clamp(hueShift, -100, 100, 0);
    result.saturationShift = clamp(saturationShift, -100, 100, 0);
    result.luminanceShift = clamp(luminanceShift, -100, 100, 0);
    result.hueRange = clamp(hueRange, 5, 180, 30);
    result.saturationRange = clamp(saturationRange, 0.05, 1, 0.4);
    result.luminanceRange = clamp(luminanceRange, 0.05, 1, 0.4);
    return result;
}

CameraRawGradeWheel CameraRawGradeWheel::normalized() const
{
    return {clamp(hue, 0, 360, 0), clamp(saturation, 0, 100, 0), clamp(luminance, -100, 100, 0)};
}

std::array<CameraRawGradeWheel, 4> CameraRawGradingSettings::wheels() const
{
    return {shadows, midtones, highlights, global};
}

bool CameraRawGradingSettings::adjusts() const
{
    const std::array<CameraRawGradeWheel, 4> all = wheels();
    return std::any_of(all.begin(), all.end(), [](const CameraRawGradeWheel &wheel) { return wheel.saturation != 0 || wheel.luminance != 0; });
}

std::vector<float> CameraRawGradingSettings::gradeFloats() const
{
    std::vector<float> result;
    for (const CameraRawGradeWheel &wheel : wheels())
        for (const double value : {wheel.hue / 360, wheel.saturation / 100, wheel.luminance / 100})
            result.push_back(float(value));
    return result;
}

CameraRawGradingSettings CameraRawGradingSettings::normalized() const
{
    return {shadows.normalized(), midtones.normalized(), highlights.normalized(), global.normalized(), clamp(blending, 0, 100, 50),
            clamp(balance, -100, 100, 0)};
}

void CameraRawSettings::applyCurveColor(uchar *pixels, int width, int height, qsizetype stride, int visualize) const
{
    const CameraRawCurveSettings shape = curve.normalized();
    const CameraRawMixerSettings mixing = mixer.normalized();
    const CameraRawGradingSettings grade = grading.normalized();
    const std::vector<float> luma = shape.lumaTable(), redTable = shape.channelTable(shape.red), greenTable = shape.channelTable(shape.green),
                             blueTable = shape.channelTable(shape.blue), mixerFloats = mixing.mixerFloats(), pointFloats = mixing.pointFloats(),
                             gradeFloats = grade.gradeFloats();
    adjust_camera_raw_curve_color(pixels, size_t(width), size_t(height), size_t(stride), luma.data(), redTable.data(), greenTable.data(),
                                  blueTable.data(), shape.refineSaturation / 100, mixerFloats.data(), int(mixing.points.size()), pointFloats.data(),
                                  gradeFloats.data(), grade.blending / 100, grade.balance / 100, visualize);
}
