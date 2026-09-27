#include "UI/ToolIcons.h"
#include "Rendering/EyedropperIcon.h"
#include "Rendering/SelectionIcons.h"
#include <QFont>
#include <QPainterPath>
#include <array>
#include <stdexcept>

namespace {
// Swift's GradientToolIcon: a Floyd-Steinberg fade, left to right.
std::array<std::array<bool, 16>, 16> ditheredFade()
{
    std::array<std::array<double, 16>, 16> ramp;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x)
            ramp[y][x] = x / 15.0;
    }
    std::array<std::array<bool, 16>, 16> result{};
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const bool on = ramp[y][x] >= 0.5;
            result[y][x] = on;
            const double error = ramp[y][x] - (on ? 1 : 0);
            if (x + 1 < 16)
                ramp[y][x + 1] += error * 7 / 16;
            if (y + 1 == 16)
                continue;
            if (x > 0)
                ramp[y + 1][x - 1] += error * 3 / 16;
            ramp[y + 1][x] += error * 5 / 16;
            if (x + 1 < 16)
                ramp[y + 1][x + 1] += error / 16;
        }
    }
    return result;
}

void gradient(QPainter &painter)
{
    static const std::array<std::array<bool, 16>, 16> pattern = ditheredFade();
    const QRectF frame(1, 1, 16, 16);
    QPainterPath shape;
    shape.addRoundedRect(frame, 3.5, 3.5);
    // Swift clips dots and frame alike: half the frame shows.
    painter.save();
    painter.setClipPath(shape, Qt::IntersectClip);
    // One path, as in Swift: dots filled apiece leave seams.
    QPainterPath dots;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            if (pattern[y][x])
                dots.addRect(QRectF(frame.left() + x, frame.top() + y, 1, 1));
        }
    }
    painter.fillPath(dots, painter.pen().color());
    QPen pen = painter.pen();
    pen.setWidthF(1.4);
    painter.setPen(pen);
    painter.drawPath(shape);
    painter.restore();
}

// Swift's CloneStampToolIcon: handle, neck, body and pad, filled.
void cloneStamp(QPainter &painter)
{
    const double w = 18, h = 18;
    QPainterPath stamp;
    stamp.addEllipse(QRectF(w * 0.33, h * 0.02, w * 0.34, h * 0.30));
    stamp.addRect(QRectF(w * 0.43, h * 0.28, w * 0.14, h * 0.28));
    stamp.addRoundedRect(QRectF(w * 0.12, h * 0.54, w * 0.76, h * 0.22), w * 0.08, w * 0.08);
    stamp.addRect(QRectF(w * 0.06, h * 0.82, w * 0.88, h * 0.12));
    stamp.setFillRule(Qt::WindingFill);
    painter.fillPath(stamp, painter.pen().color());
}

void move(QPainter &painter)
{
    painter.drawLine(QPointF(3, 3), QPointF(15, 15));
    painter.drawPolyline(QPolygonF{QPointF(3, 8.5), QPointF(3, 3), QPointF(8.5, 3)});
    painter.drawPolyline(QPolygonF{QPointF(15, 9.5), QPointF(15, 15), QPointF(9.5, 15)});
}

void marquee(QPainter &painter, ToolIconKind kind)
{
    SelectionIcons::paint(painter, kind == ToolIconKind::ellipse ? SelectionIcon::ellipseMarquee : SelectionIcon::rectangleMarquee);
}

void lasso(QPainter &painter, ToolIconKind kind)
{
    SelectionIcons::paint(painter, kind == ToolIconKind::polygonal ? SelectionIcon::polygonalLasso : SelectionIcon::freehandLasso);
}

void star(QPainter &painter, QPointF centre, double reach)
{
    painter.drawLine(centre - QPointF(reach, 0), centre + QPointF(reach, 0));
    painter.drawLine(centre - QPointF(0, reach), centre + QPointF(0, reach));
}

void wand(QPainter &painter)
{
    painter.drawLine(QPointF(2.5, 15.5), QPointF(10.5, 7.5));
    star(painter, QPointF(13, 5), 2.4);
    star(painter, QPointF(5, 4.5), 1.5);
    star(painter, QPointF(14, 12.5), 1.5);
}

void crop(QPainter &painter)
{
    painter.drawPolyline(QPolygonF{QPointF(5, 1.5), QPointF(5, 13), QPointF(16.5, 13)});
    painter.drawPolyline(QPolygonF{QPointF(1.5, 5), QPointF(13, 5), QPointF(13, 16.5)});
}

void brush(QPainter &painter)
{
    // A slanted handle over a pointed tip.
    painter.drawLine(QPointF(15.5, 2.5), QPointF(8.5, 9.5));
    QPainterPath tip(QPointF(8.8, 8));
    tip.lineTo(QPointF(10.2, 9.4));
    tip.cubicTo(QPointF(8.6, 12.2), QPointF(6, 14.6), QPointF(2.6, 15.4));
    tip.cubicTo(QPointF(3.4, 12), QPointF(5.8, 9.4), QPointF(8.8, 8));
    painter.fillPath(tip, painter.pen().color());
}

// SF Symbols' eraser: a tilted block, its rubber end apart.
void eraser(QPainter &painter)
{
    painter.save();
    painter.translate(9.5, 8);
    painter.rotate(-45);
    painter.drawRoundedRect(QRectF(-7, -3.5, 14, 7), 1.5, 1.5);
    painter.drawLine(QPointF(-2, -3.5), QPointF(-2, 3.5));
    painter.restore();
    painter.drawLine(QPointF(8, 16), QPointF(16, 16));
}

void spotHealing(QPainter &painter)
{
    // A plaster across the square, its pad in the middle.
    painter.save();
    painter.translate(9, 9);
    painter.rotate(-45);
    painter.drawRoundedRect(QRectF(-8, -3.4, 16, 6.8), 3.4, 3.4);
    painter.drawRect(QRectF(-2.6, -3.4, 5.2, 6.8));
    painter.restore();
}

void blur(QPainter &painter)
{
    // A drop: a point above a round belly.
    QPainterPath drop(QPointF(9, 1.8));
    drop.cubicTo(QPointF(11.5, 5.5), QPointF(14.4, 8.4), QPointF(14.4, 11.2));
    drop.cubicTo(QPointF(14.4, 14.2), QPointF(12, 16.4), QPointF(9, 16.4));
    drop.cubicTo(QPointF(6, 16.4), QPointF(3.6, 14.2), QPointF(3.6, 11.2));
    drop.cubicTo(QPointF(3.6, 8.4), QPointF(6.5, 5.5), QPointF(9, 1.8));
    painter.drawPath(drop);
}

void shape(QPainter &painter)
{
    painter.drawEllipse(QRectF(7, 7, 9.5, 9.5));
    painter.fillRect(QRectF(1.5, 1.5, 9.5, 9.5), painter.pen().color());
}

void type(QPainter &painter)
{
    // The caller's font stays out: no underline, no slant.
    QFont font;
    font.setPixelSize(13);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(QRectF(0, 0, 18, 18), Qt::AlignCenter, QStringLiteral("Aa"));
}

void hand(QPainter &painter)
{
    // Four fingers over a palm, a thumb at the side.
    const double tops[] = {4.6, 2.6, 2, 3.4};
    for (int finger = 0; finger < 4; ++finger)
        painter.drawLine(QPointF(5.4 + finger * 2.7, tops[finger]), QPointF(5.4 + finger * 2.7, 9));
    QPainterPath palm(QPointF(13.5, 9));
    palm.lineTo(QPointF(13.5, 12));
    palm.cubicTo(QPointF(13.5, 15), QPointF(11.4, 16.4), QPointF(9.2, 16.4));
    palm.cubicTo(QPointF(7, 16.4), QPointF(5.6, 15.2), QPointF(4.4, 13.2));
    palm.lineTo(QPointF(2.2, 9.6));
    palm.cubicTo(QPointF(1.6, 8.4), QPointF(3.2, 7.4), QPointF(4, 8.6));
    palm.lineTo(QPointF(5.4, 10.6));
    painter.drawPath(palm);
}

void zoom(QPainter &painter)
{
    painter.drawEllipse(QRectF(2, 2, 10.5, 10.5));
    painter.drawLine(QPointF(11.2, 11.2), QPointF(16, 16));
}
}

void ToolIcons::paint(QPainter &painter, NavigationTool tool, QPointF origin, double side, const QColor &colour, ToolIconKind kind)
{
    // Refused before the painter is touched.
    if (tool == NavigationTool::idle)
        throw std::logic_error("no tool has no icon");
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(origin);
    painter.scale(side / points, side / points);
    painter.setPen(QPen(colour, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    // An opaque background would fill dashes and letters.
    painter.setBackgroundMode(Qt::TransparentMode);
    switch (tool) {
    case NavigationTool::move: move(painter); break;
    case NavigationTool::marquee: marquee(painter, kind); break;
    case NavigationTool::lasso: lasso(painter, kind); break;
    case NavigationTool::wand: kind == ToolIconKind::object ? SelectionIcons::paint(painter, SelectionIcon::objectSelection) : wand(painter); break;
    case NavigationTool::crop: crop(painter); break;
    case NavigationTool::brush: kind == ToolIconKind::eraser ? eraser(painter) : brush(painter); break;
    case NavigationTool::spotHealing: spotHealing(painter); break;
    case NavigationTool::cloneStamp: cloneStamp(painter); break;
    case NavigationTool::blur: blur(painter); break;
    case NavigationTool::gradient: gradient(painter); break;
    case NavigationTool::shape: shape(painter); break;
    case NavigationTool::type: type(painter); break;
    case NavigationTool::eyedropper: EyedropperIcon::paint(painter); break;
    case NavigationTool::hand: hand(painter); break;
    case NavigationTool::zoom: zoom(painter); break;
    case NavigationTool::idle: break;
    }
    painter.restore();
}
