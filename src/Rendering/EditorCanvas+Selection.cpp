#include "Rendering/EditorCanvas.h"
#include "Logging.h"
#include <QKeyEvent>
#include <QPointer>
#include <cmath>

// The Marquee and the Lasso on the canvas.
namespace {
bool isMarquee(const std::optional<LassoDraft> &draft)
{
    return draft && (draft->kind == LassoKind::rectangle || draft->kind == LassoKind::ellipse);
}
}

// Over the selection in New mode, the move cursor.
QCursor CanvasView::lassoCursor(Qt::KeyboardModifiers modifiers, std::optional<QPointF> location) const
{
    const double ratio = devicePixelRatio();
    const SelectionMode mode = m_session.lassoCursorMode(modifiers.testFlag(Qt::ShiftModifier), modifiers.testFlag(Qt::AltModifier));
    // Ctrl over the selection: the scissors, or Ctrl-Alt's copy.
    const std::optional<CanvasDocument> &document = m_session.document();
    const bool command = modifiers.testFlag(Qt::ControlModifier);
    if ((command || mode == SelectionMode::replace) && document && location
        && m_session.canMoveSelection(m_session.viewport.documentPoint(*location, document->size())))
        return command ? pixelDragCursor(modifiers.testFlag(Qt::AltModifier), ratio) : moveSelectionCursor(ratio);
    const bool wand = m_session.tool() == NavigationTool::wand;
    if (wand && m_session.wandMode() == WandMode::wand)
        return wandCursor(mode, ratio);
    const SelectionIcon icon = wand ? SelectionIcon::objectSelection
        : m_session.tool() == NavigationTool::marquee
        ? (m_session.marqueeKind() == LassoKind::ellipse ? SelectionIcon::ellipseMarquee : SelectionIcon::rectangleMarquee)
        : (m_session.lassoKind() == LassoKind::polygonal ? SelectionIcon::polygonalLasso : SelectionIcon::freehandLasso);
    return selectionCursor(icon, mode, ratio);
}

// Freehand drags an outline; Polygonal adds a corner per click.
void CanvasView::lassoMouseDown(QPointF point, Qt::KeyboardModifiers modifiers)
{
    const QSizeF size = m_session.document().value().size();
    // Shift at the press means Add, never a square yet.
    m_marqueeConstrainArmed = !modifiers.testFlag(Qt::ShiftModifier);
    m_marqueeDragPixel = std::nullopt;
    const QPointF pixel = m_session.viewport.documentPoint(point, size);
    const std::optional<LassoDraft> &draft = m_session.lassoDraft();
    if (!draft || draft->kind != LassoKind::polygonal) {
        // Ctrl-drag inside the selection cuts and moves its pixels.
        if (modifiers.testFlag(Qt::ControlModifier) && m_session.canMoveSelection(pixel)) {
            if (m_session.beginPixelMove(modifiers.testFlag(Qt::AltModifier)))
                m_pixelDragStart = pixel;
            else
                qCWarning(lcApp) << "the selection holds no pixels to move";
            updateCursor();
            return;
        }
        const SelectionMode mode = m_session.selectionMode(modifiers.testFlag(Qt::ShiftModifier), modifiers.testFlag(Qt::AltModifier));
        if (mode == SelectionMode::replace && m_session.canMoveSelection(pixel) && m_session.beginSelectionMove()) {
            m_selectionDragStart = pixel;
            updateCursor();
            return;
        }
        // Object mode runs before the colour wand, as Swift's.
        if (m_session.tool() == NavigationTool::wand && m_session.wandMode() == WandMode::object) {
            m_session.selectObject(pixel, mode);
            return;
        }
        if (m_session.tool() == NavigationTool::wand) {
            m_session.magicWand(pixel, mode);
            return;
        }
        m_session.beginLasso(pixel, mode);
        synchronizeDisplay();
        return;
    }
    // Near the first corner, with three, the outline closes.
    const QPointF first = m_session.viewport.viewPoint(draft->points.front(), size);
    if (draft->points.size() >= 3 && std::hypot(point.x() - first.x(), point.y() - first.y()) <= 8)
        m_session.finishLasso();
    else
        m_session.extendLasso(pixel);
    synchronizeDisplay();
}

// A drag or a hover; true when taken.
bool CanvasView::moveSelectionTool(QPointF point, Qt::KeyboardModifiers modifiers)
{
    if (m_pixelDragStart && m_session.document()) {
        const QPointF pixel = m_session.viewport.documentPoint(point, m_session.document()->size());
        m_session.movePixels(QSizeF(pixel.x() - m_pixelDragStart->x(), pixel.y() - m_pixelDragStart->y()));
        updateCursor();
        synchronizeDisplay();
        return true;
    }
    if (m_selectionDragStart) {
        dragSelection(point, modifiers);
        updateMarqueeAutoscroll(point);
        updateCursor();
        synchronizeDisplay();
        return true;
    }
    const std::optional<LassoDraft> &draft = m_session.lassoDraft();
    if (!isSelectionTool(m_session.tool()) || !m_session.document())
        return false;
    const QPointF pixel = m_session.viewport.documentPoint(point, m_session.document()->size());
    if (!draft || draft->kind == LassoKind::polygonal) {
        // Keys may have changed while the app was away.
        m_session.updateHeldSelectionKeys(modifiers.testFlag(Qt::ShiftModifier), modifiers.testFlag(Qt::AltModifier));
        updateCursor();
        if (draft)
            m_session.moveLassoCursor(pixel);
        synchronizeDisplay();
        return true;
    }
    if (draft->kind == LassoKind::freehand) {
        m_session.extendLasso(pixel);
    } else {
        dragMarqueeDraft(pixel, modifiers);
        updateMarqueeAutoscroll(point);
    }
    synchronizeDisplay();
    return true;
}

void CanvasView::releaseSelectionTool()
{
    stopMarqueeAutoscroll();
    if (m_pixelDragStart) {
        m_pixelDragStart = std::nullopt;
        m_session.finishPixelMove([canvas = QPointer<CanvasView>(this)] {
            if (!canvas)
                return;
            canvas->synchronizeDisplay();
            canvas->updateCursor();
        });
    }
    if (m_selectionDragStart) {
        const QPointF start = *std::exchange(m_selectionDragStart, std::nullopt);
        const bool moved = m_session.selectionMoveOrigin() != m_session.selection();
        m_session.endSelectionMove();
        // The wand's click inside selects afresh; another's deselects.
        if (!moved && m_session.tool() == NavigationTool::wand && m_session.wandMode() == WandMode::object)
            m_session.selectObject(start, SelectionMode::replace);
        else if (!moved && m_session.tool() == NavigationTool::wand)
            m_session.magicWand(start, SelectionMode::replace);
        else if (!moved)
            m_session.deselect();
        synchronizeDisplay();
        updateCursor();
    }
    const std::optional<LassoDraft> &draft = m_session.lassoDraft();
    if (isSelectionTool(m_session.tool()) && draft && draft->kind != LassoKind::polygonal) {
        m_session.finishLasso();
        synchronizeDisplay();
        updateCursor();
    }
}

// Escape, Return, Backspace and Delete on a draft; arrows nudge.
bool CanvasView::selectionKey(const QKeyEvent &key)
{
    const bool enter = key.key() == Qt::Key_Return || key.key() == Qt::Key_Enter;
    const bool plain = !(key.modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    if (m_session.lassoDraft() && (key.key() == Qt::Key_Escape || enter || key.key() == Qt::Key_Backspace || key.key() == Qt::Key_Delete)) {
        if (key.key() == Qt::Key_Escape)
            m_session.cancelLasso();
        else if (enter)
            m_session.finishLasso();
        else
            m_session.removeLastLassoPoint();
        synchronizeDisplay();
        updateCursor();
        return true;
    }
    const std::optional<DocumentSelection> selection = m_session.selection();
    const bool arrow = key.key() == Qt::Key_Left || key.key() == Qt::Key_Right || key.key() == Qt::Key_Up || key.key() == Qt::Key_Down;
    if (isSelectionTool(m_session.tool()) && !m_session.lassoDraft() && selection && !selection->isEmpty() && arrow && plain) {
        const double step = key.modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1;
        m_session.nudgeSelection(key.key() == Qt::Key_Left ? -step : key.key() == Qt::Key_Right ? step : 0,
                                 key.key() == Qt::Key_Up ? -step : key.key() == Qt::Key_Down ? step : 0);
        return true;
    }
    // M, W and L choose tools; a repeat does nothing.
    if (plain && (key.key() == Qt::Key_M || key.key() == Qt::Key_W || key.key() == Qt::Key_L)) {
        if (key.isAutoRepeat())
            return true;
        if (key.key() == Qt::Key_M)
            m_session.pressMarqueeKey();
        else if (key.key() == Qt::Key_W)
            m_session.pressWandKey();
        else
            m_session.pressLassoKey();
        updateCursor();
        return true;
    }
    return false;
}

// Losing the keys ends a drag and an open outline.
void CanvasView::endSelectionGestures()
{
    stopMarqueeAutoscroll();
    const std::optional<LassoDraft> &draft = m_session.lassoDraft();
    if (draft && draft->kind != LassoKind::polygonal)
        m_session.cancelLasso();
    if (m_selectionDragStart) {
        m_selectionDragStart = std::nullopt;
        m_session.endSelectionMove();
    }
    if (m_pixelDragStart) {
        m_pixelDragStart = std::nullopt;
        m_session.cancelPixelMove();
    }
}

// The grabbed pixel follows the pointer; Shift keeps one axis.
void CanvasView::dragSelection(QPointF point, Qt::KeyboardModifiers modifiers)
{
    if (!m_selectionDragStart || !m_session.document())
        return;
    const QPointF pixel = m_session.viewport.documentPoint(point, m_session.document()->size());
    QSizeF offset(pixel.x() - m_selectionDragStart->x(), pixel.y() - m_selectionDragStart->y());
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        if (std::abs(offset.width()) >= std::abs(offset.height()))
            offset.setHeight(0);
        else
            offset.setWidth(0);
    }
    m_session.moveSelection(offset);
}

// Alt subtracts, never from the centre; Shift squares once armed.
void CanvasView::dragMarqueeDraft(QPointF pixel, Qt::KeyboardModifiers modifiers)
{
    if (!modifiers.testFlag(Qt::ShiftModifier))
        m_marqueeConstrainArmed = true;
    m_marqueeDragPixel = pixel;
    m_session.dragMarquee(pixel, m_marqueeConstrainArmed && modifiers.testFlag(Qt::ShiftModifier), false);
}

// A frame's pan toward a pointer at the edge.
QSizeF CanvasView::marqueeAutoscrollDelta(QPointF point) const
{
    const QRectF bounds(QPointF(0, 0), QSizeF(size()));
    constexpr double margin = 12;
    const auto speed = [](double past) { return past <= 0 ? 0.0 : std::min(40.0, 2 + past * 0.4); };
    const double left = speed(bounds.left() + margin - point.x()), right = speed(point.x() - (bounds.right() - margin));
    const double top = speed(bounds.top() + margin - point.y()), bottom = speed(point.y() - (bounds.bottom() - margin));
    // Past the right edge the document slides left.
    return QSizeF(left - right, top - bottom);
}

void CanvasView::updateMarqueeAutoscroll(QPointF point)
{
    m_marqueeAutoscrollPoint = point;
    if (marqueeAutoscrollDelta(point) == QSizeF(0, 0)) {
        stopMarqueeAutoscroll();
        return;
    }
    if (!m_marqueeAutoscroll.isActive())
        m_marqueeAutoscroll.start();
}

void CanvasView::stepMarqueeAutoscroll()
{
    const bool marquee = isMarquee(m_session.lassoDraft());
    if (!m_marqueeAutoscrollPoint || !m_session.document() || !(marquee || m_selectionDragStart)) {
        stopMarqueeAutoscroll();
        return;
    }
    const QPointF point = *m_marqueeAutoscrollPoint;
    const QSizeF delta = marqueeAutoscrollDelta(point);
    if (delta == QSizeF(0, 0)) {
        stopMarqueeAutoscroll();
        return;
    }
    m_session.viewport.translate(delta);
    // The document moved under a still pointer: the box follows.
    if (m_selectionDragStart)
        dragSelection(point, heldModifiers());
    else
        dragMarqueeDraft(m_session.viewport.documentPoint(point, m_session.document()->size()), heldModifiers());
    m_session.notify();
    synchronizeDisplay();
}

void CanvasView::stopMarqueeAutoscroll()
{
    m_marqueeAutoscroll.stop();
    m_marqueeAutoscrollPoint = std::nullopt;
}

void CanvasView::stepAnts()
{
    if (m_antsRepaintPending)
        return;
    m_overlay.antsPhase = std::fmod(m_overlay.antsPhase + 1, 8.0);
    m_antsRepaintPending = true;
    update(m_overlay.selectionRect(rect()));
}

// Marching ants animate only while a visible selection exists.
void CanvasView::updateAntsTimer()
{
    const std::optional<DocumentSelection> selection = m_session.displayedSelection();
    const bool active = selection && !selection->isEmpty() && isVisible();
    if (active && !m_antsTimer.isActive())
        m_antsTimer.start();
    else if (!active && m_antsTimer.isActive())
        m_antsTimer.stop();
}
