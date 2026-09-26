#include "Document/EditorSession.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include "Rendering/LayerRenderer.h"
#include <cmath>

// Swift's CloneStamp extension: the source, its offset, the sample.
void EditorSession::setCloneSource(QPointF point)
{
    if (!std::isfinite(point.x()) || !std::isfinite(point.y()))
        return;
    m_cloneSource = point;
    m_cloneOffset = std::nullopt;
    notify();
}

void EditorSession::setCloneSettings(const CloneSettings &settings)
{
    m_cloneSettings = settings;
    notify();
}

// Shared by stroke and preview: what a click stamps.
std::optional<QSizeF> EditorSession::cloneStrokeOffset(QPointF point) const
{
    if (!m_cloneSource)
        return std::nullopt;
    if (m_cloneSettings.aligned && m_cloneOffset)
        return m_cloneOffset;
    return QSizeF(std::round(m_cloneSource->x() - point.x()), std::round(m_cloneSource->y() - point.y()));
}

// The source itself until a stroke fixes the offset.
std::optional<QPointF> EditorSession::cloneSamplePoint(QPointF point) const
{
    if (!m_cloneSource)
        return std::nullopt;
    if (!m_cloneOffset || (!m_cloneSettings.aligned && !m_brushStroke))
        return m_cloneSource;
    return point + QPointF(m_cloneOffset->width(), m_cloneOffset->height());
}

std::optional<QImage> EditorSession::cloneSample(const CanvasDocument &document) const
{
    try {
        QImage context = BrushRaster::context(document.width, document.height, false);
        QPainter painter(&context);
        // A painted asset flattens here; the catch covers it too.
        if (m_cloneSettings.sampleAllLayers) {
            drawLiveComposite(document, painter);
        } else if (const std::optional<ImageLayer> layer = activeLayer(); layer && layer->asset) {
            const LayerTransform transform = displayedTransform(*layer);
            LayerRenderer::draw(layer->asset->image(), transform, transform.center(), painter, {});
        }
        painter.end();
        return context;
    } catch (const ExportError &error) {
        qCWarning(lcApp) << "Clone Stamp cannot sample the canvas:" << error.what();
        return std::nullopt;
    }
}
