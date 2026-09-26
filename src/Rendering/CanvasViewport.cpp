#include "Rendering/CanvasViewport.h"
#include <algorithm>
#include <cmath>

QRectF CanvasViewport::documentRect(QSizeF size) const
{
    const QSizeF scaled(size.width() * pointsPerPixel(), size.height() * pointsPerPixel());
    return {center().x() - scaled.width() / 2 + pan.width(),
            center().y() - scaled.height() / 2 + pan.height(), scaled.width(), scaled.height()};
}

QPointF CanvasViewport::documentPoint(QPointF from, QSizeF documentSize) const
{
    const QPointF origin = documentRect(documentSize).topLeft();
    return {(from.x() - origin.x()) / pointsPerPixel(), (from.y() - origin.y()) / pointsPerPixel()};
}

QPointF CanvasViewport::viewPoint(QPointF from, QSizeF documentSize) const
{
    const QPointF origin = documentRect(documentSize).topLeft();
    return {origin.x() + from.x() * pointsPerPixel(), origin.y() + from.y() * pointsPerPixel()};
}

void CanvasViewport::fit(QSizeF documentSize)
{
    m_followsFit = true;
    if (viewSize.width() <= 0 || viewSize.height() <= 0)
        return;
    const double fitted = std::min(std::max(1.0, viewSize.width() - 96) / documentSize.width(),
                                   std::max(1.0, viewSize.height() - 96) / documentSize.height());
    m_zoom = std::clamp(fitted * backingScale, minimumZoom, maximumZoom);
    pan = {0, 0};
}

void CanvasViewport::resize(QSizeF to, double newScale, std::optional<QSizeF> documentSize)
{
    const double oldScale = pointsPerPixel();
    viewSize = to;
    backingScale = std::max(1.0, newScale);
    if (m_followsFit && documentSize) {
        fit(*documentSize);
        return;
    }
    const double ratio = pointsPerPixel() / oldScale;
    pan = {pan.width() * ratio, pan.height() * ratio};
}

void CanvasViewport::setZoom(double value, QPointF anchoredAt, QSizeF documentSize)
{
    if (!std::isfinite(value))
        return;
    const QPointF pixel = documentPoint(anchoredAt, documentSize);
    m_zoom = std::clamp(value, minimumZoom, maximumZoom);
    const QPointF moved = viewPoint(pixel, documentSize);
    pan += QSizeF(anchoredAt.x() - moved.x(), anchoredAt.y() - moved.y());
    m_followsFit = false;
}

void CanvasViewport::translate(QSizeF by)
{
    pan += by;
    m_followsFit = false;
}

bool operator==(const CanvasViewport &lhs, const CanvasViewport &rhs)
{
    return lhs.viewSize.width() == rhs.viewSize.width() && lhs.viewSize.height() == rhs.viewSize.height()
        && lhs.backingScale == rhs.backingScale && lhs.m_zoom == rhs.m_zoom
        && lhs.pan.width() == rhs.pan.width() && lhs.pan.height() == rhs.pan.height()
        && lhs.m_followsFit == rhs.m_followsFit;
}
