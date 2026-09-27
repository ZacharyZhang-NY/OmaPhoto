#pragma once
#include <QHash>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QTransform>
#include <QUuid>
#include <array>
#include <memory>
#include <optional>
#include <vector>

enum class LayerSampling { nearest, smooth, high };
// The name users see and the manifest stores.
QString rawValue(LayerSampling sampling);
std::optional<LayerSampling> layerSampling(const QString &rawValue);

// CoreGraphics interpolation qualities the renderer asks for.
enum class InterpolationQuality { none, low, high };
InterpolationQuality quality(LayerSampling sampling);

using Corners = std::array<QPointF, 4>;

// Unrotated document bounds; rotation is clockwise about the center.
struct LayerTransform {
    QPointF origin;
    QSizeF size;
    double rotation = 0;
    bool flipX = false;
    bool flipY = false;
    LayerSampling sampling = LayerSampling::high;

    static const std::array<QPointF, 8> handles;

    friend bool operator==(const LayerTransform &lhs, const LayerTransform &rhs);
    QPointF center() const;
    double radians() const;
    bool isValid() const;
    QPointF point(QPointF unit) const;
    bool contains(QPointF point) const;
    double scalePercent(QSizeF pixelSize) const;
    LayerTransform scaled(double toPercent, QSizeF pixelSize) const;
    LayerTransform rounded() const;
    QTransform unitToDocument() const;
    LayerTransform placing(const QTransform &map) const;
    LayerTransform following(const LayerTransform &old, const LayerTransform &updated) const;
    bool samePlacement(const LayerTransform &other) const;
    // Mirrored across a line at `axis`; defined in LayerFlip.cpp.
    LayerTransform mirrored(bool horizontally, double axis) const;
};

// Several layers under one box; each follows it.
struct TransformGroup {
    LayerTransform box;
    QHash<QUuid, LayerTransform> originals;
};

// A pending transform: the layers change on commit.
struct FloatingTransform;

struct TransformEdit {
    QUuid layerID;
    LayerTransform draft;
    std::optional<TransformGroup> group;
    // An unlinked mask, selected: the edit places it alone.
    bool mask = false;
    // A drag's edit ends with it; Ctrl+T's waits for Apply.
    bool persistent = true;
    // Set once a handle is Ctrl-dragged: the corners move freely.
    std::optional<Corners> corners;
    // Ctrl+T on selected pixels: they float, then merge back.
    std::shared_ptr<const FloatingTransform> floating;
};

// Where a move has just snapped: guides while it lasts.
struct SnapGuides {
    std::vector<double> xs;
    std::vector<double> ys;
    friend bool operator==(const SnapGuides &, const SnapGuides &) = default;
};

struct TransformDrag {
    enum class Kind { move, resize, rotate, distort };
    struct Mode {
        Kind kind;
        int index = 0;
    };

    LayerTransform original;
    QPointF start;
    Mode mode;
    std::optional<Corners> originalCorners;

    std::optional<Corners> corners(QPointF to, bool shift = false) const;
    LayerTransform updated(QPointF to, bool lockRatio, bool shift, bool option = false) const;
};

namespace TransformSnap {
// Screen points within which a guide snaps.
inline constexpr double distance = 10;

struct Offset {
    QSizeF offset;
    std::optional<double> x;
    std::optional<double> y;
};

Offset offset(const QRectF &box, const std::vector<double> &xs, const std::vector<double> &ys,
              double tolerance);
// The upright box round a transform's corners; in Crop.cpp.
QRectF box(const LayerTransform &transform);
}
