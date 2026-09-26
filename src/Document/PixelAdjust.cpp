#include "Document/PixelAdjust.h"
#include "Rendering/PoolMap.h"
#include "Document/BrushStroke.h"
#include "IO/ImageExporter.h"
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <memory>
#include <new>
#include <numeric>
#include <stdexcept>

namespace {
using Float4 = float __attribute__((vector_size(16)));

// A pixel in floats: gray has one lane, colour four.
template <typename Pixel> Pixel load(const uchar *bytes);
template <> float load<float>(const uchar *bytes)
{
    return float(bytes[0]);
}
template <> Float4 load<Float4>(const uchar *bytes)
{
    return Float4{float(bytes[0]), float(bytes[1]), float(bytes[2]), float(bytes[3])};
}

// Rounded once, as a render to eight bits does.
void store(float value, uchar *bytes)
{
    bytes[0] = uchar(std::min(255.0f, std::max(0.0f, value) + 0.5f));
}
void store(Float4 value, uchar *bytes)
{
    for (int c = 0; c < 4; ++c)
        store(value[c], bytes + c);
}

// Normalised weights from the centre out, three sigmas long.
std::vector<float> gaussian(double sigma)
{
    const int reach = int(std::ceil(3 * sigma));
    std::vector<double> exact(size_t(reach) + 1);
    double total = 0;
    for (int i = 0; i <= reach; ++i) {
        exact[size_t(i)] = std::exp(-double(i) * i / (2 * sigma * sigma));
        total += i == 0 ? exact[0] : 2 * exact[size_t(i)];
    }
    std::vector<float> weights;
    for (const double weight : exact)
        weights.push_back(float(weight / total));
    return weights;
}

// Rows for a parallel map; no memory: a render error.
std::vector<int> rowNumbers(int height)
{
    try {
        std::vector<int> rows(static_cast<size_t>(height));
        std::iota(rows.begin(), rows.end(), 0);
        return rows;
    } catch (const std::bad_alloc &) {
        throw ExportError(ExportError::Kind::render);
    }
}

// Columns a block at a time keep the taps cached.
constexpr int block = 64;

template <typename Pixel> void blur(const QImage &image, const std::vector<float> &weights, bool clamped, Pixel *across, QImage &result)
{
    const int width = image.width(), height = image.height(), reach = int(weights.size()) - 1, size = int(sizeof(Pixel) / sizeof(float));
    const uchar *const in = image.constBits();
    uchar *const out = result.bits();
    const qsizetype inStride = image.bytesPerLine(), outStride = result.bytesPerLine();
    // Past the edge: the edge itself, or nothing.
    const auto source = [&](int at, int limit) { return clamped ? std::clamp(at, 0, limit - 1) : at; };
    std::vector<int> rows = rowNumbers(height);
    PoolMap::blocking(rows, [&](int y) {
        const uchar *row = in + y * inStride;
        Pixel *line = across + size_t(y) * size_t(width);
        for (int x = 0; x < width; ++x) {
            Pixel sum = weights[0] * load<Pixel>(row + x * size);
            // Away from the edges every tap is inside: no checks.
            if (x >= reach && x + reach < width) {
                for (int i = 1; i <= reach; ++i)
                    sum += weights[size_t(i)] * (load<Pixel>(row + (x - i) * size) + load<Pixel>(row + (x + i) * size));
            } else {
                for (int i = 1; i <= reach; ++i) {
                    const int left = source(x - i, width), right = source(x + i, width);
                    if (left >= 0)
                        sum += weights[size_t(i)] * load<Pixel>(row + left * size);
                    if (right < width)
                        sum += weights[size_t(i)] * load<Pixel>(row + right * size);
                }
            }
            line[x] = sum;
        }
    });
    PoolMap::blocking(rows, [&](int y) {
        uchar *line = out + y * outStride;
        for (int start = 0; start < width; start += block) {
            const int count = std::min(block, width - start);
            Pixel sums[block];
            for (int x = 0; x < count; ++x)
                sums[x] = weights[0] * across[size_t(y) * size_t(width) + size_t(start + x)];
            for (int i = -reach; i <= reach; ++i) {
                const int from = source(y + i, height);
                if (i == 0 || from < 0 || from >= height)
                    continue;
                const Pixel *taps = across + size_t(from) * size_t(width) + size_t(start);
                const float weight = weights[size_t(std::abs(i))];
                for (int x = 0; x < count; ++x)
                    sums[x] += weight * taps[x];
            }
            for (int x = 0; x < count; ++x)
                store(sums[x], line + (start + x) * size);
        }
    });
}
}

QImage PixelAdjust::thumbnail(const QImage &image)
{
    const double factor = std::min(1.0, 96.0 / std::max(image.width(), image.height()));
    const int width = std::max(1, int(image.width() * factor)), height = std::max(1, int(image.height() * factor));
    QImage result = BrushRaster::context(width, height, false);
    QPainter painter(&result);
    BrushRaster::draw(image, QRectF(0, 0, width, height), painter);
    return result;
}

QImage PixelAdjust::coverage(const SelectionClip &selection, int width, int height, const QTransform &pixelToDocument)
{
    QImage mask = BrushRaster::context(width, height, true);
    // An empty selection covers nothing.
    if (!selection.coverage)
        return mask;
    QPainter painter(&mask);
    painter.setTransform(pixelToDocument.inverted());
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(selection.rect, *selection.coverage);
    return mask;
}

QImage PixelAdjust::blend(const QImage &adjusted, const QImage &original, const SelectionClip &selection,
                          const QTransform &pixelToDocument, bool isMask)
{
    const int width = adjusted.width(), height = adjusted.height();
    if (original.size() != adjusted.size() || adjusted.format() != original.format())
        throw std::logic_error("blend needs two images of one size and format");
    const QImage mask = coverage(selection, width, height, pixelToDocument);
    QImage result = BrushRaster::context(width, height, isMask);
    const int channels = isMask ? 1 : 4;
    for (int y = 0; y < height; ++y) {
        const uchar *top = adjusted.constScanLine(y), *bottom = original.constScanLine(y), *weight = mask.constScanLine(y);
        uchar *target = result.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const int m = weight[x];
            for (int channel = 0; channel < channels; ++channel) {
                const int index = x * channels + channel;
                target[index] = uchar((top[index] * m + bottom[index] * (255 - m) + 127) / 255);
            }
        }
    }
    return result;
}

QImage PixelAdjust::gaussianBlur(const QImage &image, double sigma, bool clamped)
{
    const bool gray = image.format() == QImage::Format_Grayscale8;
    if (!gray && image.format() != QImage::Format_RGBA8888_Premultiplied)
        throw std::logic_error("a blur takes premultiplied RGBA or gray");
    if (!(sigma > 0) || !std::isfinite(sigma))
        throw std::logic_error("a blur needs a positive sigma");
    const std::vector<float> weights = gaussian(sigma);
    const size_t pixels = size_t(image.width()) * size_t(image.height());
    QImage result = BrushRaster::context(image.width(), image.height(), gray);
    // Allocated here, where a failure throws, before the workers run.
    if (gray) {
        const std::unique_ptr<float[]> across(new (std::nothrow) float[pixels]);
        if (!across)
            throw ExportError(ExportError::Kind::render);
        blur<float>(image, weights, clamped, across.get(), result);
    } else {
        const std::unique_ptr<Float4[]> across(new (std::nothrow) Float4[pixels]);
        if (!across)
            throw ExportError(ExportError::Kind::render);
        blur<Float4>(image, weights, clamped, across.get(), result);
    }
    return result;
}

QImage PixelAdjust::motionBlur(const QImage &image, double radius, double angle)
{
    if (image.format() != QImage::Format_RGBA8888_Premultiplied)
        throw std::logic_error("a motion blur takes premultiplied RGBA");
    if (!(radius > 0) || !std::isfinite(radius) || !std::isfinite(angle))
        throw std::logic_error("a motion blur needs a finite positive radius and angle");
    // Measured on a dot: a Gaussian taper of its radius.
    const std::vector<float> weights = gaussian(radius);
    const int width = image.width(), height = image.height(), reach = int(weights.size()) - 1;
    // Core Image's y points up: counterclockwise climbs the screen.
    const double dx = std::cos(angle), dy = -std::sin(angle);
    QImage result = BrushRaster::context(width, height, false);
    const uchar *const in = image.constBits();
    uchar *const out = result.bits();
    const qsizetype inStride = image.bytesPerLine(), outStride = result.bytesPerLine();
    // Bilinear, clear past the edges, as an unclamped image reads.
    const auto sample = [&](double x, double y) {
        const double left = std::floor(x), top = std::floor(y);
        const float across = float(x - left), down = float(y - top);
        Float4 sum{0, 0, 0, 0};
        for (int j = 0; j < 2; ++j) {
            const int row = int(top) + j;
            if (row < 0 || row >= height)
                continue;
            for (int i = 0; i < 2; ++i) {
                const int column = int(left) + i;
                if (column >= 0 && column < width)
                    sum += (j ? down : 1 - down) * (i ? across : 1 - across) * load<Float4>(in + row * inStride + column * 4);
            }
        }
        return sum;
    };
    std::vector<int> rows = rowNumbers(height);
    PoolMap::blocking(rows, [&](int y) {
        uchar *line = out + y * outStride;
        for (int x = 0; x < width; ++x) {
            Float4 sum = weights[0] * load<Float4>(in + y * inStride + x * 4);
            for (int i = 1; i <= reach; ++i)
                sum += weights[size_t(i)] * (sample(x + i * dx, y + i * dy) + sample(x - i * dx, y - i * dy));
            store(sum, line + x * 4);
        }
    });
    return result;
}
