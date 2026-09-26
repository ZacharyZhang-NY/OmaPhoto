#include "Rendering/EditorCanvas.h"
#include <QKeyEvent>
#include <array>

namespace {
// CGRect's contains: the far edges lie outside.
bool contains(const QRectF &rect, QPointF point)
{
    return point.x() >= rect.left() && point.x() < rect.right() && point.y() >= rect.top() && point.y() < rect.bottom();
}

// Swift's frame positions, top left first, as Qt's shapes.
Qt::CursorShape resizeShape(int index)
{
    constexpr std::array<Qt::CursorShape, 8> shapes = {Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor,
                                                       Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor};
    return shapes[size_t(index)];
}
}

// The grip under the pointer: the first region holding it.
std::optional<int> CanvasView::cropRegion(QPointF point) const
{
    for (const TransformOverlay::CropRegion &region : m_overlay.cropResizeRegions()) {
        if (contains(region.rect, point))
            return region.index;
    }
    return std::nullopt;
}

QCursor CanvasView::cropCursor(QPointF point) const
{
    const std::optional<int> index = cropRegion(point);
    return index ? QCursor(resizeShape(*index)) : QCursor(Qt::CrossCursor);
}

// A grip resizes, a drawn frame moves, elsewhere draws anew.
void CanvasView::beginCropDrag(QPointF point)
{
    const QSizeF size = m_session.document().value().size();
    const QPointF pixel = m_session.viewport.documentPoint(point, size);
    // Swift's current cursor: the old frame's, kept until the release.
    const QCursor cursor = cropCursor(point);
    // Swift's fallback is dead: the Crop tool has a frame.
    const QRectF rect = m_session.visibleCropRect().value();
    CropDrag::Mode mode{CropDrag::Kind::create};
    if (const std::optional<int> index = cropRegion(point))
        mode = {CropDrag::Kind::resize, *index};
    else if (m_session.cropRect() && contains(*m_session.cropRect(), pixel) && rect != QRectF(QPointF(0, 0), size))
        mode = {CropDrag::Kind::move};
    else
        m_session.setCropRect(std::nullopt);
    m_cropDrag = CropDrag{pixel, rect, mode};
    const SnapGuides targets = m_session.cropSnapTargets();
    m_cropSnap = CropSnap{targets.xs, targets.ys, cropSnapDistance / std::max(m_session.viewport.pointsPerPixel(), 0.0001)};
    m_dragCursor = cursor;
    updateCursor();
}

// Without the left button the release was lost: drag ends.
bool CanvasView::cropMove(QPointF point, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers)
{
    if (!m_cropDrag)
        return false;
    if (!buttons.testFlag(Qt::LeftButton)) {
        endCropDrag();
        return false;
    }
    if (m_session.tool() != NavigationTool::crop || m_session.isProjectBusy() || !m_session.document())
        return false;
    dragCrop(point, modifiers);
    synchronizeDisplay();
    return true;
}

// Alt keeps the middle still; Ctrl leaves the edges unsnapped.
void CanvasView::dragCrop(QPointF point, Qt::KeyboardModifiers modifiers)
{
    const QPointF pixel = m_session.viewport.documentPoint(point, m_session.document().value().size());
    const bool symmetric = modifiers.testFlag(Qt::AltModifier);
    const std::optional<double> ratio = m_session.cropRatio();
    const CropDrag &drag = m_cropDrag.value();
    QRectF next = drag.updated(pixel, ratio, symmetric);
    if (m_session.snappingEnabled() && !modifiers.testFlag(Qt::ControlModifier))
        next = m_cropSnap.value().apply(next, drag, pixel, ratio, symmetric);
    if (CropGeometry::valid(next))
        m_session.setCropRect(next);
}

// Swift's cropDrag = nil: its didSet lets the cursor go.
void CanvasView::endCropDrag()
{
    if (!m_cropDrag)
        return;
    m_cropDrag.reset();
    m_dragCursor.reset();
    updateCursor();
}

// Escape drops the frame; Return and Enter crop to it.
bool CanvasView::cropKey(const QKeyEvent &key)
{
    const bool enter = key.key() == Qt::Key_Return || key.key() == Qt::Key_Enter;
    if (m_session.tool() != NavigationTool::crop || !(enter || key.key() == Qt::Key_Escape))
        return false;
    endCropDrag();
    if (enter)
        m_session.commitCrop();
    else
        m_session.cancelCrop();
    return true;
}
