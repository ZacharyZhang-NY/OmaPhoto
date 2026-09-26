#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "Document/Filters.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include "Logging.h"

// What Ctrl+E merges, in stacking order, and its place.
std::optional<EditorSession::MergePlan> EditorSession::mergePlan() const
{
    const std::optional<ImageLayer> active = activeLayer();
    if (!canEditLayers() || !active)
        return std::nullopt;
    const std::vector<ImageLayer> &layers = m_document->layers;
    if (m_selectedLayerIDs.size() > 1) {
        // Several selected layers, with anything their folders hold.
        QSet<QUuid> picked = m_selectedLayerIDs;
        for (const QUuid &id : m_selectedLayerIDs)
            picked.unite(descendantIDs(id));
        std::vector<QUuid> ordered;
        std::optional<ImageLayer> top;
        bool pixels = false;
        for (const ImageLayer &layer : layers) {
            if (!picked.contains(layer.id))
                continue;
            ordered.push_back(layer.id);
            pixels = pixels || !layer.isGroup;
            if (m_selectedLayerIDs.contains(layer.id))
                top = layer;
        }
        if (!pixels || !top)
            return std::nullopt;
        return MergePlan{ordered, picked, top->name, top->parentID, top->id, QStringLiteral("Merge Layers")};
    }
    if (active->isGroup) {
        // A folder merges its contents, and the folder goes.
        const QSet<QUuid> inside = descendantIDs(active->id);
        std::vector<QUuid> ids;
        bool pixels = false;
        for (const ImageLayer &layer : layers) {
            if (inside.contains(layer.id) || layer.id == active->id)
                ids.push_back(layer.id);
            pixels = pixels || (inside.contains(layer.id) && !layer.isGroup);
        }
        if (!pixels)
            return std::nullopt;
        return MergePlan{ids, QSet<QUuid>(ids.begin(), ids.end()), active->name, active->parentID, active->id, QStringLiteral("Merge Group")};
    }
    // One layer merges with the one beneath it.
    for (int index = indexOf(layers, active->id) - 1; index >= 0; --index) {
        const ImageLayer &below = layers[index];
        if (below.parentID != active->parentID)
            continue;
        if (below.isGroup)
            return std::nullopt;
        return MergePlan{{below.id, active->id}, {below.id, active->id}, below.name, active->parentID, active->id, QStringLiteral("Merge Down")};
    }
    return std::nullopt;
}

bool EditorSession::canMergeLayers() const
{
    return mergePlan().has_value();
}

QString EditorSession::mergeTitle() const
{
    const std::optional<MergePlan> plan = mergePlan();
    return plan ? plan->action : QStringLiteral("Merge Down");
}

// The layers as shown, baked into one pixel layer.
void EditorSession::mergeLayers()
{
    commitTransform();
    const std::optional<MergePlan> plan = mergePlan();
    if (!plan)
        return;
    const std::vector<ImageLayer> &layers = m_document->layers;
    const QSet<QUuid> kept(plan->ids.begin(), plan->ids.end());
    // Only the merged layers, cut loose from the rest.
    CanvasDocument flat(m_document->width, m_document->height);
    flat.id = m_document->id;
    flat.resolution = m_document->resolution;
    for (const ImageLayer &layer : layers) {
        if (!kept.contains(layer.id))
            continue;
        ImageLayer copy = layer;
        if (copy.parentID && !kept.contains(*copy.parentID))
            copy.parentID = std::nullopt;
        if (copy.maskSourceID && !kept.contains(*copy.maskSourceID))
            copy.maskSourceID = std::nullopt;
        flat.layers.push_back(copy);
    }
    std::optional<ImageLayer> merged;
    try {
        QImage full = BrushRaster::context(m_document->width, m_document->height, false);
        {
            QPainter context(&full);
            drawLiveComposite(flat, context);
        }
        const LayerTransform canvas{.origin = {0, 0}, .size = m_document->size()};
        const PixelFilter::Trimmed trimmed = PixelFilter::trimmed(full, canvas);
        // At 1:1 the trimmed size is the crop's: the constructor's.
        merged = ImageLayer(ImportedImage(trimmed.image, PixelAdjust::thumbnail(trimmed.image), plan->name), trimmed.transform.origin);
    } catch (const ExportError &error) {
        qCWarning(lcRendering) << "merge: the layers could not be rendered:" << error.what();
        return;
    }
    merged->parentID = plan->parent;
    std::vector<ImageLayer> next;
    int insertion = 0;
    for (const ImageLayer &layer : layers) {
        if (plan->removed.contains(layer.id))
            continue;
        next.push_back(layer);
        // Layers clipped to anything merged now clip to the result.
        if (next.back().maskSourceID && plan->removed.contains(*next.back().maskSourceID))
            next.back().maskSourceID = merged->id;
    }
    for (const ImageLayer &layer : layers) {
        if (layer.id == plan->anchor)
            break;
        insertion += plan->removed.contains(layer.id) ? 0 : 1;
    }
    next.insert(next.begin() + insertion, *merged);
    std::vector<ProjectLayerRecord> records;
    for (const ImageLayer &layer : next)
        records.push_back(layer.hierarchyRecord());
    try {
        LayerHierarchy::validate(records);
    } catch (const ProjectError &error) {
        qCWarning(lcRendering) << "merge: the result is no valid hierarchy:" << error.what();
        return;
    }
    beginEdit(plan->action);
    m_document->layers = std::move(next);
    setActiveLayerID(merged->id);
    endEdit();
}
