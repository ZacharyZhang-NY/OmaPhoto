#include "Rendering/EditorCanvas.h"
#include <QPainterPath>
#include <cmath>

// One-screen-pixel lines on document pixel boundaries, over the image only.
void CanvasView::drawPixelGrid(const QRectF &view, const CanvasDocument &document, QPainter &context) const
{
    const CanvasViewport &viewport = m_session.viewport;
    const double points = viewport.pointsPerPixel();
    const QRectF canvas(viewport.viewPoint(QPointF(0, 0), document.size()), QSizeF(document.width * points, document.height * points));
    const QRectF area = view.intersected(canvas);
    if (area.isEmpty())
        return;
    const QPointF first = viewport.documentPoint(area.topLeft(), document.size());
    const QPointF last = viewport.documentPoint(area.bottomRight(), document.size());
    const double hairline = 1 / viewport.backingScale;
    // Crossings fill once, as CoreGraphics fills the union.
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    for (int column = int(std::ceil(first.x())); column <= int(std::floor(last.x())); ++column) {
        const double x = viewport.viewPoint(QPointF(column, 0), document.size()).x();
        path.addRect(QRectF(x - hairline / 2, area.top(), hairline, area.height()));
    }
    for (int row = int(std::ceil(first.y())); row <= int(std::floor(last.y())); ++row) {
        const double y = viewport.viewPoint(QPointF(0, row), document.size()).y();
        path.addRect(QRectF(area.left(), y - hairline / 2, area.width(), hairline));
    }
    context.save();
    context.setRenderHint(QPainter::Antialiasing, true);
    context.fillPath(path, QColor::fromRgbF(0.55, 0.55, 0.55, 0.45));
    context.restore();
}
