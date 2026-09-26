#pragma once
#include "Document/LayerTransform.h"
#include <QRectF>
#include <optional>
#include <vector>

// Swift's CropGeometry: frames on whole pixels, within Swift's bounds.
namespace CropGeometry {
QRectF snapped(const QRectF &rect);
bool valid(const QRectF &rect);
// From `start` to `end`, or out from `start` when symmetric.
QRectF create(QPointF start, QPointF end, std::optional<double> ratio, bool symmetric = false);
}

// Swift's CropDrag: a frame being made, moved or resized.
struct CropDrag {
    enum class Kind { create, move, resize };
    struct Mode {
        Kind kind;
        int index = 0;
    };
    QPointF start;
    QRectF original;
    Mode mode;
    // Symmetric (Alt) keeps the frame's middle still.
    QRectF updated(QPointF point, std::optional<double> ratio, bool symmetric = false) const;
};

// Swift's CropSnap: dragged edges drawn to nearby edges.
struct CropSnap {
    std::vector<double> xs;
    std::vector<double> ys;
    // In document pixels.
    double tolerance;
    QRectF apply(QRectF rect, const CropDrag &drag, QPointF point, std::optional<double> ratio, bool symmetric = false) const;
};
