#include "Rendering/EditorCanvas.h"
#include "UI/FloatingPanel.h"

// Swift's Camera Raw branches of mouseDown, mouseDragged and mouseUp.
bool CanvasView::cameraRawPress(QPointF point)
{
    const std::optional<FilterEdit> &edit = m_session.filterEdit();
    if (!edit || m_spaceHeld)
        return false;
    const QPointF document = m_session.viewport.documentPoint(point, m_session.document()->size());
    const CameraRawPanel &panel = edit->rawPanel;
    // Each eyedropper samples and hands the panel the keys back.
    if (panel.samplesWhiteBalance || panel.samplesPointColor || panel.samplesDefringe) {
        if (panel.samplesWhiteBalance)
            m_session.sampleCameraRawWhiteBalance(document);
        else if (panel.samplesPointColor)
            m_session.sampleCameraRawPointColor(document);
        else
            m_session.sampleCameraRawDefringe(document);
        FloatingPanel::refocus(QStringLiteral("filterPanel"));
        return true;
    }
    if (panel.drawingGeometryGuide) {
        m_session.beginCameraRawGeometryGuide(document);
        return true;
    }
    if (panel.targetsCurve || panel.targetsMixer) {
        m_session.beginCameraRawDrag(document);
        return true;
    }
    return false;
}

// No left button: the release was lost, so it ends.
bool CanvasView::cameraRawMove(QPointF point, Qt::MouseButtons buttons)
{
    const std::optional<FilterEdit> &edit = m_session.filterEdit();
    if (!edit || !m_session.document() || !(edit->rawPanel.guideDraft || edit->rawPanel.drag))
        return false;
    if (!buttons.testFlag(Qt::LeftButton)) {
        cameraRawRelease();
        return false;
    }
    const QPointF document = m_session.viewport.documentPoint(point, m_session.document()->size());
    if (edit->rawPanel.drawingGeometryGuide && edit->rawPanel.guideDraft)
        m_session.continueCameraRawGeometryGuide(document);
    else if (edit->rawPanel.drag)
        m_session.dragCameraRaw(document);
    return true;
}

void CanvasView::cameraRawRelease()
{
    if (!m_session.filterEdit())
        return;
    if (m_session.filterEdit()->rawPanel.guideDraft)
        m_session.commitCameraRawGeometryGuide();
    if (m_session.filterEdit() && m_session.filterEdit()->rawPanel.drag) {
        CameraRawPanel panel = m_session.filterEdit()->rawPanel;
        panel.drag.reset();
        m_session.setCameraRawPanel(panel);
    }
}

// Swift's mouseExited: the readout goes with the pointer.
void CanvasView::clearCameraRawReadout()
{
    if (!m_session.filterEdit() || !m_session.filterEdit()->rawPanel.readout)
        return;
    CameraRawPanel panel = m_session.filterEdit()->rawPanel;
    panel.readout.reset();
    m_session.setCameraRawPanel(panel);
}
