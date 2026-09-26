#include "Rendering/EditorCanvas.h"
#include <cmath>
#include <numbers>

// Grabs a pending end, else starts a line there.
void CanvasView::beginGradientDrag(QPointF point)
{
    if (!m_session.document())
        return;
    if (const std::optional<std::pair<QPointF, QPointF>> line = m_overlay.gradientLine()) {
        if (std::hypot(point.x() - line->second.x(), point.y() - line->second.y()) <= 10) {
            m_gradientDrag = GradientHandle::end;
            return;
        }
        if (std::hypot(point.x() - line->first.x(), point.y() - line->first.y()) <= 10) {
            m_gradientDrag = GradientHandle::start;
            return;
        }
    }
    m_session.beginGradient(m_session.viewport.documentPoint(point, m_session.document()->size()));
    m_gradientDrag = m_session.gradientEdit() ? std::optional(GradientHandle::end) : std::nullopt;
    synchronizeDisplay();
}

bool CanvasView::dragGradient(QPointF point, Qt::KeyboardModifiers modifiers)
{
    const std::optional<GradientEdit> &edit = m_session.gradientEdit();
    if (!m_gradientDrag || !edit || !m_session.document())
        return false;
    const bool start = *m_gradientDrag == GradientHandle::start;
    QPointF pixel = m_session.viewport.documentPoint(point, m_session.document()->size());
    if (modifiers.testFlag(Qt::ShiftModifier))
        pixel = snapped(pixel, start ? edit->end : edit->start);
    m_session.moveGradient(start ? std::optional(pixel) : std::nullopt, start ? std::nullopt : std::optional(pixel));
    synchronizeDisplay();
    return true;
}

QPointF CanvasView::snapped(QPointF point, QPointF anchor)
{
    const double dx = point.x() - anchor.x(), dy = point.y() - anchor.y();
    const double length = std::hypot(dx, dy);
    const double angle = std::round(std::atan2(dy, dx) / (std::numbers::pi / 4)) * (std::numbers::pi / 4);
    return {anchor.x() + std::cos(angle) * length, anchor.y() + std::sin(angle) * length};
}
