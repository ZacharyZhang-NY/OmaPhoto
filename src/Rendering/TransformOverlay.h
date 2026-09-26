#pragma once
#include "Document/EditorSession.h"
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
    void drawTransformHandles(QPainter &context, const QPalette &palette) const;
    void drawGradientLine(QPainter &context, QPointF start, QPointF end) const;
    void drawCrop(QPainter &context) const;
    void drawSelection(QPainter &context) const;
    void drawLassoDraft(QPainter &context) const;
    void drawSnapGuides(QPainter &context, const QPalette &palette) const;

    const EditorSession &m_session;
};
