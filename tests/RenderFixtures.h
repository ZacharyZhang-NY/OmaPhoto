#pragma once
#include "Document/BrushStroke.h"
#include "Rendering/RasterSnapshot.h"
#include <QImage>
#include <functional>
#include <memory>
#include <vector>

// An RGBA image filled with one premultiplied pixel value.
inline QImage solid(int width, int height, QRgb premultiplied)
{
    QImage image = BrushRaster::context(width, height, false);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x)
            image.setPixel(x, y, premultiplied);
    }
    return image;
}

// Red holds x, green holds y, opaque.
inline QImage coordinates(int width, int height, int step = 10)
{
    QImage image = BrushRaster::context(width, height, false);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x)
            image.setPixel(x, y, qRgba(x * step, y * step, 0, 255));
    }
    return image;
}

inline QImage gray(int width, int height, int value)
{
    QImage image = BrushRaster::context(width, height, true);
    image.fill(value);
    return image;
}

inline LayerTransform placedAt(QPointF origin, QSizeF size)
{
    return {.origin = origin, .size = size, .sampling = LayerSampling::nearest};
}

// Detailed, deterministic pixels.
inline QImage noise(int width, int height, quint32 seed, int alpha = 255)
{
    QImage image = BrushRaster::context(width, height, false);
    quint32 state = seed;
    for (int y = 0; y < height; ++y) {
        uchar *row = image.scanLine(y);
        for (int x = 0; x < width; ++x, row += 4) {
            state = state * 1'664'525u + 1'013'904'223u;
            row[0] = uchar(int(uchar(state >> 24)) * alpha / 255);
            row[1] = uchar(int(uchar(state >> 16)) * alpha / 255);
            row[2] = uchar(int(uchar(state >> 8)) * alpha / 255);
            row[3] = uchar(alpha);
        }
    }
    return image;
}

// A painted layer: unchanged pixels and replacement tiles.
inline std::shared_ptr<const RasterSnapshot> painted(const QImage &base, const std::vector<BrushPatch> &patches)
{
    return std::make_shared<const RasterSnapshot>(base.width(), base.height(), base, QRectF(base.rect()), patches);
}

// Smooth pixels: a slipped sample changes little.
inline QImage ramp(int width, int height, int seed)
{
    QImage image = BrushRaster::context(width, height, false);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x)
            image.setPixel(x, y, qRgba((x + seed) / 4 % 256, y / 4 % 256, seed % 256, 255));
    }
    return image;
}

inline QImage composite(const QImage &base, const std::vector<BrushPatch> &patches)
{
    QImage result = BrushRaster::context(base.width(), base.height(), false);
    QPainter painter(&result);
    BrushRaster::draw(base, QRectF(base.rect()), painter);
    for (const BrushPatch &patch : patches)
        BrushRaster::draw(patch.image, patch.rect, painter);
    return result;
}

inline QImage render(int side, const std::function<void(QPainter &)> &draw)
{
    QImage surface = BrushRaster::context(side, side, false);
    QPainter painter(&surface);
    draw(painter);
    return surface;
}

// Largest channel difference over pixels fully inside `outline`'s layer.
inline int largestDifference(const QImage &expected, const QImage &actual, const QImage &outline)
{
    int largest = 0;
    const int side = expected.width();
    for (int y = 1; y < side - 1; ++y) {
        for (int x = 1; x < side - 1; ++x) {
            bool inside = true;
            for (int dy = -1; dy <= 1 && inside; ++dy) {
                for (int dx = -1; dx <= 1 && inside; ++dx)
                    inside = outline.constScanLine(y + dy)[(x + dx) * 4 + 3] == 255;
            }
            if (!inside)
                continue;
            for (int channel = 0; channel < 4; ++channel)
                largest = std::max(largest, std::abs(expected.constScanLine(y)[x * 4 + channel] - actual.constScanLine(y)[x * 4 + channel]));
        }
    }
    return largest;
}

inline int largestDifference(const QImage &expected, const QImage &actual)
{
    return largestDifference(expected, actual, expected);
}

// Largest channel difference over every pixel.
inline int largestAnywhere(const QImage &expected, const QImage &actual)
{
    int largest = 0;
    for (int y = 0; y < expected.height(); ++y) {
        for (int x = 0; x < expected.width() * 4; ++x)
            largest = std::max(largest, std::abs(expected.constScanLine(y)[x] - actual.constScanLine(y)[x]));
    }
    return largest;
}
