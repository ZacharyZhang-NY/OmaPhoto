#pragma once
#include "Document/BrushStroke.h"
#include "Document/CameraRaw.h"
#include <QColor>
#include <QPainter>
#include <algorithm>
#include <array>
#include <vector>

// Swift's CameraRawTests helpers: solid pictures and straight bytes.
namespace CameraRawFixtures {
inline QImage image(int width, int height, double red, double green, double blue, double alpha = 1)
{
    QImage context = BrushRaster::context(width, height, false);
    QPainter painter(&context);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(context.rect(), QColor::fromRgbF(float(red), float(green), float(blue), float(alpha)));
    return context;
}

inline QImage gray(int width = 4, int height = 4, double alpha = 1)
{
    return image(width, height, 128 / 255.0, 128 / 255.0, 128 / 255.0, alpha);
}

// Straight RGBA, top row first, rounded as Swift's.
inline std::vector<std::array<int, 4>> pixels(const QImage &source)
{
    const QImage drawn = source.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    std::vector<std::array<int, 4>> result;
    for (int y = 0; y < drawn.height(); ++y) {
        const uchar *row = drawn.constScanLine(y);
        for (int x = 0; x < drawn.width(); ++x) {
            const uchar *p = row + x * 4;
            const int alpha = p[3];
            std::array<int, 4> pixel{0, 0, 0, alpha};
            for (int channel = 0; channel < 3; ++channel)
                pixel[size_t(channel)] = alpha == 0 ? 0 : std::min(255, (p[channel] * 255 + alpha / 2) / alpha);
            result.push_back(pixel);
        }
    }
    return result;
}

inline int chroma(const std::array<int, 4> &pixel)
{
    return std::max({pixel[0], pixel[1], pixel[2]}) - std::min({pixel[0], pixel[1], pixel[2]});
}

// Two tones side by side, one pixel each.
inline QImage pair(int left, int right)
{
    QImage context = BrushRaster::context(2, 1, false);
    context.setPixelColor(0, 0, QColor(left, left, left));
    context.setPixelColor(1, 0, QColor(right, right, right));
    return context;
}

// A step, 40 to 200 at x 12.
inline QImage step()
{
    QImage context = BrushRaster::context(24, 4, false);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 24; ++x)
            context.setPixelColor(x, y, x < 12 ? QColor(40, 40, 40) : QColor(200, 200, 200));
    return context;
}

// A checkerboard of 2-pixel squares: a warp shows on it.
inline QImage checker(int width = 24, int height = 24)
{
    QImage context = BrushRaster::context(width, height, false);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const bool light = ((x / 2) + (y / 2)) % 2 == 0;
            const double level = light ? 0.9 : 0.1;
            context.setPixelColor(x, y, QColor::fromRgbF(float(level), float(level), float(level)));
        }
    return context;
}

inline int peakIndex(const std::array<double, 256> &bins)
{
    return int(std::max_element(bins.begin(), bins.end()) - bins.begin());
}
}
