#pragma once
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <array>
#include <optional>

// Document: top-left pixels. View: widget points. Zoom 1: device pixels.
class CanvasViewport {
public:
    static constexpr double minimumZoom = 0.001;
    static constexpr double maximumZoom = 32;
    // Swift's keyboard zoom stops, 12.5% to 1600%.
    static constexpr std::array keyboardZoomLevels{0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 1.25, 1.5,
                                                   2.0,   3.0,       4.0,  5.0,       6.0, 8.0,       12.0, 16.0};

    QSizeF viewSize{0, 0};
    double backingScale = 1;
    QSizeF pan{0, 0};

    double zoom() const { return m_zoom; }
    bool followsFit() const { return m_followsFit; }
    double pointsPerPixel() const { return m_zoom / backingScale; }
    QPointF center() const { return {viewSize.width() / 2, viewSize.height() / 2}; }

    QRectF documentRect(QSizeF size) const;
    QPointF documentPoint(QPointF from, QSizeF documentSize) const;
    QPointF viewPoint(QPointF from, QSizeF documentSize) const;
    void fit(QSizeF documentSize);
    void resize(QSizeF to, double backingScale, std::optional<QSizeF> documentSize);
    void setZoom(double value, QPointF anchoredAt, QSizeF documentSize);
    // The next stop past the zoom, else the zoom.
    double keyboardZoomTarget(int step) const;
    void translate(QSizeF by);
    friend bool operator==(const CanvasViewport &lhs, const CanvasViewport &rhs);

private:
    double m_zoom = 1;
    bool m_followsFit = true;
};
