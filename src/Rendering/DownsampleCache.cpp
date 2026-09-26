#include "Rendering/DownsampleCache.h"
#include "Rendering/PoolMap.h"
#include "Document/BrushStroke.h"
#include <QThreadPool>
#include <QtConcurrent>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>

extern "C" {
#include "AdjustPixels.h"
}

namespace {
constexpr int taps = 12;

// Lanczos-3 at half scale, sampled between source pixels.
const std::array<float, taps> &kernel()
{
    static const std::array<float, taps> weights = [] {
        std::array<double, taps> lobes{};
        double sum = 0;
        for (int tap = 0; tap < taps; ++tap) {
            const double x = std::numbers::pi * (tap - 5.5) / 2;
            lobes[tap] = std::sin(x) / x * std::sin(x / 3) / (x / 3);
            sum += lobes[tap];
        }
        std::array<float, taps> result{};
        for (int tap = 0; tap < taps; ++tap)
            result[tap] = float(lobes[tap] / sum);
        return result;
    }();
    return weights;
}

// A mask's row, one channel: scalar, clamped at the edges.
void filterMaskRow(const uchar *row, int width, float *line, int outWidth)
{
    const float *const weights = kernel().data();
    for (int x = 0; x < outWidth; ++x) {
        float sum = 0;
        for (int tap = 0; tap < taps; ++tap)
            sum += weights[tap] * float(row[std::clamp(2 * x - 5 + tap, 0, width - 1)]);
        line[x] = sum;
    }
}

// Four channels in one vector: each lane sums as before.
using Float4 = float __attribute__((vector_size(16)));

void filterColorRow(const uchar *row, int width, float *line, int outWidth, float *floats)
{
    for (int index = 0; index < width * 4; ++index)
        floats[index] = float(row[index]);
    const float *const weights = kernel().data();
    for (int x = 0; x < outWidth; ++x) {
        Float4 sum = {0, 0, 0, 0};
        for (int tap = 0; tap < taps; ++tap) {
            Float4 pixel;
            std::memcpy(&pixel, floats + std::clamp(2 * x - 5 + tap, 0, width - 1) * 4, sizeof pixel);
            sum += weights[tap] * pixel;
        }
        std::memcpy(line + x * 4, &sum, sizeof sum);
    }
}

// One band's scratch: twelve rows, sums, a source row.
struct Band {
    int first;
    int last;
    float *lines;
    float *sums;
    float *floats;
};

struct Target {
    uchar *bits;
    qsizetype stride;
    int width;
};

template <int channels> void filterBand(const QImage &source, const Target &target, const Band &band)
{
    const float *const weights = kernel().data();
    const int samples = target.width * channels;
    std::array<int, taps> held;
    held.fill(-1);
    for (int y = band.first; y < band.last; ++y) {
        std::array<const float *, taps> rows;
        for (int tap = 0; tap < taps; ++tap) {
            const int sourceRow = std::clamp(2 * y - 5 + tap, 0, source.height() - 1);
            const int slot = sourceRow % taps;
            float *line = band.lines + size_t(slot) * samples;
            if (held[slot] != sourceRow) {
                if (channels == 4)
                    filterColorRow(source.constScanLine(sourceRow), source.width(), line, target.width, band.floats);
                else
                    filterMaskRow(source.constScanLine(sourceRow), source.width(), line, target.width);
                held[slot] = sourceRow;
            }
            rows[tap] = line;
        }
        // Tap by tap over whole rows: the same order, contiguous.
        float *const sum = band.sums;
        std::fill(sum, sum + samples, 0.0f);
        for (int tap = 0; tap < taps; ++tap) {
            const float weight = weights[tap];
            const float *const row = rows.data()[tap];
            for (int sample = 0; sample < samples; ++sample)
                sum[sample] += weight * row[sample];
        }
        uchar *out = target.bits + y * target.stride;
        for (int sample = 0; sample < samples; ++sample)
            out[sample] = uchar(std::clamp(int(sum[sample] + 0.5f), 0, 255));
    }
}

void filterBand(const QImage &source, const Target &target, int channels, const Band &band)
{
    if (channels == 4)
        filterBand<4>(source, target, band);
    else
        filterBand<1>(source, target, band);
}

// False when the scratch rows cannot be allocated.
bool lanczosHalve(const QImage &source, QImage &destination, int channels, bool threaded)
{
    const int height = destination.height();
    const int count = threaded ? std::clamp(height / 32, 1, QThreadPool::globalInstance()->maxThreadCount()) : 1;
    const size_t samples = size_t(destination.width()) * channels;
    // Workers allocate nothing: a failure throws here, caught below.
    const size_t bandFloats = size_t(taps) * samples + samples + size_t(source.width()) * channels;
    // Writable bits are fetched once: scanLine() is not thread-safe.
    const Target target{destination.bits(), destination.bytesPerLine(), destination.width()};
    try {
        std::vector<float> scratch(bandFloats * count);
        std::vector<Band> bands;
        for (int index = 0; index < count; ++index) {
            float *const lines = scratch.data() + bandFloats * index;
            bands.push_back({height * index / count, height * (index + 1) / count, lines, lines + taps * samples, lines + taps * samples + samples});
        }
        if (threaded)
            PoolMap::blocking(bands, [&](const Band &band) { filterBand(source, target, channels, band); });
        else
            filterBand(source, target, channels, bands.front());
    } catch (const std::bad_alloc &) {
        return false;
    }
    return true;
}
}

DownsampleCache::DownsampleCache(qint64 pixelBudget) : m_pixelBudget(pixelBudget) {}

DownsampleCache &DownsampleCache::shared()
{
    static DownsampleCache cache;
    return cache;
}

qint64 DownsampleCache::Entry::pixels() const
{
    qint64 total = 0;
    for (const QImage &level : levels)
        total += qint64(level.width()) * level.height();
    return total;
}

int DownsampleCache::level(double factor)
{
    if (!std::isfinite(factor) || factor <= 0 || factor >= 0.5)
        return 0;
    return std::min(maxLevel, int(std::floor(std::log2(1 / factor))));
}

QImage DownsampleCache::imageDrawnAt(const QImage &image, double factor)
{
    return imageAtLevel(image, level(factor)).image;
}

DownsampleCache::Reduced DownsampleCache::imageAtLevel(const QImage &image, int wanted)
{
    if (wanted < 1 || (image.width() <= 1 && image.height() <= 1))
        return {image, 0};
    const qint64 key = image.cacheKey();
    std::vector<QImage> levels;
    {
        const std::lock_guard<std::mutex> guard(m_lock);
        m_clock += 1;
        if (const auto found = m_entries.find(key); found != m_entries.end())
            levels = found->second.levels;
    }
    while (int(levels.size()) < wanted) {
        const QImage &previous = levels.empty() ? image : levels.back();
        if (previous.width() <= 1 && previous.height() <= 1)
            break;
        const std::optional<QImage> next = halve(previous);
        if (!next)
            break;
        levels.push_back(*next);
    }
    if (levels.empty())
        return {image, 0};
    {
        const std::lock_guard<std::mutex> guard(m_lock);
        const auto found = m_entries.find(key);
        if (found != m_entries.end() && found->second.levels.size() >= levels.size())
            found->second.lastUse = m_clock;
        else
            m_entries.insert_or_assign(key, Entry{image, levels, m_clock});
        evict(key);
    }
    const int applied = std::min(wanted, int(levels.size()));
    return {levels[applied - 1], applied};
}

void DownsampleCache::evict(qint64 keeping)
{
    qint64 total = 0;
    for (const auto &[key, entry] : m_entries)
        total += entry.pixels();
    while (total > m_pixelBudget) {
        auto oldest = m_entries.end();
        for (auto entry = m_entries.begin(); entry != m_entries.end(); ++entry) {
            if (entry->first != keeping && (oldest == m_entries.end() || entry->second.lastUse < oldest->second.lastUse))
                oldest = entry;
        }
        if (oldest == m_entries.end())
            return;
        total -= oldest->second.pixels();
        m_entries.erase(oldest);
    }
}

std::optional<QImage> DownsampleCache::halve(const QImage &image, bool threaded)
{
    const bool mask = image.format() == QImage::Format_Grayscale8;
    const int width = (image.width() + 1) / 2, height = (image.height() + 1) / 2;
    // Color fades into transparent padding wherever an image is cut.
    const int pad = mask ? 0 : 8;
    const int paddedWidth = width * 2 + pad * 2, paddedHeight = height * 2 + pad * 2;
    const QImage::Format format = mask ? QImage::Format_Grayscale8 : QImage::Format_RGBA8888_Premultiplied;
    QImage source(paddedWidth, paddedHeight, format), destination(paddedWidth / 2, paddedHeight / 2, format);
    if (source.isNull() || destination.isNull())
        return std::nullopt;
    source.fill(0);
    {
        QPainter painter(&source);
        BrushRaster::draw(image, QRectF(pad, pad, image.width(), image.height()), painter);
    }
    if (mask) {
        // An odd last column and row repeat.
        for (int y = 0; y < image.height() && paddedWidth > image.width(); ++y)
            source.scanLine(y)[image.width()] = source.constScanLine(y)[image.width() - 1];
        if (paddedHeight > image.height())
            std::copy_n(source.constScanLine(image.height() - 1), paddedWidth, source.scanLine(image.height()));
    }
    if (!lanczosHalve(source, destination, mask ? 1 : 4, threaded))
        return std::nullopt;
    if (mask)
        return destination;
    rgba_clamp_premultiplied(destination.bits(), size_t(destination.width()) * destination.height());
    const QImage cropped = destination.copy(pad / 2, pad / 2, width, height);
    return cropped.isNull() ? std::nullopt : std::optional(cropped);
}
