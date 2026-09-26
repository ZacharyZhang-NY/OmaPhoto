#pragma once
#include <QImage>
#include <QPainter>
#include <QRegion>
#include <optional>

// Swift's brush circle; the canvas draws it over its pixels.
class BrushCursorOverlay {
public:
    // Moves circle and marker; returns the old and new areas.
    QRegion update(std::optional<QPointF> point, double diameter, std::optional<double> hardness,
                   std::optional<QPointF> sample = std::nullopt, const QImage &preview = QImage(), double previewOpacity = 1,
                   const QImage &tip = QImage());
    void draw(QPainter &painter) const;
    std::optional<QRectF> circle() const { return m_circle; }
    std::optional<double> hardness() const { return m_hardness; }
    std::optional<QPointF> marker() const { return m_marker; }
    const QImage &preview() const { return m_preview; }
    const QImage &tip() const { return m_tip; }

private:
    static constexpr double markerReach = 7;
    std::optional<QRectF> m_circle;
    // Clone Stamp's source crosshair, in view points.
    std::optional<QPointF> m_marker;
    // What a Clone Stamp click would stamp, inside the circle.
    QImage m_preview;
    double m_previewOpacity = 1;
    // One click's coverage, shaping the preview's edge.
    QImage m_tip;
    // While hardness is dragged: the full-strength share, as a ring.
    std::optional<double> m_hardness;
};
