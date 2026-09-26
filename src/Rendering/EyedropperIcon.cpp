#include "Rendering/EyedropperIcon.h"
#include <QPainterPath>

namespace {
// The glyph stands turned 45 degrees about the grid's middle.
QTransform turned()
{
    return QTransform().translate(9, 9).rotate(45);
}

constexpr QPointF tubeTip(0, 8.4);
}

QPointF EyedropperIcon::tip()
{
    return turned().map(tubeTip);
}

void EyedropperIcon::paint(QPainter &painter)
{
    // A bulb at the top right; a tube below.
    painter.save();
    painter.setTransform(turned(), true);
    painter.fillRect(QRectF(-2.4, -8.6, 4.8, 4.4), painter.pen().color());
    painter.drawLine(QPointF(-3.6, -3.6), QPointF(3.6, -3.6));
    QPainterPath tube(QPointF(-1.7, -3.6));
    tube.lineTo(QPointF(-1.7, 5.2));
    tube.lineTo(tubeTip);
    tube.lineTo(QPointF(1.7, 5.2));
    tube.lineTo(QPointF(1.7, -3.6));
    painter.drawPath(tube);
    painter.restore();
}
