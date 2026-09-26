#pragma once
#include <QPainter>
#include <QPointF>

// SF Symbols' eyedropper, which the rail and the cursor share.
namespace EyedropperIcon {
// The tube's tip, bottom left, on the 18-point grid.
QPointF tip();
// Draws with the painter's pen, as the rail's icons.
void paint(QPainter &painter);
}
