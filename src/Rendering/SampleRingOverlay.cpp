#include "Rendering/SampleRingOverlay.h"

QRegion SampleRingOverlay::update(std::optional<QPointF> point, const PaletteColor &original, const PaletteColor &sampled)
{
    QRegion dirty;
    if (m_frame)
        dirty += m_frame->toAlignedRect();
    m_frame = point ? std::optional(QRectF(point->x() - 58, point->y() - 58, 116, 116)) : std::nullopt;
    m_original = original;
    m_sampled = sampled;
    if (m_frame)
        dirty += m_frame->toAlignedRect();
    return dirty;
}

void SampleRingOverlay::draw(QPainter &painter) const
{
    if (!m_frame)
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    const QRectF ring = m_frame->adjusted(15, 15, -15, -15);
    painter.setPen(QPen(QColor::fromRgbF(0.45f, 0.45f, 0.45f), 24));
    painter.drawEllipse(ring);
    // The sampled colour on top, the original below; restore unclips.
    for (const auto &[colour, top] : {std::pair(m_sampled, m_frame->top()), std::pair(m_original, m_frame->center().y())}) {
        painter.setClipRect(QRectF(m_frame->left(), top, m_frame->width(), m_frame->height() / 2));
        painter.setPen(QPen(colour.color(), 16));
        painter.drawEllipse(ring);
    }
    painter.restore();
}
