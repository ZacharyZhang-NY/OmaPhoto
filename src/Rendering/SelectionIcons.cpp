#include "Rendering/SelectionIcons.h"
#include <QPainterPath>

namespace {
void dashed(QPainter &painter)
{
    QPen pen = painter.pen();
    pen.setDashPattern({2.2, 1.8});
    pen.setCapStyle(Qt::FlatCap);
    painter.setPen(pen);
}
}

void SelectionIcons::paint(QPainter &painter, SelectionIcon icon)
{
    switch (icon) {
    case SelectionIcon::rectangleMarquee:
        dashed(painter);
        painter.drawRoundedRect(QRectF(2.5, 3.5, 13, 11), 2, 2);
        return;
    // Swift's circle.dashed: the Marquee in Ellipse mode.
    case SelectionIcon::ellipseMarquee:
        dashed(painter);
        painter.drawEllipse(QRectF(2.5, 2.5, 13, 13));
        return;
    case SelectionIcon::freehandLasso: {
        painter.drawEllipse(QRectF(2, 2.5, 14, 8.5));
        QPainterPath tail(QPointF(5.2, 10.2));
        tail.cubicTo(QPointF(3.2, 12), QPointF(4.6, 14.2), QPointF(6.6, 13.2));
        tail.cubicTo(QPointF(8, 12.4), QPointF(6.4, 10.8), QPointF(5.2, 12.2));
        tail.cubicTo(QPointF(4.4, 13.4), QPointF(5, 15.6), QPointF(7.4, 16));
        painter.drawPath(tail);
        return;
    }
    // Swift's PolygonalLassoToolIcon: loop, knot and rope as segments.
    case SelectionIcon::polygonalLasso:
        painter.drawPolygon(QPolygonF{QPointF(1.2, 7.0), QPointF(4.0, 2.4), QPointF(11.8, 1.8), QPointF(16.8, 5.2), QPointF(15.6, 10.4), QPointF(7.0, 11.6)});
        painter.drawPolygon(QPolygonF{QPointF(8.9, 10.9), QPointF(13.3, 10.5), QPointF(11.6, 14.5)});
        painter.drawLine(QPointF(11.6, 14.5), QPointF(12.9, 17.3));
        return;
    }
}
