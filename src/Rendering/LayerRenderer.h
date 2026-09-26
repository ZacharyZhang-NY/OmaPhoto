#pragma once
#include "Document/BrushStroke.h"
#include "Document/LayerAppearance.h"
#include "Document/LayerTransform.h"
#include <QImage>
#include <QPainter>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class RasterSnapshot;

// Draws layers top-left, shared by the canvas and export.
namespace LayerRenderer {
struct Options {
    double scale = 1;
    double opacity = 1;
    LayerBlendMode blendMode = LayerBlendMode::normal;
    // A null image means no mask.
    QImage mask = QImage();
    // Folder masks as device-sized alpha coverage; null means none.
    QImage clip = QImage();
};

// An image halved for a large reduction, and its overhang.
struct Reduced {
    QImage image;
    int level;
    double widthScale;
    double heightScale;
};

// Replacement tiles over a source, in a pixel grid.
struct BrushPreview {
    std::vector<BrushPatch> patches;
    int pixelWidth;
    int pixelHeight;
    // Patches are coverage the source shows through.
    bool paintingMask;
    // Where the source sits in the grid; absent means everywhere.
    std::optional<QRectF> sourceRect = std::nullopt;
    // A painted source, drawn from its tiles.
    std::shared_ptr<const RasterSnapshot> raster = nullptr;
    // Stands in for the raster's base when not null.
    QImage rasterBase = QImage();
};

void draw(const QImage &image, const LayerTransform &transform, QPointF center, QPainter &context,
          const Options &options = {});
// Fills white through a gray mask, as the painter blends.
void drawCoverage(const QImage &image, const LayerTransform &transform, QPainter &context);
// Previews tiles without a full-size raster; blends exactly once.
void drawBrushPreview(const QImage &image, const LayerTransform &transform, QPointF center, QPainter &context,
                      const Options &options, const BrushPreview &preview);
// Composes a layer aside, composites it once; `veil` repaints coverage.
void composite(QPainter &context, const QTransform &placement, const QRectF &extent, LayerSampling sampling,
               InterpolationQuality quality, const Options &options, const QRectF &maskBounds,
               const std::function<void(QPainter &)> &body, const std::function<void(QPainter &)> &veil = {});
InterpolationQuality interpolation(LayerSampling sampling, double finalFactor);
Reduced reduced(const QImage &image, double width, double device, LayerSampling sampling);
QRectF coverage(const Reduced &reduced, const QRectF &bounds);
double deviceScale(const QPainter &context);
}
