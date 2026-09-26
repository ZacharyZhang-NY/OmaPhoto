#include "Document/GuidedMatte.h"
#include "IO/ImageExporter.h"
#include <algorithm>
#include <cmath>
#include <new>

namespace {
// Floats; a failed allocation is a render error.
std::vector<float> floats(size_t count)
{
    try {
        return std::vector<float>(count);
    } catch (const std::bad_alloc &) {
        throw ExportError(ExportError::Kind::render);
    }
}

QImage checked(const QImage &image)
{
    if (image.isNull())
        throw ExportError(ExportError::Kind::render);
    return image;
}
}

std::vector<float> GuidedMatte::box(const std::vector<float> &source, int width, int height, int radius)
{
    const float span = float(radius * 2 + 1);
    const size_t stride = size_t(width);
    std::vector<float> pass = floats(stride * size_t(height));
    for (int y = 0; y < height; ++y) {
        const float *row = source.data() + size_t(y) * stride;
        float sum = 0;
        for (int x = -radius; x <= radius; ++x)
            sum += row[std::clamp(x, 0, width - 1)];
        float *out = pass.data() + size_t(y) * stride;
        for (int x = 0; x < width; ++x) {
            out[x] = sum / span;
            sum -= row[std::clamp(x - radius, 0, width - 1)];
            sum += row[std::clamp(x + radius + 1, 0, width - 1)];
        }
    }
    // Each column sums as Swift's, a row at a time.
    std::vector<float> result = floats(stride * size_t(height)), sums = floats(stride);
    for (int y = -radius; y <= radius; ++y) {
        const float *row = pass.data() + size_t(std::clamp(y, 0, height - 1)) * stride;
        for (size_t x = 0; x < stride; ++x)
            sums[x] += row[x];
    }
    for (int y = 0; y < height; ++y) {
        float *out = result.data() + size_t(y) * stride;
        const float *leaving = pass.data() + size_t(std::clamp(y - radius, 0, height - 1)) * stride;
        const float *entering = pass.data() + size_t(std::clamp(y + radius + 1, 0, height - 1)) * stride;
        for (size_t x = 0; x < stride; ++x) {
            out[x] = sums[x] / span;
            sums[x] -= leaving[x];
            sums[x] += entering[x];
        }
    }
    return result;
}

std::vector<float> GuidedMatte::filter(const std::vector<float> &mask, const std::vector<float> &guide, int width, int height, int radius, float epsilon)
{
    const size_t count = size_t(width) * size_t(height);
    const std::vector<float> meanGuide = box(guide, width, height, radius), meanMask = box(mask, width, height, radius);
    std::vector<float> slope = floats(count), offset = floats(count);
    {
        std::vector<float> squares = floats(count), products = floats(count);
        for (size_t i = 0; i < count; ++i) {
            squares[i] = guide[i] * guide[i];
            products[i] = guide[i] * mask[i];
        }
        const std::vector<float> meanSquares = box(squares, width, height, radius), meanProducts = box(products, width, height, radius);
        for (size_t i = 0; i < count; ++i) {
            const float variance = meanSquares[i] - meanGuide[i] * meanGuide[i];
            const float covariance = meanProducts[i] - meanGuide[i] * meanMask[i];
            slope[i] = covariance / (variance + epsilon);
            offset[i] = meanMask[i] - slope[i] * meanGuide[i];
        }
    }
    const std::vector<float> meanSlope = box(slope, width, height, radius), meanOffset = box(offset, width, height, radius);
    std::vector<float> result = floats(count);
    for (size_t i = 0; i < count; ++i)
        result[i] = std::min(1.0f, std::max(0.0f, meanSlope[i] * guide[i] + meanOffset[i]));
    return result;
}

// Premultiplied first: Qt area-filters that, not gray, when shrinking.
std::vector<float> GuidedMatte::levels(const QImage &image, int width, int height)
{
    const QImage source = checked(checked(image.convertToFormat(QImage::Format_RGBA8888_Premultiplied))
                                      .scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                      .convertToFormat(QImage::Format_RGBA8888_Premultiplied));
    std::vector<float> result = floats(size_t(width) * size_t(height));
    for (int y = 0; y < height; ++y) {
        const uchar *row = source.constScanLine(y);
        for (int x = 0; x < width; ++x)
            result[size_t(y) * size_t(width) + size_t(x)] = float(qGray(row[x * 4], row[x * 4 + 1], row[x * 4 + 2])) / 255;
    }
    return result;
}

QImage GuidedMatte::image(const std::vector<float> &levels, int width, int height)
{
    QImage result = checked(QImage(width, height, QImage::Format_Grayscale8));
    for (int y = 0; y < height; ++y) {
        uchar *row = result.scanLine(y);
        for (int x = 0; x < width; ++x)
            row[x] = uchar(std::min(255.0f, std::max(0.0f, levels[size_t(y) * size_t(width) + size_t(x)] * 255 + 0.5f)));
    }
    return result;
}

QImage GuidedMatte::refine(const QImage &mask, const QImage &guide, double radius, double limit)
{
    const QSize full = mask.size();
    const double factor = std::min(1.0, limit / std::max(full.width(), full.height()));
    const int width = std::max(1, int(std::round(full.width() * factor))), height = std::max(1, int(std::round(full.height() * factor)));
    const int steps = std::max(1, int(std::round(radius * factor)));
    const QImage small = image(filter(levels(mask, width, height), levels(guide, width, height), width, height, steps, 1e-4f), width, height);
    // Drawn back up, filtered; Qt returns one already that size.
    return checked(small.scaled(full, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_Grayscale8));
}
