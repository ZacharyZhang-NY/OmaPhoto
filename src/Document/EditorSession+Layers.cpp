#include "Document/EditorSession.h"
#include <algorithm>
#include <map>

void EditorSession::addBlankLayer()
{
    if (!canEditLayers())
        return;
    const std::vector<ImageLayer> &existing = m_document->layers;
    int number = 1;
    const auto taken = [&](const QString &name) {
        return std::any_of(existing.begin(), existing.end(), [&](const ImageLayer &layer) { return layer.name == name; });
    };
    while (taken(QStringLiteral("Layer %1").arg(number)))
        ++number;
    ImageLayer layer(QStringLiteral("Layer %1").arg(number), m_document->size());
    const std::optional<ImageLayer> active = activeLayer();
    const bool intoFolder = active && active->isGroup;
    layer.parentID = intoFolder ? m_activeLayerID : active ? active->parentID : std::nullopt;
    if (layer.parentID)
        m_collapsedGroupIDs.remove(*layer.parentID);
    const int activeIndex = indexOf(existing, m_activeLayerID);
    int insertion = activeIndex >= 0 ? activeIndex + 1 : int(existing.size());
    // In a folder the layer goes above its topmost contents.
    if (intoFolder) {
        std::map<QUuid, std::optional<QUuid>> parents;
        for (const ImageLayer &each : existing)
            parents[each.id] = each.parentID;
        const auto isInside = [&](QUuid id) {
            std::optional<QUuid> parent = parents[id];
            for (int steps = 0; parent && steps < 64; ++steps) {
                if (*parent == *m_activeLayerID)
                    return true;
                parent = parents.count(*parent) ? parents[*parent] : std::nullopt;
            }
            return false;
        };
        for (int index = int(existing.size()) - 1; index >= 0; --index) {
            if (isInside(existing[index].id)) {
                insertion = std::max(insertion, index + 1);
                break;
            }
        }
    }
    beginEdit(QStringLiteral("New Blank Layer"));
    m_document->layers.insert(m_document->layers.begin() + insertion, layer);
    setActiveLayerID(layer.id);
    endEdit();
}

void EditorSession::deleteLayer(QUuid id)
{
    if (!canEditLayers() || indexOf(m_document->layers, id) < 0)
        return;
    if (!deleteWithLiveMaskChoice({id}))
        finishDeletingLayer(id, {});
}

void EditorSession::deleteActiveLayer()
{
    if (m_activeLayerID)
        deleteLayer(*m_activeLayerID);
}

void EditorSession::deleteSelectedLayers()
{
    if (!canEditLayers())
        return;
    // Captured first: deleting moves the active layer.
    std::vector<QUuid> ids;
    for (const ImageLayer &layer : m_document->layers) {
        if (m_selectedLayerIDs.contains(layer.id))
            ids.push_back(layer.id);
    }
    if (ids.size() <= 1)
        deleteActiveLayer();
    else if (!deleteWithLiveMaskChoice(ids))
        finishDeletingLayers(ids, {});
}

void EditorSession::renameLayer(QUuid id, const QString &name)
{
    const QString trimmed = name.trimmed();
    const int index = m_document ? indexOf(m_document->layers, id) : -1;
    if (m_isProjectBusy || m_isImporting || trimmed.isEmpty() || index < 0)
        return;
    beginEdit(QStringLiteral("Rename Layer"));
    m_document->layers[index].name = trimmed;
    endEdit();
}

void EditorSession::toggleLayerVisibility(QUuid id)
{
    const int index = canEditLayers() ? indexOf(m_document->layers, id) : -1;
    if (index < 0)
        return;
    beginEdit(m_document->layers[index].isVisible ? QStringLiteral("Hide Layer") : QStringLiteral("Show Layer"));
    m_document->layers[index].isVisible = !m_document->layers[index].isVisible;
    endEdit();
}

std::optional<bool> EditorSession::beginVisibilitySwipe(QUuid id)
{
    const int index = canEditLayers() ? indexOf(m_document->layers, id) : -1;
    if (index < 0)
        return std::nullopt;
    const bool visible = !m_document->layers[index].isVisible;
    beginEdit(visible ? QStringLiteral("Show Layer") : QStringLiteral("Hide Layer"));
    setVisibilityInSwipe(id, visible);
    return visible;
}

void EditorSession::setVisibilityInSwipe(QUuid id, bool visible)
{
    const int index = m_document ? indexOf(m_document->layers, id) : -1;
    if (index < 0 || m_document->layers[index].isVisible == visible)
        return;
    m_document->layers[index].isVisible = visible;
    notify();
}

void EditorSession::endVisibilitySwipe()
{
    endEdit();
}

void EditorSession::reorderLayers(const std::set<int> &offsets, int destination)
{
    if (!canEditLayers())
        return;
    std::vector<ImageLayer> listed(m_document->layers.rbegin(), m_document->layers.rend());
    const int count = int(listed.size());
    const bool inRange = std::all_of(offsets.begin(), offsets.end(), [&](int offset) { return offset >= 0 && offset < count; });
    if (!inRange || destination < 0 || destination > count)
        return;
    // SwiftUI's move: the picked rows land before `destination`.
    std::vector<ImageLayer> moved, kept;
    int before = 0;
    for (int index = 0; index < count; ++index) {
        const bool picked = offsets.count(index) > 0;
        (picked ? moved : kept).push_back(listed[index]);
        before += picked && index < destination;
    }
    kept.insert(kept.begin() + (destination - before), moved.begin(), moved.end());
    beginEdit(QStringLiteral("Reorder Layers"));
    m_document->layers.assign(kept.rbegin(), kept.rend());
    endEdit();
}

bool EditorSession::canMoveActiveLayer(int offset) const
{
    const std::optional<ImageLayer> active = canEditLayers() ? activeLayer() : std::nullopt;
    if (!active)
        return false;
    int position = -1, siblings = 0;
    for (const ImageLayer &layer : m_document->layers) {
        if (layer.parentID != active->parentID)
            continue;
        if (layer.id == active->id)
            position = siblings;
        ++siblings;
    }
    return position >= 0 && position + offset >= 0 && position + offset < siblings;
}

void EditorSession::moveActiveLayer(int offset)
{
    if (!canMoveActiveLayer(offset))
        return;
    const ImageLayer active = *activeLayer();
    std::vector<int> siblings;
    for (int index = 0; index < int(m_document->layers.size()); ++index) {
        if (m_document->layers[index].parentID == active.parentID)
            siblings.push_back(index);
    }
    const int from = indexOf(m_document->layers, active.id);
    const auto position = std::find(siblings.begin(), siblings.end(), from) - siblings.begin();
    beginEdit(QStringLiteral("Reorder Layers"));
    std::swap(m_document->layers[from], m_document->layers[siblings[position + offset]]);
    endEdit();
}
