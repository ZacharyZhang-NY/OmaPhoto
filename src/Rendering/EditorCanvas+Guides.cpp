#include "Rendering/EditorCanvas.h"

// Swift's isOverRuler: rulers sit above and left.
bool CanvasView::isOverRuler(QPointF point) const
{
    return m_session.showsRulers() && (point.x() < 0 || point.y() < 0);
}

std::optional<double> CanvasView::documentPosition(CanvasGuide::Axis axis, QPointF point) const
{
    const std::optional<CanvasDocument> &document = m_session.document();
    if (!document)
        return std::nullopt;
    const QPointF pixel = m_session.viewport.documentPoint(point, document->size());
    return axis == CanvasGuide::Axis::vertical ? pixel.x() : pixel.y();
}

// A press on a guide moves it, before any layer.
bool CanvasView::beginGuideDrag(QPointF point)
{
    const std::optional<CanvasGuide> guide = m_session.canEditGuides() ? m_session.hitGuide(point) : std::nullopt;
    if (!guide)
        return false;
    m_session.beginGuideMove(*guide);
    m_guideDragging = true;
    m_dragCursor = QCursor(guide->axis == CanvasGuide::Axis::vertical ? Qt::SizeHorCursor : Qt::SizeVerCursor);
    setCursor(*m_dragCursor);
    return true;
}

// A move without the left button: Qt lost the release.
bool CanvasView::guideMove(QPointF point, Qt::MouseButtons buttons)
{
    if (!m_guideDragging)
        return false;
    if (!buttons.testFlag(Qt::LeftButton)) {
        endGuideDrag(point);
        return true;
    }
    const std::optional<GuideDrag> &drag = m_session.guideDrag();
    if (const std::optional<double> position = drag ? documentPosition(drag->axis, point) : std::nullopt)
        m_session.moveGuideDrag(*position);
    synchronizeDisplay();
    return true;
}

// Released over a ruler, the guide goes.
void CanvasView::endGuideDrag(QPointF point)
{
    if (!m_guideDragging)
        return;
    m_guideDragging = false;
    m_dragCursor.reset();
    m_session.finishGuideDrag(isOverRuler(point));
    updateCursor();
}
