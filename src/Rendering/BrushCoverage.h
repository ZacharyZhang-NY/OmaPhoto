#pragma once
#include <QImage>
#include <QRectF>
#include <QSizeF>
#include <QTransform>
#include <vector>

struct BrushSettings;

// A piece of the centreline, in document points (Swift's SIMD4).
struct BrushSegment {
    float x0;
    float y0;
    float x1;
    float y1;
};

// Twin of MetalBrushCoverage: the continuous brush on the CPU.
namespace BrushCoverage {
// Density laid so far, a float a pixel.
struct Tile {
    std::vector<float> permanent;
};
// One tile's density, rect, and the gray context previewed.
struct Work {
    Tile *tile;
    QRectF rect;
    QImage *context;
};
Tile tile(int width, int height);
void render(const std::vector<Work> &tiles, const std::vector<BrushSegment> &settled, const std::vector<BrushSegment> &tail,
            const QTransform &mapping, const BrushSettings &settings, QSizeF canvas);
}
