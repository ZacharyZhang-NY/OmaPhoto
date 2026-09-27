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
    // Swift's ObjectSelectionToolIcon: four corners round a pointer.
    case SelectionIcon::objectSelection: {
        QPen pen = painter.pen();
        pen.setWidthF(1.6);
        painter.setPen(pen);
        for (const QPolygonF &corner : {QPolygonF{QPointF(2, 6), QPointF(2, 2), QPointF(6, 2)}, QPolygonF{QPointF(12, 2), QPointF(16, 2), QPointF(16, 6)},
                                        QPolygonF{QPointF(16, 12), QPointF(16, 16), QPointF(12, 16)}, QPolygonF{QPointF(6, 16), QPointF(2, 16), QPointF(2, 12)}})
            painter.drawPolyline(corner);
        painter.setPen(Qt::NoPen);
        painter.setBrush(pen.color());
        painter.drawPolygon(QPolygonF{QPointF(7, 5), QPointF(7, 14), QPointF(9.6, 11.7), QPointF(11.3, 15.3), QPointF(13.2, 14.4), QPointF(11.5, 10.9), QPointF(14.5, 10.9)});
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
