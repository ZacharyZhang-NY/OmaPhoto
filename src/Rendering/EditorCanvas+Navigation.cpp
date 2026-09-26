#include "Rendering/EditorCanvas.h"
#include "UI/FloatingPanel.h"
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleHints>
#include <QWheelEvent>
#include <QWindow>
#include <cmath>

namespace {
bool isArrow(int key)
{
    return key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down;
}

std::optional<NavigationTool> toolForKey(int key)
{
    switch (key) {
    case Qt::Key_B:
    case Qt::Key_E:
        return NavigationTool::brush;
    case Qt::Key_J:
        return NavigationTool::spotHealing;
    case Qt::Key_S:
        return NavigationTool::cloneStamp;
    case Qt::Key_T:
        return NavigationTool::type;
    case Qt::Key_G:
        return NavigationTool::gradient;
    case Qt::Key_U:
        return NavigationTool::shape;
    case Qt::Key_I:
        return NavigationTool::eyedropper;
    case Qt::Key_W:
        return NavigationTool::wand;
    case Qt::Key_A:
        return NavigationTool::idle;
    case Qt::Key_R:
        return NavigationTool::blur;
    case Qt::Key_C:
        return NavigationTool::crop;
    case Qt::Key_V:
        return NavigationTool::move;
    case Qt::Key_H:
        return NavigationTool::hand;
    case Qt::Key_Z:
        return NavigationTool::zoom;
    default:
        return std::nullopt;
    }
}
}

bool CanvasView::event(QEvent *event)
{
    // Open text takes its editing keys ahead of the menus.
    if (event->type() == QEvent::ShortcutOverride && m_inlineTextEditor && InlineTextEditor::claims(*static_cast<QKeyEvent *>(event))) {
        event->accept();
        return true;
    }
    // A text view keeps Shift-Tab: the focus stays put.
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Backtab && m_inlineTextEditor)
        return true;
    // Tab cycles a tool's mode, as Swift's canvas takes it.
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Tab) {
        keyPressEvent(static_cast<QKeyEvent *>(event));
        return true;
    }
    if (event->type() == QEvent::NativeGesture) {
        const auto *gesture = static_cast<QNativeGestureEvent *>(event);
        // A pinch, as Swift's magnify; not mid-drag or mid-stroke.
        if (gesture->gestureType() == Qt::ZoomNativeGesture && !m_transformDrag && !m_cropDrag && !m_session.brushStroke() && !m_session.warpStroke()) {
            m_session.zoom(m_session.viewport.zoom() * (1 + gesture->value()), gesture->position());
            event->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

void CanvasView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    syncGeometry();
}

void CanvasView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    syncGeometry();
    // The window's keys and Alt, as Swift's monitors watch them.
    qApp->installEventFilter(this);
    // Alt, Ctrl and Shift may be down already.
    readModifiers(QApplication::keyboardModifiers());
    updateCursor();
    // Another screen may scale differently, as Swift's backing changes.
    connect(window()->windowHandle(), &QWindow::screenChanged, this, &CanvasView::syncGeometry, Qt::UniqueConnection);
    // A mounted canvas takes keys; dialogs and pickers keep theirs.
    QMetaObject::invokeMethod(this, [this] {
        if (m_session.document() && !QApplication::activeModalWidget() && !m_session.showsNewDocument() && !m_session.showsImporter()
            && !m_session.colorPicker())
            setFocus(Qt::OtherFocusReason);
    }, Qt::QueuedConnection);
}

void CanvasView::hideEvent(QHideEvent *event)
{
    qApp->removeEventFilter(this);
    QWidget::hideEvent(event);
}

void CanvasView::syncGeometry()
{
    // After the layout pass, as Swift waits out SwiftUI's.
    QMetaObject::invokeMethod(this, [this] {
        const double scale = devicePixelRatio();
        const QSizeF bounds(size());
        if (m_session.viewport.viewSize == bounds && m_session.viewport.backingScale == scale)
            return;
        m_session.viewport.resize(bounds, scale, m_session.document() ? std::optional(m_session.document()->size()) : std::nullopt);
        update();
        m_session.notify();
    }, Qt::QueuedConnection);
}

void CanvasView::consumeFocusRequest(int request)
{
    if (request == m_lastFocusRequest)
        return;
    m_lastFocusRequest = request;
    // An open draft leaves the keys where they are.
    QMetaObject::invokeMethod(this, [this] {
        if (!QApplication::activeModalWidget() && !m_session.textDraft())
            setFocus(Qt::OtherFocusReason);
    }, Qt::QueuedConnection);
}

void CanvasView::wheelEvent(QWheelEvent *event)
{
    if (!m_session.document() || m_transformDrag || m_cropDrag || m_session.brushStroke() || m_session.warpStroke())
        return;
    // Trackpads report pixels; a wheel reports notches, lines each.
    const bool precise = !event->pixelDelta().isNull();
    const QPointF delta = precise ? QPointF(event->pixelDelta()) : QPointF(event->angleDelta()) / 120.0 * QApplication::wheelScrollLines();
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) {
        m_session.zoom(m_session.viewport.zoom() * std::exp(-delta.y() * 0.015), event->position());
        return;
    }
    const double multiplier = precise ? 1 : 12;
    m_session.viewport.translate(QSizeF(delta.x() * multiplier, delta.y() * multiplier));
    m_session.notify();
}

void CanvasView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton && isBrushTool(m_session.tool()) && !m_session.brushStroke() && !m_session.warpStroke() && !m_spaceHeld) {
        beginBrushTipDrag(event->position(), event->modifiers());
        return;
    }
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    // Swift's mouseDown lets go of a chosen effect first.
    m_session.dropEffectSelection();
    readModifiers(event->modifiers());
    setFocus(Qt::MouseFocusReason);
    // A press starts afresh: Qt can lose a release.
    endSampling();
    if (!m_session.document() || m_session.isProjectBusy() || m_session.isImporting())
        return;
    const QPointF point = event->position();
    m_hover = point;
    // A Levels eyedropper sets its point; the panel keeps keys.
    if (m_session.levels() && m_session.levels()->sampleMode && !m_spaceHeld) {
        m_session.sampleLevels(m_session.viewport.documentPoint(point, m_session.document()->size()));
        FloatingPanel::refocus(QStringLiteral("levelsPanel"));
        return;
    }
    // Open Levels leaves the canvas to panning and zooming.
    if (m_session.levels() && !m_spaceHeld && m_session.tool() != NavigationTool::hand && m_session.tool() != NavigationTool::zoom)
        return;
    if (!m_spaceHeld && pickingPress(point))
        return;
    if (m_spaceHeld || m_session.tool() == NavigationTool::hand) {
        m_lastDragPoint = point;
        updateCursor();
    } else if (m_session.tool() == NavigationTool::move) {
        beginTransformDrag(point, event->modifiers());
    } else if (isSelectionTool(m_session.tool())) {
        lassoMouseDown(point, event->modifiers());
        updateCursor();
    } else if (m_session.tool() == NavigationTool::zoom) {
        m_zoomDrag = ZoomDrag{point, m_session.viewport.zoom(), false};
    } else if (isBrushTool(m_session.tool())) {
        brushMouseDown(point, event->modifiers());
    } else if (m_session.tool() == NavigationTool::gradient) {
        beginGradientDrag(point);
    } else if (m_session.tool() == NavigationTool::shape) {
        m_session.beginShape(m_session.viewport.documentPoint(point, m_session.document()->size()));
    } else if (m_session.tool() == NavigationTool::crop) {
        beginCropDrag(point);
    } else if (m_session.tool() == NavigationTool::type) {
        const bool triple = m_textDoubleClick.isValid() && m_textDoubleClick.elapsed() < QGuiApplication::styleHints()->mouseDoubleClickInterval()
            && (point - m_textDoubleClickPoint).manhattanLength() < QGuiApplication::styleHints()->startDragDistance();
        m_textDoubleClick.invalidate();
        pressTextTool(point, event->modifiers(), triple ? 3 : 1);
    }
}

// A double click's second press closes a polygonal draft.
void CanvasView::mouseDoubleClickEvent(QMouseEvent *event)
{
    // The Type tool's second click selects a word.
    if (event->button() == Qt::LeftButton && m_session.tool() == NavigationTool::type && !m_spaceHeld && m_session.document()
        && !m_session.isProjectBusy() && !m_session.isImporting() && !picking() && !m_session.hueTargeting()) {
        setFocus(Qt::MouseFocusReason);
        pressTextTool(event->position(), event->modifiers(), 2);
        m_textDoubleClick.start();
        m_textDoubleClickPoint = event->position();
        return;
    }
    const std::optional<LassoDraft> &draft = m_session.lassoDraft();
    if (event->button() == Qt::LeftButton && isSelectionTool(m_session.tool()) && draft && draft->kind == LassoKind::polygonal
        && !m_spaceHeld && !m_session.isProjectBusy() && !m_session.isImporting() && !picking() && !m_session.hueTargeting()) {
        m_session.finishLasso();
        synchronizeDisplay();
        updateCursor();
        return;
    }
    // Else, picking included, it is a press, as Swift's mouseDown.
    QWidget::mouseDoubleClickEvent(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF point = event->position();
    m_hover = point;
    readModifiers(event->modifiers());
    // Without the left button a text gesture's release was lost.
    const bool held = event->buttons().testFlag(Qt::LeftButton);
    if (m_textBoxAnchor) {
        if (held)
            dragTextGesture(point);
        else
            endTextGesture();
        return;
    }
    if (m_inlineTextEditor && held && m_inlineTextEditor->drag(point))
        return;
    if (m_inlineTextEditor && !held)
        m_inlineTextEditor->release();
    if (pickingMove(point, event->buttons()) || targetingMove(point, event->buttons(), event->modifiers())
        || cropMove(point, event->buttons(), event->modifiers()))
        return;
    if (m_transformDrag) {
        dragTransform(point, event->modifiers());
        return;
    }
    if (m_brushTipDrag) {
        dragBrushTip(point, event->modifiers());
        return;
    }
    if (!m_lastDragPoint && dragGradient(point, event->modifiers()))
        return;
    if (!m_lastDragPoint && brushMouseMove(point, event->modifiers(), event->buttons().testFlag(Qt::LeftButton)))
        return;
    // A pan with Space comes first, as Swift's lastDragPoint.
    if (!m_lastDragPoint && moveSelectionTool(point, event->modifiers()))
        return;
    if (m_session.shapeDraft() && m_session.document()) {
        // Alt has no other job here: it grows from centre.
        m_session.dragShape(m_session.viewport.documentPoint(point, m_session.document()->size()),
                            event->modifiers().testFlag(Qt::ShiftModifier), event->modifiers().testFlag(Qt::AltModifier));
        synchronizeDisplay();
        return;
    }
    if (m_zoomDrag) {
        const double dx = point.x() - m_zoomDrag->start.x();
        if (std::abs(dx) >= 3)
            m_zoomDrag->moved = true;
        // Right zooms in, left out: doubling every 100 points.
        if (m_zoomDrag->moved)
            m_session.zoom(m_zoomDrag->zoom * std::pow(2, dx / 100), m_zoomDrag->start);
        return;
    }
    if (!m_lastDragPoint) {
        // Hover: the Move tool's cursor follows the handles.
        updateCursor();
        return;
    }
    m_session.viewport.translate(QSizeF(point.x() - m_lastDragPoint->x(), point.y() - m_lastDragPoint->y()));
    m_lastDragPoint = point;
    m_session.notify();
}

void CanvasView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton && m_brushTipDrag) {
        endBrushTipDrag(event->position());
        return;
    }
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    if (m_textBoxAnchor) {
        finishTextGesture();
        return;
    }
    // Open text's press ends; the cursor follows below.
    if (m_inlineTextEditor)
        m_inlineTextEditor->release();
    if (m_zoomDrag) {
        const ZoomDrag drag = *m_zoomDrag;
        m_zoomDrag.reset();
        // A press that never moved zooms a step on release.
        if (!drag.moved)
            m_session.zoom(m_session.viewport.zoom() * (event->modifiers().testFlag(Qt::AltModifier) ? 0.5 : 2), drag.start);
        return;
    }
    m_session.snapGuides = {};
    pickingRelease();
    targetingRelease();
    brushMouseUp(event->position());
    if (m_gradientDrag) {
        m_gradientDrag.reset();
        m_session.endGradientDrag();
    }
    if (m_session.shapeDraft())
        m_session.finishShape();
    endCropDrag();
    if (m_transformDrag)
        endTransformDrag();
    releaseSelectionTool();
    m_lastDragPoint.reset();
    updateCursor();
    // A Ctrl+T edit commits nothing here: the guides still go.
    synchronizeDisplay();
}

void CanvasView::keyPressEvent(QKeyEvent *event)
{
    // A drag swallows Alt's release: every key re-reads it.
    m_optionHeld = event->modifiers().testFlag(Qt::AltModifier) || event->key() == Qt::Key_Alt;
    m_controlHeld = event->modifiers().testFlag(Qt::ControlModifier) || event->key() == Qt::Key_Control;
    m_shiftHeld = event->modifiers().testFlag(Qt::ShiftModifier) || event->key() == Qt::Key_Shift;
    // Escape drops a text box being drawn, as Swift's first.
    if (event->key() == Qt::Key_Escape && m_textBoxAnchor) {
        endTextGesture();
        return;
    }
    // Open text is the text view: it takes every key.
    if (m_inlineTextEditor) {
        if (!m_inlineTextEditor->keyPress(*event))
            QWidget::keyPressEvent(event);
        return;
    }
    const bool plain = !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    const std::optional<NavigationTool> tool = plain ? toolForKey(event->key()) : std::nullopt;
    const bool erase = event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete;
    if (erase && event->modifiers() == Qt::ShiftModifier) {
        // Shift-Delete: Content-Aware Fill, taken either way.
        if (m_session.canContentAwareFill())
            m_session.beginFilter(FilterKind::contentAwareFill);
    } else if (m_session.levels() && event->key() != Qt::Key_Space) {
        // Open Levels takes its keys and passes the rest on.
        if (event->key() == Qt::Key_Escape)
            m_session.cancelLevels();
        else if (enter)
            m_session.commitLevels();
        else if (event->key() == Qt::Key_P && event->modifiers().testFlag(Qt::AltModifier))
            m_session.updateLevels(m_session.levels()->settings, !m_session.levels()->preview);
        else
            QWidget::keyPressEvent(event);
    } else if (m_session.brushStroke() || m_session.warpStroke()) {
        // A stroke takes every key; Escape cancels it.
        if (event->key() == Qt::Key_Escape && !m_session.isProjectBusy()) {
            m_session.cancelBrush();
            synchronizeDisplay();
        }
    } else if (selectionKey(*event)) {
        return;
    } else if (event->key() == Qt::Key_Escape && m_session.shapeDraft()) {
        m_session.cancelShape();
        synchronizeDisplay();
    } else if ((event->key() == Qt::Key_Escape || enter) && m_session.gradientEdit()) {
        m_gradientDrag.reset();
        if (enter)
            m_session.commitGradient();
        else
            m_session.cancelGradient();
    } else if (cropKey(*event)) {
        return;
    } else if ((event->key() == Qt::Key_Escape || enter) && m_session.transformEdit()) {
        // The keys end a drag with its edit, cursor included.
        m_transformDrag.reset();
        m_dragCursor.reset();
        if (enter)
            m_session.commitTransform();
        else
            m_session.cancelTransform();
        updateCursor();
    } else if (isArrow(event->key()) && event->modifiers().testFlag(Qt::ControlModifier) && !(event->modifiers() & (Qt::AltModifier | Qt::MetaModifier))
               && !m_session.lassoDraft() && m_session.selection() && !m_session.selection()->isEmpty()) {
        // Ctrl-arrow moves the selected pixels in any tool.
        const double step = event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1;
        m_session.nudgePixels(event->key() == Qt::Key_Left ? -step : event->key() == Qt::Key_Right ? step : 0,
                              event->key() == Qt::Key_Up ? -step : event->key() == Qt::Key_Down ? step : 0, [canvas = QPointer<CanvasView>(this)] {
                                  if (canvas)
                                      canvas->synchronizeDisplay();
                              });
    } else if (m_session.tool() == NavigationTool::move && plain && isArrow(event->key())) {
        const double step = event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1;
        m_session.nudgeLayer(event->key() == Qt::Key_Left ? -step : event->key() == Qt::Key_Right ? step : 0,
                             event->key() == Qt::Key_Up ? -step : event->key() == Qt::Key_Down ? step : 0);
    } else if (erase && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        m_session.deleteKeyPressed();
    } else if (event->key() == Qt::Key_Space) {
        m_spaceHeld = true;
        updateCursor();
        updateBrushCursor();
    } else if (brushKey(*event)) {
        return;
    } else if (plain && (event->key() == Qt::Key_X || event->key() == Qt::Key_D)) {
        // X swaps the palette; D brings back black and white.
        if (event->key() == Qt::Key_X)
            m_session.swapPaletteColors();
        else
            m_session.resetPaletteColors();
    } else if (tool == NavigationTool::shape && event->modifiers().testFlag(Qt::ShiftModifier) && m_session.tool() == NavigationTool::shape) {
        m_session.toggleShapeKind();
    } else if (tool) {
        m_session.selectTool(*tool);
    } else {
        QWidget::keyPressEvent(event);
    }
}

void CanvasView::keyReleaseEvent(QKeyEvent *event)
{
    // Auto-repeat sends release and press pairs: not a release.
    if (event->isAutoRepeat()) {
        QWidget::keyReleaseEvent(event);
    } else if (event->key() == Qt::Key_Space) {
        m_spaceHeld = false;
        updateCursor();
        updateBrushCursor();
    } else {
        QWidget::keyReleaseEvent(event);
    }
}

void CanvasView::focusInEvent(QFocusEvent *event)
{
    if (m_inlineTextEditor)
        m_inlineTextEditor->setFocused(true);
    QWidget::focusInEvent(event);
}

void CanvasView::focusOutEvent(QFocusEvent *event)
{
    if (m_inlineTextEditor) {
        m_inlineTextEditor->setFocused(false);
        m_inlineTextEditor->release();
    }
    // Qt can lose the release: box and sampling go.
    if (m_textBoxAnchor)
        endTextGesture();
    endSampling();
    // Swift's resignFirstResponder: a stroke cancels, the circle goes.
    if (!m_session.isProjectBusy())
        m_session.cancelBrush();
    m_brushPointer = std::nullopt;
    updateBrushCursor();
    cancelTransformDrag();
    endCropDrag();
    m_gradientDrag.reset();
    m_session.cancelShape();
    endSelectionGestures();
    m_spaceHeld = false;
    m_lastDragPoint.reset();
    updateCursor();
    QWidget::focusOutEvent(event);
}

// Swift's mouseExited: the circle leaves with the pointer.
void CanvasView::leaveEvent(QEvent *event)
{
    m_brushPointer = std::nullopt;
    updateBrushCursor();
    QWidget::leaveEvent(event);
}
