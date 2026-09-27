#pragma once
#include "Document/EditorSession+Model.h"
#include <QColor>
#include <QPainter>
#include <QRectF>

// The Marquee's and the Lasso's icons follow their kinds.
enum class ToolIconKind { plain, ellipse, polygonal, eraser, object };

// The tool rail's icons, drawn: SF Symbols have no twin.
namespace ToolIcons {
// Swift's icons sit in an 18 point square.
inline constexpr int points = 18;
// Paints the tool's icon into a square, in one colour.
void paint(QPainter &painter, NavigationTool tool, QPointF origin, double side, const QColor &colour, ToolIconKind kind = ToolIconKind::plain);
}
