#include "Rendering/EditorCanvas.h"
#include "UI/ColorPickerSheet.h"
#include "UI/FloatingPanel.h"

// The Eyedropper, or Alt with Brush, Spot Healing, Gradient.
bool CanvasView::palettePicking() const
{
    const NavigationTool tool = m_session.tool();
    return tool == NavigationTool::eyedropper
        || (m_optionHeld && (tool == NavigationTool::brush || tool == NavigationTool::spotHealing || tool == NavigationTool::gradient)
            && !m_session.brushStroke() && !m_gradientDrag);
}

bool CanvasView::picking() const
{
    const std::optional<FilterEdit> &edit = m_session.filterEdit();
    return palettePicking() || (m_session.colorPicker() && !m_session.pickingForDialog()) || m_session.hueSampleMode()
        || (m_session.levels() && m_session.levels()->sampleMode) || m_session.colorRange()
        || (edit
            && (edit->rawPanel.samplesWhiteBalance || edit->rawPanel.samplesPointColor || edit->rawPanel.samplesDefringe
                || edit->rawPanel.drawingGeometryGuide));
}

// Picking or targeting begins or ends: a sampling goes.
void CanvasView::syncPicking()
{
    // The sheet's eyedroppers change the badge, not the sampling.
    const std::optional<HueSampleMode> range = m_session.colorRange() ? std::optional(m_session.colorRange().value().effectiveMode()) : std::nullopt;
    if (range != m_displayedRangeMode) {
        m_displayedRangeMode = range;
        updateCursor();
    }
    if (m_displayedPicking == picking() && m_displayedTargeting == m_session.hueTargeting())
        return;
    m_displayedPicking = picking();
    m_displayedTargeting = m_session.hueTargeting();
    endSampling();
    updateCursor();
}

bool CanvasView::pickingPress(QPointF point)
{
    const QPointF pixel = m_session.viewport.documentPoint(point, m_session.document()->size());
    if (picking()) {
        // A colour, unless a range's eyedropper is armed.
        if (m_session.colorPicker() || (palettePicking() && !m_session.hueSampleMode())) {
            beginSampling(point);
        } else {
            m_session.sampleHueRange(pixel);
            FloatingPanel::refocus(QStringLiteral("adjustmentPanel"));
        }
        return true;
    }
    if (!m_session.hueTargeting())
        return false;
    if (m_session.beginHueTargeting(pixel))
        m_hueTargetStart = point;
    return true;
}

bool CanvasView::targetingMove(QPointF point, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers)
{
    if (!m_hueTargetStart)
        return false;
    // Without the left button its release was lost.
    if (!buttons.testFlag(Qt::LeftButton)) {
        targetingRelease();
        return true;
    }
    m_session.dragHueTargeting(point.x() - m_hueTargetStart->x(), modifiers.testFlag(Qt::ControlModifier));
    // Every drag restores the cursor Alt may have changed.
    setCursor(Qt::SizeHorCursor);
    return true;
}

void CanvasView::targetingRelease()
{
    if (!m_hueTargetStart)
        return;
    m_hueTargetStart.reset();
    m_session.endHueTargeting();
}

void CanvasView::beginSampling(QPointF point)
{
    // Alt may come with the press: take its change first.
    syncPicking();
    m_samplingOriginal = m_session.colorPicker() ? m_session.colorPicker()->color() : m_session.foregroundColor();
    m_samplingColor = true;
    sampleColor(point);
}

// Swift's sampleColor: into the open picker, else the foreground.
void CanvasView::sampleColor(QPointF point)
{
    const std::optional<CanvasDocument> &document = m_session.document();
    if (!document)
        return;
    const QPointF pixel = m_session.viewport.documentPoint(point, document->size());
    if (m_session.colorPicker()) {
        m_session.sampleIntoColorPicker(pixel);
    } else if (m_session.canEditPalette()) {
        if (const std::optional<PaletteColor> color = m_session.sampleCompositeColor(pixel))
            m_session.setForegroundColor(*color);
    }
    const PaletteColor sampled = m_session.colorPicker() ? m_session.colorPicker()->color() : m_session.foregroundColor();
    update(m_sampleRing.update(m_session.showsSampleRing() ? std::optional(point) : std::nullopt, m_samplingOriginal, sampled));
}

void CanvasView::endSampling()
{
    m_samplingColor = false;
    update(m_sampleRing.update(std::nullopt, m_samplingOriginal, m_sampleRing.sampled()));
}

// A drag samples on; a buttonless hover shows the eyedropper.
bool CanvasView::pickingMove(QPointF point, Qt::MouseButtons buttons)
{
    syncPicking();
    if (m_samplingColor) {
        // Without the left button its release was lost.
        if (buttons.testFlag(Qt::LeftButton))
            sampleColor(point);
        else
            endSampling();
        return true;
    }
    if (buttons != Qt::NoButton || !picking())
        return false;
    updateCursor();
    return true;
}

// The release ends sampling; the picker takes the keys back.
void CanvasView::pickingRelease()
{
    if (!m_samplingColor)
        return;
    endSampling();
    if (m_session.colorPicker())
        ColorPickerPanelController::refocus();
}
