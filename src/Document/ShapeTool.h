#pragma once
#include "Document/ColorPalette.h"
#include "IO/ImageImporter.h"
#include <QImage>
#include <QPainterPath>
#include <QRectF>
#include <array>
#include <optional>

// The Shape tool's shapes: lines stroked, the rest filled.
enum class ShapeKind { rectangle, ellipse, line };
inline constexpr std::array allShapeKinds{ShapeKind::rectangle, ShapeKind::ellipse, ShapeKind::line};
QString rawValue(ShapeKind kind);
std::optional<ShapeKind> shapeKind(const QString &text);
// The shape filling `rect`; a big radius makes a pill.
QPainterPath path(ShapeKind kind, const QRectF &rect, double cornerRadius = 0);
// A round-capped line's outline, as CoreGraphics strokes it.
QPainterPath strokedLine(QPointF from, QPointF to, double width);

// What a shape layer draws, kept for drawing it resized.
struct LayerShapeStyle {
    ShapeKind kind;
    double red;
    double green;
    double blue;
    // Document pixels, whatever size the shape is scaled to.
    double cornerRadius;
    // A line's thickness and ends, as fractions of its box.
    std::optional<double> lineWidth = std::nullopt;
    std::optional<QPointF> start = std::nullopt;
    std::optional<QPointF> end = std::nullopt;
    PaletteColor color() const { return {red, green, blue}; }
    friend bool operator==(const LayerShapeStyle &, const LayerShapeStyle &) = default;
};

// A shape layer: its style and the image it drew.
struct LayerShape {
    LayerShapeStyle style;
    ImageIdentity image;
    static std::optional<LayerShape> loaded(const std::optional<LayerShapeStyle> &style, const std::optional<ImportedImage> &image);
    friend bool operator==(const LayerShape &, const LayerShape &) = default;
};

// A shape being dragged out, in document pixels.
struct ShapeDraft {
    ShapeKind kind;
    QPointF anchor;
    QRectF rect;
    // Where a line is dragged to; its ends stay put.
    std::optional<QPointF> end = std::nullopt;
    // Rectangles only, fixed as the drag starts.
    double cornerRadius = 0;
    friend bool operator==(const ShapeDraft &, const ShapeDraft &) = default;
};

// Swift's tuple: a rounded rectangle drawn at its dragged size.
struct ShapePreview {
    QSizeF size;
    QImage image;
};
