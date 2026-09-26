#include "Rendering/BrushCursorOverlay.h"
#include "Rendering/AdjustmentSurface.h"
#include <QPainterPath>

namespace {
// A white line under a black one, as Swift's.
void stroked(QPainter &painter, const QRectF &ellipse, bool dashed)
{
    for (const auto &[colour, width] : {std::pair(QColor(Qt::white), 2.5), std::pair(QColor(Qt::black), 1.0)}) {
        QPen pen(colour, width, Qt::SolidLine, Qt::FlatCap);
        // Four on, three off, in points: Qt counts in widths.
        if (dashed)
            pen.setDashPattern({4 / width, 3 / width});
        painter.setPen(pen);
        painter.drawEllipse(ellipse);
    }
}

QRect around(QPointF point, double reach)
{
    return QRectF(point.x() - reach, point.y() - reach, reach * 2, reach * 2).toAlignedRect();
}
}

QRegion BrushCursorOverlay::update(std::optional<QPointF> point, double diameter, std::optional<double> hardness, std::optional<QPointF> sample,
                                   const QImage &preview, double previewOpacity, const QImage &tip)
{
    const std::optional<QRectF> next = point ? std::optional(QRectF(point->x() - diameter / 2, point->y() - diameter / 2, diameter, diameter)) : std::nullopt;
    QRegion dirty;
    // Images compare by identity, as Swift's `!==`.
    if (m_circle != next || m_preview.cacheKey() != preview.cacheKey() || m_previewOpacity != previewOpacity || m_tip.cacheKey() != tip.cacheKey()
        || m_hardness != hardness) {
        if (m_circle)
            dirty += m_circle->adjusted(-3, -3, 3, 3).toAlignedRect();
        m_circle = next;
        m_preview = preview;
        m_previewOpacity = previewOpacity;
        m_tip = tip;
        m_hardness = hardness;
        if (m_circle)
            dirty += m_circle->adjusted(-3, -3, 3, 3).toAlignedRect();
    }
    if (m_marker != sample) {
        if (m_marker)
            dirty += around(*m_marker, markerReach + 3);
        m_marker = sample;
        if (m_marker)
            dirty += around(*m_marker, markerReach + 3);
    }
    return dirty;
}

void BrushCursorOverlay::draw(QPainter &painter) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    if (m_circle) {
        if (!m_preview.isNull()) {
            painter.save();
            QPainterPath ellipse;
            ellipse.addEllipse(*m_circle);
            painter.setClipPath(ellipse, Qt::IntersectClip);
            painter.setOpacity(painter.opacity() * m_previewOpacity);
            // Swift's transparency layer: the tip cuts at drawing resolution.
            AdjustmentSurface::draw(painter, [this](QPainter &layer) {
                layer.setRenderHint(QPainter::SmoothPixmapTransform);
                layer.drawImage(*m_circle, m_preview);
                // Only what one click lays down: soft tips preview softly.
                layer.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                layer.drawImage(*m_circle, m_tip);
            });
            painter.restore();
        }
        stroked(painter, *m_circle, false);
        // At zero hardness the inner ellipse has no size.
        if (m_hardness) {
            const double inset = m_circle->width() * (1 - *m_hardness) / 2;
            stroked(painter, m_circle->adjusted(inset, inset, -inset, -inset), true);
        }
    }
    if (m_marker) {
        QPainterPath arms;
        arms.moveTo(m_marker->x() - markerReach, m_marker->y());
        arms.lineTo(m_marker->x() + markerReach, m_marker->y());
        arms.moveTo(m_marker->x(), m_marker->y() - markerReach);
        arms.lineTo(m_marker->x(), m_marker->y() + markerReach);
        for (const auto &[colour, width] : {std::pair(QColor(Qt::white), 3.0), std::pair(QColor(Qt::black), 1.0)})
            painter.strokePath(arms, QPen(colour, width, Qt::SolidLine, Qt::RoundCap));
    }
    painter.restore();
}
