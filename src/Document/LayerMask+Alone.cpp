#include "Document/EditorSession.h"

// Swift's mask shown alone, an Alt-click on its thumbnail.
std::optional<ImageLayer> EditorSession::maskAloneLayer() const
{
    const std::optional<ImageLayer> layer = activeLayer();
    if (!m_viewsMaskAlone || !m_isMaskSelected || !layer || !layer->mask)
        return std::nullopt;
    return layer;
}

void EditorSession::toggleMaskAlone(QUuid id)
{
    const std::optional<ImageLayer> shown = maskAloneLayer();
    const bool showing = shown && shown->id == id;
    selectLayerTarget(id, true);
    if (m_activeLayerID != id || !m_isMaskSelected)
        return;
    m_viewsMaskAlone = !showing;
    notify();
}
