#include "Rendering/BrushCoverage.h"
#include "Rendering/PoolMap.h"
#include "Document/BrushStroke.h"
#include "IO/ImageExporter.h"
#include <QtConcurrent>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>

namespace {
// The shader's uniforms.
struct Uniforms {
    float a, b, c, d;
    float originX, originY, radius, hardness;
    float canvasWidth, canvasHeight, antialias, spacing;
};

float segmentDistanceSquared(float px, float py, const BrushSegment &s)
{
    const float vx = s.x1 - s.x0, vy = s.y1 - s.y0;
    const float t = std::clamp(((px - s.x0) * vx + (py - s.y0) * vy) / std::max(vx * vx + vy * vy, 1e-12f), 0.0f, 1.0f);
    const float dx = px - (s.x0 + t * vx), dy = py - (s.y0 + t * vy);
    return dx * dx + dy * dy;
}

// The hard tip's antialiased silhouette, as the shader draws it.
float hardCoverage(float distanceSquared, const Uniforms &u)
{
    return std::clamp((u.radius - std::sqrt(distanceSquared)) / u.antialias + 0.5f, 0.0f, 1.0f);
}

// The optical density at the solid core: coverage capped.
const float solidDensity = -std::log(0.001f);

// The shader's soft falloff as optical density, float for float.
float tipDensity(float distanceSquared, const Uniforms &u)
{
    const float t = std::clamp((std::sqrt(distanceSquared) / u.radius - u.hardness) / (1.0f - u.hardness), 0.0f, 1.0f);
    // The solid and empty ends, as the formula yields them.
    if (t == 0.0f)
        return solidDensity;
    if (t == 1.0f)
        return 0.0f;
    const float coverage = std::max(0.0f, (std::exp(-2.5f * t * t) - std::exp(-2.5f)) / (1.0f - std::exp(-2.5f)));
    // Optical density adds; coverage is 1 − exp(−density).
    return -std::log(std::max(1.0f - coverage, 0.001f));
}

// Table steps over d²/r², from hardness² out to the rim.
constexpr int densitySteps = 8192;

// A soft tip's density, in radii; solid within its hardness.
std::shared_ptr<const std::vector<float>> densityTable(float hardness)
{
    static std::mutex lock;
    static float cached = -1;
    static std::shared_ptr<const std::vector<float>> table;
    const std::lock_guard<std::mutex> guard(lock);
    if (table && cached == hardness)
        return table;
    Uniforms unit{};
    unit.radius = 1;
    unit.hardness = hardness;
    const float start = hardness * hardness;
    auto values = std::make_shared<std::vector<float>>(size_t(densitySteps) + 1);
    for (int step = 0; step <= densitySteps; ++step)
        (*values)[size_t(step)] = tipDensity(start + (1 - start) * float(step) / densitySteps, unit);
    cached = hardness;
    table = values;
    return table;
}

// Linear between steps; `core` clamps for the solid centre.
template <bool core> float tableDensity(const std::vector<float> &table, float beyondStart, float scale)
{
    float position = std::min(beyondStart * scale, float(densitySteps));
    if constexpr (core)
        position = std::max(position, 0.0f);
    const size_t index = size_t(std::min(int(position), densitySteps - 1));
    // One checked read: the upper step; the lower sits below.
    const float *const upper = &table[index + 1];
    return upper[-1] + (position - float(index)) * (upper[0] - upper[-1]);
}

// A soft tip's density: from the table, or by formula.
struct Fade {
    const std::vector<float> &table;
    // Where the table begins, h²r², and its steps per pixel².
    float start;
    float scale;
    // The table's steps finer than float can tell apart.
    bool exact;
};

// Eight-point Gauss–Legendre quadrature, clipped to the tip.
template <typename Density> float quadrature(float midpoint, float halfLength, float projection, const Density &density)
{
    constexpr float nodes[4] = {0.1834346425f, 0.5255324099f, 0.7966664774f, 0.9602898565f};
    constexpr float weights[4] = {0.3626837834f, 0.3137066459f, 0.2223810345f, 0.1012285363f};
    float integral = 0.0f;
    for (int i = 0; i < 4; ++i) {
        // The shader's order: far along a stroke, float rounds differently.
        const float a = midpoint - halfLength * nodes[i] - projection, b = midpoint + halfLength * nodes[i] - projection;
        integral += weights[i] * (density(a * a) + density(b * b));
    }
    return integral;
}

float segmentDensity(float px, float py, const BrushSegment &s, const Uniforms &u, const Fade &fade)
{
    const float vx = s.x1 - s.x0, vy = s.y1 - s.y0;
    const float length = std::sqrt(vx * vx + vy * vy);
    if (length < 1e-6f) {
        const float distanceSquared = (px - s.x0) * (px - s.x0) + (py - s.y0) * (py - s.y0);
        return fade.exact ? tipDensity(distanceSquared, u) : tableDensity<true>(fade.table, distanceSquared - fade.start, fade.scale);
    }
    const float dx = vx / length, dy = vy / length;
    const float projection = (px - s.x0) * dx + (py - s.y0) * dy;
    const float perpX = px - s.x0 - projection * dx, perpY = py - s.y0 - projection * dy;
    const float perpendicularSquared = perpX * perpX + perpY * perpY;
    const float radiusSquared = u.radius * u.radius;
    if (perpendicularSquared >= radiusSquared)
        return 0.0f;
    const float reach = std::sqrt(radiusSquared - perpendicularSquared);
    const float lo = std::max(0.0f, projection - reach), hi = std::min(length, projection + reach);
    if (hi <= lo)
        return 0.0f;
    const float midpoint = (lo + hi) * 0.5f, halfLength = (hi - lo) * 0.5f;
    // The table counts from h²r², so shift once per segment.
    const float shifted = perpendicularSquared - fade.start;
    float integral;
    if (fade.exact)
        integral = quadrature(midpoint, halfLength, projection, [&](float along) { return tipDensity(perpendicularSquared + along, u); });
    else if (shifted < 0)
        // Only a segment through the solid core needs the clamp.
        integral = quadrature(midpoint, halfLength, projection, [&](float along) { return tableDensity<true>(fade.table, shifted + along, fade.scale); });
    else
        integral = quadrature(midpoint, halfLength, projection, [&](float along) { return tableDensity<false>(fade.table, shifted + along, fade.scale); });
    return integral * halfLength / u.spacing;
}

// A tile's rows, from bits fetched before the threads.
struct Band {
    const BrushCoverage::Work *work;
    uchar *bits;
    qsizetype stride;
    int top;
    int bottom;
};

// Permanent paint and the replaceable tail stay apart.
void renderBand(const Band &band, const Uniforms &u, const std::vector<BrushSegment> &segments, size_t settledCount, const std::vector<float> &table)
{
    const BrushCoverage::Work &work = *band.work;
    const int width = int(work.rect.width());
    // The table's span, h²r² to r², in pixels squared.
    const float inner = u.hardness * u.hardness;
    const Fade fade{table, inner * u.radius * u.radius, densitySteps / ((1 - inner) * u.radius * u.radius),
                    (1 - inner) / densitySteps < std::numeric_limits<float>::epsilon()};
    for (int y = band.top; y < band.bottom; ++y) {
        uchar *row = band.bits + y * band.stride;
        for (int x = 0; x < width; ++x) {
            float &permanent = work.tile->permanent[size_t(y) * width + x];
            const float lx = x + 0.5f, ly = y + 0.5f;
            const float px = u.originX + lx * u.a + ly * u.c, py = u.originY + lx * u.b + ly * u.d;
            if (px < 0 || py < 0 || px >= u.canvasWidth || py >= u.canvasHeight) {
                row[x] = 0;
                continue;
            }
            if (u.hardness >= 1.0f) {
                // Hard tips keep their antialiased silhouette.
                float settled = INFINITY, tail = INFINITY;
                for (size_t i = 0; i < settledCount; ++i)
                    settled = std::min(settled, segmentDistanceSquared(px, py, segments[i]));
                for (size_t i = settledCount; i < segments.size(); ++i)
                    tail = std::min(tail, segmentDistanceSquared(px, py, segments[i]));
                const float value = std::max(permanent, hardCoverage(settled, u));
                permanent = value;
                row[x] = uchar(std::round(255.0f * std::max(value, hardCoverage(tail, u))));
            } else {
                float value = permanent, tail = 0.0f;
                for (size_t i = 0; i < settledCount; ++i)
                    value += segmentDensity(px, py, segments[i], u, fade);
                for (size_t i = settledCount; i < segments.size(); ++i)
                    tail += segmentDensity(px, py, segments[i], u, fade);
                permanent = std::min(value, 20.0f);
                row[x] = uchar(std::round(255.0f * (1.0f - std::exp(-std::min(value + tail, 20.0f)))));
            }
        }
    }
}
}

BrushCoverage::Tile BrushCoverage::tile(int width, int height)
{
    try {
        return Tile{std::vector<float>(size_t(width) * size_t(height), 0.0f)};
    } catch (const std::bad_alloc &) {
        throw ExportError(ExportError::Kind::render);
    }
}

void BrushCoverage::render(const std::vector<Work> &tiles, const std::vector<BrushSegment> &settled, const std::vector<BrushSegment> &tail,
                           const QTransform &mapping, const BrushSettings &settings, QSizeF canvas)
{
    if (tiles.empty())
        return;
    std::vector<BrushSegment> segments = settled;
    segments.insert(segments.end(), tail.begin(), tail.end());
    Uniforms base{};
    base.a = float(mapping.m11());
    base.b = float(mapping.m12());
    base.c = float(mapping.m21());
    base.d = float(mapping.m22());
    base.radius = float(settings.diameter / 2);
    base.hardness = float(settings.hardness);
    base.canvasWidth = float(canvas.width());
    base.canvasHeight = float(canvas.height());
    base.antialias = float(std::max(0.001, std::min(std::hypot(mapping.m11(), mapping.m12()), std::hypot(mapping.m21(), mapping.m22()))));
    base.spacing = float(std::max(0.25, settings.diameter * BrushStroke::spacingFraction(settings.hardness)));
    const std::shared_ptr<const std::vector<float>> table = densityTable(base.hardness);
    // Row bands keep every core busy, however few tiles changed.
    std::vector<Band> bands;
    for (const Work &item : tiles) {
        uchar *const bits = item.context->bits();
        const int height = int(item.rect.height());
        for (int top = 0; top < height; top += 32)
            bands.push_back({&item, bits, item.context->bytesPerLine(), top, std::min(height, top + 32)});
    }
    PoolMap::blocking(bands, [&](const Band &band) {
        Uniforms u = base;
        const QPointF origin = mapping.map(band.work->rect.topLeft());
        u.originX = float(origin.x());
        u.originY = float(origin.y());
        renderBand(band, u, segments, settled.size(), *table);
    });
}
