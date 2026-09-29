#pragma once
#include "Document/EditorSession.h"
#include <QFutureWatcher>
#include <QPainter>
#include <QPalette>
#include <array>
#include <optional>
#include <vector>

// The Move tool's box on screen: handles and a grip.
struct TransformOverlayGeometry {
    std::array<QPointF, 8> handles;
    QPointF rotationHandle;
    // A distortion has no single rotation: its grip is hidden.
    bool showsRotation;

    TransformOverlayGeometry(const LayerTransform &transform, const CanvasViewport &viewport, QSizeF documentSize);
    // A distortion: its corners and the middles of its edges.
    TransformOverlayGeometry(const Corners &corners, const CanvasViewport &viewport, QSizeF documentSize);

    // What a press takes hold of, within ten points.
    std::optional<TransformDrag::Mode> hit(QPointF point) const;
    // The resize cursor along the handle's edge, turned with it.
    Qt::CursorShape resizeCursor(int index) const;
    friend bool operator==(const TransformOverlayGeometry &, const TransformOverlayGeometry &) = default;
};

// Drawn by the canvas over its pixels: Swift's overlay view.
class TransformOverlay {
public:
    explicit TransformOverlay(const EditorSession &session) : m_session(session) {}

    // The marching ants' dash offset, stepped by the canvas.
    double antsPhase = 0;
    // Asked for when a traced outline lands: repaint the ants.
    std::function<void()> repaintAnts;

    std::optional<TransformOverlayGeometry> geometry() const;
    // Swift's gradientLine: the pending gradient's ends in view points.
    std::optional<std::pair<QPointF, QPointF>> gradientLine() const;
    // Where it draws, so the canvas repaints only that.
    QRect drawnRect(const QRect &canvas) const;
    // Where the ants draw: a tick repaints that alone.
    QRect selectionRect(const QRect &canvas) const;
    // Swift's crop frame in view points; none off the tool.
    std::optional<QRectF> cropViewRect() const;
    // A grip and the view points it takes, corners first.
    struct CropRegion {
        int index;
        QRectF rect;
    };
    std::vector<CropRegion> cropResizeRegions() const;
    void draw(QPainter &context, const QPalette &palette) const;

private:
    QTransform documentToView() const;
    void drawLayoutGrid(QPainter &context) const;
    void drawGuides(QPainter &context) const;
    void drawTransformHandles(QPainter &context, const QPalette &palette) const;
    void drawGradientLine(QPainter &context, QPointF start, QPointF end) const;
    void drawCrop(QPainter &context) const;
    void drawSelection(QPainter &context) const;
    // Swift's antsOutline: the path, or zoomed out, one traced.
    std::optional<QPainterPath> antsOutline(const QPainterPath &path, double deviceScale) const;
    static std::optional<QPainterPath> traceOutline(const QPainterPath &path, QRectF canvas, double step);
    void drawLassoDraft(QPainter &context) const;
    void drawSnapGuides(QPainter &context, const QPalette &palette) const;

    const EditorSession &m_session;
    // Swift's level of detail, cached per power-of-two zoom step.
    struct AntsLevel {
        QPainterPath path;
        double step;
    };
    mutable std::optional<QPainterPath> m_antsSource;
    mutable bool m_antsSourceIsComplex = false;
    mutable std::optional<AntsLevel> m_antsLevel;
    mutable std::optional<double> m_antsPendingStep;
    // A new trace replaces the watcher: the old result drops.
    mutable std::unique_ptr<QFutureWatcher<std::optional<QPainterPath>>> m_antsTask;
};
