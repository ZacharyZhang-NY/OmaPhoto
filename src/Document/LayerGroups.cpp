#include "Document/LayerGroups.h"
#include "Document/EditorSession.h"
#include <QHash>
#include <algorithm>
#include <map>
#include <stdexcept>

namespace {
using Children = std::map<std::optional<QUuid>, std::vector<const ProjectLayerRecord *>>;

void visit(const Children &children, const std::optional<QUuid> &parent, int depth, bool visible, bool topFirst,
           const QSet<QUuid> &collapsed, std::vector<LayerHierarchy::Entry> &result)
{
    if (depth > 64)
        return;
    const auto found = children.find(parent);
    if (found == children.end())
        return;
    std::vector<const ProjectLayerRecord *> siblings = found->second;
    if (topFirst)
        std::reverse(siblings.begin(), siblings.end());
    for (const ProjectLayerRecord *layer : siblings) {
        const bool effective = visible && layer->isVisible;
        result.push_back({*layer, depth, effective});
        if (layer->isGroup == true && !collapsed.contains(layer->id))
            visit(children, layer->id, depth + 1, effective, topFirst, collapsed, result);
    }
}

// Swift's `try?`: a refused hierarchy changes nothing.
bool isValidHierarchy(const std::vector<ImageLayer> &layers)
{
    std::vector<ProjectLayerRecord> records;
    for (const ImageLayer &layer : layers)
        records.push_back(layer.hierarchyRecord());
    try {
        LayerHierarchy::validate(records);
    } catch (const ProjectError &) {
        return false;
    }
    return true;
}
}

std::vector<LayerHierarchy::Entry> LayerHierarchy::entries(const std::vector<ProjectLayerRecord> &layers,
                                                           bool topFirst, const QSet<QUuid> &collapsed)
{
    Children children;
    for (const ProjectLayerRecord &layer : layers)
        children[layer.parentID].push_back(&layer);
    std::vector<Entry> result;
    visit(children, std::nullopt, 0, true, topFirst, collapsed, result);
    return result;
}

std::vector<ProjectLayerRecord> LayerHierarchy::visibleLayers(const std::vector<ProjectLayerRecord> &layers)
{
    std::vector<ProjectLayerRecord> result;
    for (const Entry &entry : entries(layers)) {
        if (entry.visible && entry.layer.isGroup != true)
            result.push_back(entry.layer);
    }
    return result;
}

void LayerHierarchy::validate(const std::vector<ProjectLayerRecord> &layers)
{
    QHash<QUuid, const ProjectLayerRecord *> byID;
    for (const ProjectLayerRecord &layer : layers) {
        if (byID.contains(layer.id) || (layer.isGroup == true && layer.imageFile))
            throw ProjectError(ProjectError::Kind::invalid);
        byID.insert(layer.id, &layer);
    }
    for (const ProjectLayerRecord &layer : layers) {
        QSet<QUuid> seen{layer.id};
        std::optional<QUuid> parent = layer.parentID;
        while (parent) {
            const ProjectLayerRecord *node = byID.value(*parent);
            if (seen.size() > 64 || seen.contains(*parent) || !node || node->isGroup != true)
                throw ProjectError(ProjectError::Kind::invalid);
            seen.insert(*parent);
            parent = node->parentID;
        }
        if (layer.isGroup == true && seen.size() > 64)
            throw ProjectError(ProjectError::Kind::invalid);
    }
}

double LayerOpacity::effective(double own, std::optional<QUuid> parent, const Node &folder)
{
    double opacity = own;
    std::optional<QUuid> id = parent;
    // Past 64 folders the tree is wrong: Swift stops there.
    for (int depth = 0; id && depth < 64; ++depth) {
        const std::optional<std::pair<double, std::optional<QUuid>>> node = folder(*id);
        if (!node)
            break;
        opacity *= node->first;
        id = node->second;
    }
    return opacity;
}

double ImageLayer::effectiveOpacity(const std::map<QUuid, ImageLayer> &byID) const
{
    return LayerOpacity::effective(opacity, parentID, [&byID](QUuid id) -> std::optional<std::pair<double, std::optional<QUuid>>> {
        if (!byID.contains(id))
            return std::nullopt;
        const ImageLayer &node = byID.at(id);
        return std::pair(node.opacity, node.parentID);
    });
}

double ProjectLayerRecord::effectiveOpacity(const std::map<QUuid, ProjectLayerRecord> &byID) const
{
    return LayerOpacity::effective(opacity.value_or(1), parentID, [&byID](QUuid id) -> std::optional<std::pair<double, std::optional<QUuid>>> {
        if (!byID.contains(id))
            return std::nullopt;
        const ProjectLayerRecord &node = byID.at(id);
        return std::pair(node.opacity.value_or(1), node.parentID);
    });
}

ProjectLayerRecord ImageLayer::hierarchyRecord() const
{
    const QString file = id.toString(QUuid::WithoutBraces).toUpper();
    const std::optional<LayerShape> live = liveShape();
    const std::optional<LayerText> typed = liveText();
    return {.id = id,
            .name = name,
            .isVisible = isVisible,
            .transform = transform,
            .imageFile = asset ? std::optional(file + ".png") : std::nullopt,
            .parentID = parentID,
            .isGroup = isGroup,
            .opacity = opacity,
            .blendMode = blendMode,
            .maskFile = mask ? std::optional(file + ".mask.png") : std::nullopt,
            .maskEnabled = mask ? std::optional(mask->isEnabled) : std::nullopt,
            .maskSourceID = maskSourceID,
            .adjustment = adjustment,
            .maskPlacement = mask ? mask->placement : std::nullopt,
            .maskLinked = mask ? std::optional(mask->isLinked) : std::nullopt,
            .shape = live ? std::optional(live->style) : std::nullopt,
            .effects = effects,
            .text = typed ? std::optional(typed->style) : std::nullopt};
}

void EditorSession::selectLayers(const QSet<QUuid> &ids, std::optional<QUuid> primary)
{
    dropEffectSelection();
    if (ids != m_selectedLayerIDs && !finishText())
        return;
    if (m_brushStroke)
        return;
    QSet<QUuid> valid;
    if (m_document) {
        for (const ImageLayer &layer : m_document->layers) {
            if (ids.contains(layer.id))
                valid.insert(layer.id);
        }
    }
    if (valid != m_selectedLayerIDs) {
        commitTransform();
        resolveGradient();
    }
    // The primary layer is active; any other of them otherwise.
    const std::optional<QUuid> active = primary && valid.contains(*primary) ? primary
        : valid.isEmpty()                                                   ? std::nullopt
                                                                            : std::optional(*valid.begin());
    setActiveLayerID(active);
    m_selectedLayerIDs = valid;
    notify();
}

void EditorSession::extendSelection(QUuid id)
{
    const auto known = [&](const ImageLayer &layer) { return layer.id == id; };
    // Cmd-Shift-click works while a transform is open too.
    if ((!canEditLayers() && !m_transformEdit) || std::none_of(m_document->layers.begin(), m_document->layers.end(), known))
        return;
    QSet<QUuid> ids = m_selectedLayerIDs;
    if (ids.contains(id) && ids.size() > 1) {
        // Taking the active layer out hands its role on.
        ids.remove(id);
        selectLayers(ids, m_activeLayerID == id ? std::optional(*ids.begin()) : m_activeLayerID);
    } else {
        ids.insert(id);
        selectLayers(ids, id);
    }
}

void EditorSession::groupSelectedLayers()
{
    if (!canEditLayers() || m_document->layers.size() >= 10'000)
        return;
    const std::vector<ImageLayer> &existing = m_document->layers;
    std::map<QUuid, const ImageLayer *> byID;
    for (const ImageLayer &layer : existing)
        byID[layer.id] = &layer;
    // The folders around a layer, nearest first.
    using Chain = std::vector<QUuid>;
    const auto ancestors = [&](QUuid id) {
        Chain result;
        for (std::optional<QUuid> parent = byID.at(id)->parentID; parent; parent = byID.at(*parent)->parentID)
            result.push_back(*parent);
        return result;
    };
    // A selected folder carries its contents: they stay inside it.
    QSet<QUuid> rootIDs;
    for (const QUuid id : m_selectedLayerIDs) {
        if (!byID.count(id))
            continue;
        const Chain chain = ancestors(id);
        if (std::none_of(chain.begin(), chain.end(), [&](QUuid above) { return m_selectedLayerIDs.contains(above); }))
            rootIDs.insert(id);
    }
    std::vector<QUuid> ordered;
    std::vector<Chain> chains;
    for (const QUuid id : m_document->hierarchy().order) {
        if (rootIDs.contains(id)) {
            ordered.push_back(id);
            chains.push_back(ancestors(id));
        }
    }
    // The nearest folder they all share; the root when none.
    std::optional<QUuid> parent;
    for (const QUuid candidate : chains.empty() ? Chain() : chains.front()) {
        const auto holds = [&](const Chain &chain) { return std::find(chain.begin(), chain.end(), candidate) != chain.end(); };
        if (std::all_of(chains.begin(), chains.end(), holds)) {
            parent = candidate;
            break;
        }
    }
    int number = 1;
    const auto taken = [&](const QString &name) {
        return std::any_of(existing.begin(), existing.end(), [&](const ImageLayer &layer) { return layer.name == name; });
    };
    while (taken(QStringLiteral("Folder %1").arg(number)))
        ++number;
    ImageLayer group(QStringLiteral("Folder %1").arg(number), m_document->size());
    group.isGroup = true;
    group.parentID = parent;
    // It sits at the topmost selected branch under that parent.
    QSet<QUuid> branches;
    for (QUuid branch : ordered) {
        while (byID.at(branch)->parentID && byID.at(branch)->parentID != parent)
            branch = *byID.at(branch)->parentID;
        branches.insert(branch);
    }
    const auto isKept = [&](const ImageLayer &layer) { return !rootIDs.contains(layer.id); };
    const auto highest = std::find_if(existing.rbegin(), existing.rend(), [&](const ImageLayer &layer) { return branches.contains(layer.id); });
    // With nothing selected the folder goes on top.
    const auto insertion = highest == existing.rend() ? std::ptrdiff_t(existing.size()) : std::count_if(existing.begin(), highest.base(), isKept);
    std::vector<ImageLayer> layers;
    std::copy_if(existing.begin(), existing.end(), std::back_inserter(layers), isKept);
    layers.insert(layers.begin() + insertion, group);
    for (const QUuid id : ordered) {
        ImageLayer child = *byID.at(id);
        child.parentID = group.id;
        layers.push_back(child);
    }
    if (!isValidHierarchy(layers))
        return;
    beginEdit(QStringLiteral("Group Layers"));
    m_document->layers = layers;
    setActiveLayerID(group.id);
    if (parent)
        m_collapsedGroupIDs.remove(*parent);
    endEdit();
}

bool EditorSession::canUngroupLayers() const
{
    const std::optional<ImageLayer> active = activeLayer();
    return canEditLayers() && active && active->isGroup;
}

// The folder's children take its place, in order.
void EditorSession::ungroupLayers()
{
    if (!canUngroupLayers())
        return;
    const ImageLayer group = activeLayer().value();
    std::vector<ImageLayer> children;
    for (const ImageLayer &layer : m_document->layers) {
        if (layer.parentID == group.id) {
            children.push_back(layer);
            children.back().parentID = group.parentID;
        }
    }
    std::vector<ImageLayer> layers;
    QSet<QUuid> childIDs;
    for (const ImageLayer &child : children)
        childIDs.insert(child.id);
    for (const ImageLayer &layer : m_document->layers) {
        if (layer.id == group.id)
            layers.insert(layers.end(), children.begin(), children.end());
        else if (!childIDs.contains(layer.id))
            layers.push_back(layer);
    }
    releaseDetachedClipping(layers);
    finishOpacityEdit();
    beginEdit(QStringLiteral("Ungroup Layers"));
    m_document->layers = layers;
    selectLayers(childIDs, children.empty() ? std::nullopt : std::optional(children.front().id));
    m_collapsedGroupIDs.remove(group.id);
    endEdit();
}

std::vector<LayerHierarchy::Entry> EditorSession::layerRows() const
{
    std::vector<ProjectLayerRecord> records;
    if (m_document) {
        for (const ImageLayer &layer : m_document->layers)
            records.push_back(layer.hierarchyRecord());
    }
    return LayerHierarchy::entries(records, true, m_collapsedGroupIDs);
}

QSet<QUuid> EditorSession::descendantIDs(QUuid id) const
{
    QSet<QUuid> result;
    if (!m_document)
        return result;
    // One lookup of children by parent, then the walk.
    QMultiHash<QUuid, QUuid> children;
    for (const ImageLayer &layer : m_document->layers) {
        if (layer.parentID)
            children.insert(*layer.parentID, layer.id);
    }
    std::vector<QUuid> pending{id};
    while (!pending.empty()) {
        const QUuid parent = pending.back();
        pending.pop_back();
        for (const QUuid child : children.values(parent)) {
            if (!result.contains(child)) {
                result.insert(child);
                pending.push_back(child);
            }
        }
    }
    return result;
}

void EditorSession::addGroup()
{
    if (!canEditLayers() || m_document->layers.size() >= 10'000)
        return;
    int number = 1;
    const auto taken = [&](const QString &name) {
        return std::any_of(m_document->layers.begin(), m_document->layers.end(), [&](const ImageLayer &layer) { return layer.name == name; });
    };
    while (taken(QStringLiteral("Folder %1").arg(number)))
        ++number;
    ImageLayer group(QStringLiteral("Folder %1").arg(number), m_document->size());
    group.isGroup = true;
    const std::optional<ImageLayer> active = activeLayer();
    group.parentID = active && active->isGroup ? m_activeLayerID : active ? active->parentID : std::nullopt;
    std::vector<ImageLayer> layers = m_document->layers;
    const auto place = std::find_if(layers.begin(), layers.end(), [&](const ImageLayer &layer) { return m_activeLayerID && layer.id == *m_activeLayerID; });
    layers.insert(place == layers.end() ? place : place + 1, group);
    // A folder nested too deep is refused.
    if (!isValidHierarchy(layers))
        return;
    beginEdit(QStringLiteral("New Folder"));
    m_document->layers = layers;
    setActiveLayerID(group.id);
    if (group.parentID)
        m_collapsedGroupIDs.remove(*group.parentID);
    endEdit();
}

void EditorSession::toggleGroupExpansion(QUuid id)
{
    const auto isFolder = [&](const ImageLayer &layer) { return layer.id == id && layer.isGroup; };
    if (m_isProjectBusy || !m_document || std::none_of(m_document->layers.begin(), m_document->layers.end(), isFolder))
        return;
    if (m_collapsedGroupIDs.contains(id)) {
        m_collapsedGroupIDs.remove(id);
    } else {
        // A folded folder takes the selection from what it hides.
        if (m_activeLayerID && descendantIDs(id).contains(*m_activeLayerID))
            selectLayer(id);
        m_collapsedGroupIDs.insert(id);
    }
    notify();
}

bool EditorSession::canPlaceLayer(QUuid id, std::optional<QUuid> parent) const
{
    const auto holds = [&](QUuid wanted, bool folder) {
        return std::any_of(m_document->layers.begin(), m_document->layers.end(),
                           [&](const ImageLayer &layer) { return layer.id == wanted && (!folder || layer.isGroup); });
    };
    if (!canEditLayers() || !holds(id, false))
        return false;
    // Into a folder: never itself nor one of its contents.
    return !parent || (*parent != id && !descendantIDs(id).contains(*parent) && holds(*parent, true));
}

bool EditorSession::placeLayer(QUuid id, std::optional<QUuid> parent, std::optional<QUuid> above, bool atBottom)
{
    if (!canPlaceLayer(id, parent))
        return false;
    std::vector<ImageLayer> layers = m_document->layers;
    const auto from = std::find_if(layers.begin(), layers.end(), [&](const ImageLayer &layer) { return layer.id == id; });
    ImageLayer layer = *from;
    layers.erase(from);
    layer.parentID = parent;
    auto insertion = atBottom ? layers.begin() : layers.end();
    // The layer itself is gone by now: no target.
    if (above) {
        const auto target = std::find_if(layers.begin(), layers.end(),
                                         [&](const ImageLayer &each) { return each.id == *above && each.parentID == parent; });
        if (target == layers.end())
            return false;
        insertion = target + 1;
    }
    layers.insert(insertion, layer);
    // Dropped into a clipping stack it joins; broken ones release.
    adoptClipping(id, layers);
    releaseDetachedClipping(layers);
    if (!isValidHierarchy(layers))
        return false;
    beginEdit(QStringLiteral("Move Layer"));
    m_document->layers = layers;
    setActiveLayerID(id);
    if (parent)
        m_collapsedGroupIDs.remove(*parent);
    endEdit();
    return true;
}

void EditorSession::moveActiveLayerOutOfGroup()
{
    const std::optional<ImageLayer> layer = activeLayer();
    if (!layer || !layer->parentID)
        return;
    const auto group = std::find_if(m_document->layers.begin(), m_document->layers.end(),
                                    [&](const ImageLayer &each) { return each.id == *layer->parentID; });
    if (group != m_document->layers.end())
        placeLayer(layer->id, group->parentID, group->id);
}
