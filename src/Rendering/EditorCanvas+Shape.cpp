#include "Rendering/EditorCanvas.h"
#include <QPainterPath>
#include <cmath>

void CanvasView::drawShapeDraft(double scale, const Center &center, QPainter &target, const QImage &clip) const
{
    const std::optional<ShapeDraft> &draft = m_session.shapeDraft();
    // A flat or upright line has a box worth drawing.
    if (!draft || (draft->kind == ShapeKind::line ? draft->rect.width() <= 0 && draft->rect.height() <= 0 : draft->rect.isEmpty()))
        return;
    const QColor color = m_session.foregroundColor().color();
    const QPointF middle = center(draft->rect.center());
    const QRectF rect(middle.x() - draft->rect.width() * scale / 2, middle.y() - draft->rect.height() * scale / 2, draft->rect.width() * scale,
                      draft->rect.height() * scale);
    std::function<void(QPainter &)> body;
    QRectF extent = rect;
    if (draft->kind == ShapeKind::line) {
        // A line's box is set only with its end.
        const std::pair<QPointF, QPointF> ends = m_session.shapeLineEnds().value();
        const double thickness = std::max(1.0, m_session.shapeLineWidth() * scale);
        // Exactly the two points dragged between, so the start holds.
        const QPointF from = center(ends.first), to = center(ends.second);
        extent = QRectF(from, to).normalized().adjusted(-thickness, -thickness, thickness, thickness);
        body = [color, thickness, from, to](QPainter &aside) { aside.fillPath(strokedLine(from, to, thickness), color); };
    } else {
        const QPainterPath shape = path(draft->kind, rect, draft->cornerRadius * scale);
        body = [color, shape](QPainter &aside) { aside.fillPath(shape, color); };
    }
    // Composed aside, so folder masks clip it as its layer.
    LayerRenderer::composite(target, QTransform(), extent, LayerSampling::smooth, InterpolationQuality::high, {.scale = scale, .clip = clip},
                             extent, body);
}

std::optional<CanvasView::ShapeShown> CanvasView::shownShape() const
{
    const std::optional<ShapeDraft> &draft = m_session.shapeDraft();
    if (!draft)
        return std::nullopt;
    return ShapeShown{*draft, m_session.foregroundColor(), m_session.shapeLineWidth(), m_session.activeLayerID()};
}
