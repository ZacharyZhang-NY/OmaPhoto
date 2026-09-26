#include "UI/LayerIcons.h"
#include "UI/ToolIcons.h"
#include <QPainterPath>

namespace {
void eye(QPainter &painter, bool slashed)
{
    QPainterPath lid;
    lid.moveTo(1.5, 9);
    lid.cubicTo(5, 3, 13, 3, 16.5, 9);
    lid.cubicTo(13, 15, 5, 15, 1.5, 9);
    painter.drawPath(lid);
    painter.drawEllipse(QPointF(9, 9), 2.6, 2.6);
    if (slashed)
        painter.drawLine(QPointF(3, 15), QPointF(15, 3));
}

void chevron(QPainter &painter, bool down)
{
    if (down)
        painter.drawPolyline(QPolygonF{QPointF(4.5, 6.5), QPointF(9, 11), QPointF(13.5, 6.5)});
    else
        painter.drawPolyline(QPolygonF{QPointF(6.5, 4.5), QPointF(11, 9), QPointF(6.5, 13.5)});
}

void folder(QPainter &painter, bool badged)
{
    QPainterPath shape;
    shape.moveTo(2, 4.5);
    shape.lineTo(7, 4.5);
    shape.lineTo(8.5, 6.5);
    shape.lineTo(16, 6.5);
    shape.lineTo(16, 14.5);
    shape.lineTo(2, 14.5);
    shape.closeSubpath();
    painter.drawPath(shape);
    if (badged) {
        painter.drawLine(QPointF(9, 8.5), QPointF(9, 12.5));
        painter.drawLine(QPointF(7, 10.5), QPointF(11, 10.5));
    }
}

// Swift's chain turned 45 degrees: upright in a narrow gap.
void link(QPainter &painter)
{
    painter.save();
    painter.translate(9, 9);
    painter.rotate(-45);
    painter.drawRoundedRect(QRectF(-6.5, -2.2, 7.5, 4.4), 2.2, 2.2);
    painter.drawRoundedRect(QRectF(-1, -2.2, 7.5, 4.4), 2.2, 2.2);
    painter.restore();
}

void newLayer(QPainter &painter)
{
    painter.drawRoundedRect(QRectF(2.5, 2.5, 13, 13), 2, 2);
    painter.drawLine(QPointF(9, 6), QPointF(9, 12));
    painter.drawLine(QPointF(6, 9), QPointF(12, 9));
}

void addMask(QPainter &painter)
{
    painter.drawRoundedRect(QRectF(2.5, 3.5, 13, 11), 2, 2);
    painter.fillRect(QRectF(5.5, 6.5, 7, 5), painter.pen().color());
}

void trash(QPainter &painter)
{
    painter.drawLine(QPointF(3, 5), QPointF(15, 5));
    painter.drawLine(QPointF(7, 3), QPointF(11, 3));
    painter.drawPolyline(QPolygonF{QPointF(4.5, 5), QPointF(5.5, 15.5), QPointF(12.5, 15.5), QPointF(13.5, 5)});
    painter.drawLine(QPointF(7.5, 8), QPointF(7.8, 13));
    painter.drawLine(QPointF(10.5, 8), QPointF(10.2, 13));
}

// Swift's square.3.layers.3d: three tilted plates.
void layers(QPainter &painter)
{
    for (int step = 0; step < 3; ++step) {
        const double y = 4 + step * 4;
        painter.drawPolygon(QPolygonF{QPointF(9, y - 3), QPointF(16, y), QPointF(9, y + 3), QPointF(2, y)});
    }
}

// Swift's circle.lefthalf.filled: a ring, its left half solid.
void halfFilledCircle(QPainter &painter)
{
    QPainterPath half;
    half.moveTo(9, 2);
    half.arcTo(QRectF(2, 2, 14, 14), 90, 180);
    half.closeSubpath();
    painter.fillPath(half, painter.pen().color());
    painter.drawEllipse(QPointF(9, 9), 7, 7);
}

// Swift's slider.horizontal.3: three tracks, each with a knob.
void sliders(QPainter &painter)
{
    for (const auto &[y, knob] : {std::pair{4.0, 12.0}, std::pair{9.0, 6.0}, std::pair{14.0, 10.5}}) {
        painter.drawLine(QPointF(2, y), QPointF(knob - 2.8, y));
        painter.drawLine(QPointF(knob + 2.8, y), QPointF(16, y));
        painter.drawEllipse(QPointF(knob, y), 2, 2);
    }
}

// Swift's point.topleft.down.to.point.bottomright.curvepath.
void curvePath(QPainter &painter)
{
    QPainterPath curve;
    curve.moveTo(5.5, 3.5);
    curve.cubicTo(12, 3.5, 6, 14.5, 12.5, 14.5);
    painter.drawPath(curve);
    painter.setBrush(painter.pen().color());
    painter.drawEllipse(QPointF(3.5, 3.5), 1.6, 1.6);
    painter.drawEllipse(QPointF(14.5, 14.5), 1.6, 1.6);
}

// Swift's plusminus.circle.
void plusMinusCircle(QPainter &painter)
{
    painter.drawEllipse(QPointF(9, 9), 7, 7);
    painter.drawLine(QPointF(6.5, 7.5), QPointF(11.5, 7.5));
    painter.drawLine(QPointF(9, 5), QPointF(9, 10));
    painter.drawLine(QPointF(6.5, 12.2), QPointF(11.5, 12.2));
}

// Swift's paintpalette: a palette with a thumb notch and wells.
void paintPalette(QPainter &painter)
{
    QPainterPath palette;
    palette.moveTo(9, 2.5);
    palette.cubicTo(13.5, 2.5, 16, 5.5, 16, 8.5);
    palette.cubicTo(16, 11, 13.5, 11.5, 12, 11.2);
    palette.cubicTo(10.8, 11, 10.2, 12.2, 10.8, 13.4);
    palette.cubicTo(11.5, 15, 10.5, 15.8, 9, 15.8);
    palette.cubicTo(5, 15.8, 2, 12.8, 2, 9);
    palette.cubicTo(2, 5.2, 5, 2.5, 9, 2.5);
    painter.drawPath(palette);
    painter.setBrush(painter.pen().color());
    for (const QPointF well : {QPointF(5.8, 6.8), QPointF(9.3, 5.4), QPointF(12.7, 7.3), QPointF(5.4, 10.6)})
        painter.drawEllipse(well, 1.1, 1.1);
}

// Swift's circle.grid.3x3: nine dots.
void circleGrid(QPainter &painter)
{
    painter.setBrush(painter.pen().color());
    painter.setPen(Qt::NoPen);
    for (const double y : {4.0, 9.0, 14.0}) {
        for (const double x : {4.0, 9.0, 14.0})
            painter.drawEllipse(QPointF(x, y), 1.7, 1.7);
    }
}

// Swift's sparkles: a large four-pointed star, two small ones.
void sparkles(QPainter &painter)
{
    const auto star = [&painter](QPointF centre, double radius) {
        const double bend = radius * 0.18;
        QPainterPath path;
        path.moveTo(centre.x(), centre.y() - radius);
        path.quadTo(centre.x() + bend, centre.y() - bend, centre.x() + radius, centre.y());
        path.quadTo(centre.x() + bend, centre.y() + bend, centre.x(), centre.y() + radius);
        path.quadTo(centre.x() - bend, centre.y() + bend, centre.x() - radius, centre.y());
        path.quadTo(centre.x() - bend, centre.y() - bend, centre.x(), centre.y() - radius);
        painter.fillPath(path, painter.pen().color());
    };
    star(QPointF(7.5, 10.5), 6.5);
    star(QPointF(14, 4.5), 3);
    star(QPointF(14.5, 14), 2);
}
}

void LayerIcons::paint(QPainter &painter, LayerIcon icon, QPointF origin, double side, const QColor &colour)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(origin);
    painter.scale(side / 18, side / 18);
    painter.setPen(QPen(colour, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.setBackgroundMode(Qt::TransparentMode);
    switch (icon) {
    case LayerIcon::eye: eye(painter, false); break;
    case LayerIcon::eyeSlash: eye(painter, true); break;
    case LayerIcon::chevronRight: chevron(painter, false); break;
    case LayerIcon::chevronDown: chevron(painter, true); break;
    case LayerIcon::folder: folder(painter, false); break;
    case LayerIcon::link: link(painter); break;
    case LayerIcon::newLayer: newLayer(painter); break;
    case LayerIcon::newFolder: folder(painter, true); break;
    case LayerIcon::addMask: addMask(painter); break;
    case LayerIcon::trash: trash(painter); break;
    case LayerIcon::layers: layers(painter); break;
    // Swift's textformat, as the rail draws it, 1.21 times up.
    case LayerIcon::text: ToolIcons::paint(painter, NavigationTool::type, QPointF(3.6, 3.6), 10.8, colour); break;
    case LayerIcon::halfFilledCircle: halfFilledCircle(painter); break;
    case LayerIcon::sliders: sliders(painter); break;
    case LayerIcon::curvePath: curvePath(painter); break;
    case LayerIcon::plusMinusCircle: plusMinusCircle(painter); break;
    case LayerIcon::paintPalette: paintPalette(painter); break;
    case LayerIcon::circleGrid: circleGrid(painter); break;
    case LayerIcon::sparkles: sparkles(painter); break;
    }
    painter.restore();
}

QPixmap LayerIcons::pixmap(LayerIcon icon, double side, const QColor &colour, double ratio)
{
    QPixmap result(QSize(int(side * ratio), int(side * ratio)));
    result.setDevicePixelRatio(ratio);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    paint(painter, icon, QPointF(0, 0), side, colour);
    return result;
}

LayerIcon LayerIcons::symbol(AdjustmentKind kind)
{
    switch (kind) {
    case AdjustmentKind::curves: return LayerIcon::curvePath;
    case AdjustmentKind::levels: return LayerIcon::sliders;
    case AdjustmentKind::hsv: return LayerIcon::halfFilledCircle;
    case AdjustmentKind::exposure: return LayerIcon::plusMinusCircle;
    case AdjustmentKind::gradientMap: return LayerIcon::paintPalette;
    case AdjustmentKind::grain: return LayerIcon::circleGrid;
    }
    throw std::logic_error("unknown adjustment kind");
}

QPixmap LayerIcons::adjustmentThumbnail(AdjustmentKind kind, const QColor &colour, double ratio)
{
    QPixmap result(QSize(int(36 * ratio), int(36 * ratio)));
    result.setDevicePixelRatio(ratio);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    // Swift turns the curve a quarter clockwise: a tone curve.
    if (kind == AdjustmentKind::curves) {
        painter.translate(18, 18);
        painter.rotate(90);
        painter.translate(-18, -18);
    }
    paint(painter, symbol(kind), QPointF(7.2, 7.2), 18 * 1.2, colour);
    return result;
}
