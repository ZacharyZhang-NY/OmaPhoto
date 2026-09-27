#include "Rendering/EditorCanvas.h"
#include "UI/KeyboardShortcuts.h"
#include <QKeyEvent>

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

void CanvasView::keyPressEvent(QKeyEvent *event)
{
    // Remapped keys arrive as the keys they stand for.
    ShortcutSettings &shortcuts = ShortcutSettings::shared();
    const std::unique_ptr<QKeyEvent> typed = m_inlineTextEditor ? shortcuts.textEvent(*event) : shortcuts.canvasEvent(*event);
    if (!typed)
        return;
    pressKey(typed.get(), event->key());
    event->setAccepted(typed->isAccepted());
}

void CanvasView::pressKey(QKeyEvent *event, int physical)
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
    } else if (event->key() == Qt::Key_Escape && m_session.guideDrag()) {
        m_session.cancelGuideDrag();
        m_guideDragging = false;
        m_dragCursor.reset();
        updateCursor();
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
        // Swift's panPhysicalKey: the key held, whatever it stands for.
        m_panKey = physical;
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
    } else if (event->key() == m_panKey.value_or(Qt::Key_Space)) {
        m_panKey.reset();
        m_spaceHeld = false;
        updateCursor();
        updateBrushCursor();
    } else {
        QWidget::keyReleaseEvent(event);
    }
}
