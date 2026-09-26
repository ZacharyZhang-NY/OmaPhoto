#pragma once
#include <QImage>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <optional>
#include <vector>

// Coverage of one document region, ready to clip edits.
struct SelectionClip {
    QRectF rect;
    // Nil for an empty selection: it clips everything away.
    std::optional<QImage> coverage;
};

// A document-space outline, clipped to the canvas.
struct DocumentSelection {
    QPainterPath path;
    bool antialiased = true;
    // How far the edge fades, in pixels; 0 is hard.
    double feather = 0;

    // An explicit empty selection: later edits touch nothing.
    bool isEmpty() const;
    // Gray coverage at document resolution, white where selected.
    QImage coverage(int width, int height) const;
    // The outline's box, grown by the feather's visible falloff.
    QRectF coverageBounds() const;
    SelectionClip clip(QSizeF canvas) const;
    friend bool operator==(const DocumentSelection &, const DocumentSelection &) = default;
};

// The Marquee's outlines are rectangle and ellipse.
enum class LassoKind { freehand, polygonal, rectangle, ellipse };
// The name the tool bar shows.
QString rawValue(LassoKind kind);

enum class SelectionMode { replace, add, subtract };
QString rawValue(SelectionMode mode);

// What Select's Expand, Contract and Feather ask an amount for.
enum class SelectionAmountOperation { expand, contract, feather };
QString rawValue(SelectionAmountOperation operation);

// A drag's box in whole pixels; shared with Shape.
namespace DragBox {
QRectF rect(QPointF anchor, QPointF point, bool square, bool fromCenter);
}

// An outline being drawn, in document pixels.
struct LassoDraft {
    std::vector<QPointF> points;
    // The polygonal lasso's rubber-band end.
    std::optional<QPointF> cursor;
    SelectionMode mode;
    LassoKind kind;
    // The Marquee's starting corner or centre, whole pixels.
    std::optional<QPointF> anchor;
    friend bool operator==(const LassoDraft &, const LassoDraft &) = default;
};
