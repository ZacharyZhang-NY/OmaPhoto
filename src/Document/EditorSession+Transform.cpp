#include "Document/Distort.h"
#include "Document/EditorSession.h"
#include <algorithm>

bool EditorSession::canTransform() const
{
    if (!canEditLayers())
        return false;
    if (transformsAsGroup())
        return !groupTransformMembers().empty();
    const std::optional<ImageLayer> active = activeLayer();
    return active && active->asset && m_document->effectiveVisibleIDs().contains(active->id);
}

bool EditorSession::transformsAsGroup() const
{
    const std::optional<ImageLayer> active = activeLayer();
    return m_selectedLayerIDs.size() > 1 || (m_selectedLayerIDs.size() == 1 && active && active->isGroup);
}

std::vector<ImageLayer> EditorSession::groupTransformMembers() const
{
    if (!transformsAsGroup() || !m_document)
        return {};
    QHash<QUuid, std::optional<QUuid>> parents;
    for (const ImageLayer &layer : m_document->layers)
        parents.insert(layer.id, layer.parentID);
    const QSet<QUuid> visible = m_document->effectiveVisibleIDs();
    std::vector<ImageLayer> members;
    for (const ImageLayer &layer : m_document->layers) {
        if (!layer.asset || !visible.contains(layer.id))
            continue;
        // Selected itself, or inside a selected folder.
        std::optional<QUuid> current = layer.id;
        for (int step = 0; step < 64 && current; ++step) {
            if (m_selectedLayerIDs.contains(*current)) {
                members.push_back(layer);
                break;
            }
            current = parents.value(*current);
        }
    }
    return members;
}

std::optional<LayerTransform> EditorSession::groupTransformBox() const
{
    const std::vector<ImageLayer> members = groupTransformMembers();
    if (members.empty())
        return std::nullopt;
    QPointF low = DistortWarp::corners(members.front().transform)[0], high = low;
    for (const ImageLayer &member : members) {
        for (const QPointF corner : DistortWarp::corners(member.transform)) {
            low = {std::min(low.x(), corner.x()), std::min(low.y(), corner.y())};
            high = {std::max(high.x(), corner.x()), std::max(high.y(), corner.y())};
        }
    }
    // Rounding can leave a unit layer's box under one pixel.
    return LayerTransform{.origin = low, .size = {std::max(1.0, high.x() - low.x()), std::max(1.0, high.y() - low.y())}};
}

void EditorSession::beginTransform(bool persistent)
{
    cancelCrop();
    const std::optional<ImageLayer> layer = activeLayer();
    if (!canTransform() || !layer)
        return;
    m_tool = NavigationTool::move;
    if (transformsAsGroup()) {
        const std::optional<LayerTransform> box = groupTransformBox();
        if (!box)
            return;
        QHash<QUuid, LayerTransform> originals;
        for (const ImageLayer &member : groupTransformMembers())
            originals.insert(member.id, member.transform);
        m_transformEdit = TransformEdit{layer->id, *box, TransformGroup{*box, originals}, false, persistent, std::nullopt, nullptr};
    } else {
        // Linked, layer and mask move together as the layer.
        const bool maskAlone = transformTargetsMask();
        m_transformEdit = TransformEdit{layer->id, maskAlone ? layer->maskTransform() : layer->transform, std::nullopt, maskAlone, persistent, std::nullopt, nullptr};
    }
    notify();
}

void EditorSession::setTransformAutoSelect(bool picks)
{
    m_transformAutoSelect = picks;
    notify();
}

void EditorSession::setShowsTransformControls(bool shows)
{
    m_showsTransformControls = shows;
    notify();
}

void EditorSession::setLocksTransformRatio(bool locks)
{
    m_locksTransformRatio = locks;
    notify();
}

// Alt-drag copies selected roots with their contents.
void EditorSession::beginDuplicateTransform()
{
    if (m_transformDuplicate || !m_activeLayerID)
        return;
    const QUuid primary = *m_activeLayerID;
    commitTransform();
    if (!canTransform())
        return;
    const QSet<QUuid> selection = m_selectedLayerIDs;
    // A selected folder carries its selected contents along.
    QSet<QUuid> carried;
    for (const QUuid &id : selection)
        carried.unite(descendantIDs(id));
    // Bottom to top: the copies keep their order.
    std::vector<QUuid> targets;
    for (const ImageLayer &layer : m_document->layers) {
        if (selection.contains(layer.id) && !carried.contains(layer.id))
            targets.push_back(layer.id);
    }
    if (targets.empty())
        return;
    beginEdit(targets.size() > 1 ? QStringLiteral("Duplicate Layers") : QStringLiteral("Duplicate Layer"));
    std::vector<QUuid> copies;
    for (const QUuid &id : targets) {
        selectLayer(id);
        duplicateActiveLayer();
        if (m_activeLayerID != id)
            copies.push_back(m_activeLayerID.value());
    }
    // Past the layer limit nothing was copied.
    if (copies.empty()) {
        endEdit();
        selectLayers(selection, primary);
        return;
    }
    m_transformDuplicate = TransformDuplicate{copies, selection, primary};
    selectLayers(QSet<QUuid>(copies.begin(), copies.end()), copies.back());
    beginTransform(false);
}

// The duplicate's step closes with its transform.
void EditorSession::endDuplicateTransform()
{
    if (!m_transformDuplicate)
        return;
    m_transformDuplicate = std::nullopt;
    endEdit();
}

void EditorSession::previewTransform(const LayerTransform &value)
{
    if (!value.isValid() || !m_transformEdit)
        return;
    m_transformEdit->draft = value;
    notify();
}

void EditorSession::commitTransform()
{
    snapGuides = {};
    // Whatever ends a transform ends these two as well.
    if (m_blendPreview) {
        m_blendPreview = std::nullopt;
        notify();
    }
    finishOpacityEdit();
    if (!m_transformEdit)
        return;
    if (m_transformEdit->floating) {
        // Unchanged: restored exactly, so soft edges keep no seam.
        const TransformEdit edit = *std::exchange(m_transformEdit, std::nullopt);
        if (edit.draft == edit.floating->original && !edit.corners)
            cancelFloatingTransform(*edit.floating);
        else
            mergeFloatingTransform(edit, *edit.floating);
        return;
    }
    if (m_transformEdit->mask) {
        commitMaskTransform();
        return;
    }
    if (m_transformEdit->corners) {
        const TransformEdit edit = *m_transformEdit;
        m_transformEdit = std::nullopt;
        commitDistort(edit, *edit.corners);
        endDuplicateTransform();
        return;
    }
    // One transaction: an edit that moves nothing records nothing.
    beginEdit(m_transformEdit->group ? QStringLiteral("Transform Layers") : QStringLiteral("Transform Layer"));
    for (int index = 0; index < int(m_document->layers.size()); ++index) {
        ImageLayer &layer = m_document->layers[size_t(index)];
        // A layer the box squeezes under a pixel stays put.
        const std::optional<LayerTransform> moved = pendingTransform(layer);
        if (!moved || !moved->isValid())
            continue;
        // A linked mask goes along; an unlinked one stays.
        if (layer.mask)
            layer.mask->placement = layer.mask->placementMovingLayer(layer.transform, *moved);
        layer.transform = *moved;
        redrawShape(index);
    }
    m_transformEdit = std::nullopt;
    endEdit();
    endDuplicateTransform();
}

void EditorSession::cancelTransform()
{
    snapGuides = {};
    if (!m_transformEdit)
        return;
    const std::shared_ptr<const FloatingTransform> floating = std::exchange(m_transformEdit, std::nullopt)->floating;
    if (m_transformDuplicate) {
        // The copies go and the old selection returns.
        const TransformDuplicate duplicate = *m_transformDuplicate;
        QSet<QUuid> removed(duplicate.copies.begin(), duplicate.copies.end());
        for (const QUuid &copy : duplicate.copies)
            removed.unite(descendantIDs(copy));
        std::erase_if(m_document->layers, [&](const ImageLayer &layer) { return removed.contains(layer.id); });
        m_collapsedGroupIDs.subtract(removed);
        selectLayers(duplicate.source, duplicate.primary);
        endDuplicateTransform();
    }
    if (floating)
        cancelFloatingTransform(*floating);
    notify();
}

std::optional<QSizeF> EditorSession::transformPixelSize() const
{
    if (m_transformEdit && m_transformEdit->group)
        return m_transformEdit->group->box.size;
    if (!m_transformEdit && transformsAsGroup()) {
        const std::optional<LayerTransform> box = groupTransformBox();
        return box ? std::optional(box->size) : std::nullopt;
    }
    // A mask has no pixel size to scale against.
    if (transformTargetsMask())
        return std::nullopt;
    if (m_transformEdit && m_transformEdit->floating)
        return m_transformEdit->floating->pixelSize;
    const std::optional<ImageLayer> active = activeLayer();
    return active && active->asset ? std::optional(QSizeF(active->asset->size())) : std::nullopt;
}

bool EditorSession::transformTargetsMask() const
{
    if (m_transformEdit)
        return m_transformEdit->mask;
    const std::optional<ImageLayer> active = activeLayer();
    return m_isMaskSelected && active && active->mask && !active->mask->isLinked;
}

LayerTransform EditorSession::displayedTransform(const ImageLayer &layer) const
{
    if (const std::optional<LayerTransform> pending = pendingTransform(layer))
        return *pending;
    // Content-Aware Fill past the edge previews on the grown layer.
    if (m_filterEdit && m_filterEdit->grownTransform && m_filterEdit->previewImage(layer.id))
        return *m_filterEdit->grownTransform;
    return layer.transform;
}

LayerTransform EditorSession::editedTransform(const ImageLayer &layer) const
{
    if (m_transformEdit && m_transformEdit->layerID == layer.id)
        return m_transformEdit->draft;
    // Before an edit the handles sit on the group's box.
    if (!m_transformEdit && layer.id == m_activeLayerID && transformsAsGroup())
        return groupTransformBox().value_or(layer.transform);
    return layer.id == m_activeLayerID && transformTargetsMask() ? layer.maskTransform() : layer.transform;
}

std::optional<LayerTransform> EditorSession::pendingTransform(const ImageLayer &layer) const
{
    // An edit of the mask alone moves no layer.
    if (!m_transformEdit || m_transformEdit->mask)
        return std::nullopt;
    if (m_transformEdit->group) {
        const TransformGroup &group = *m_transformEdit->group;
        const auto original = group.originals.constFind(layer.id);
        return original == group.originals.constEnd() ? std::nullopt : std::optional(original->following(group.box, m_transformEdit->draft));
    }
    return m_transformEdit->layerID == layer.id ? std::optional(m_transformEdit->draft) : std::nullopt;
}

void EditorSession::nudgeLayer(double dx, double dy)
{
    const bool alreadyEditing = m_transformEdit.has_value();
    if (!alreadyEditing)
        beginTransform(false);
    if (!m_transformEdit)
        return;
    LayerTransform value = m_transformEdit->draft;
    value.origin += QPointF(dx, dy);
    previewTransform(value);
    if (const std::optional<Corners> corners = m_transformEdit->corners) {
        Corners moved = *corners;
        for (QPointF &corner : moved)
            corner += QPointF(dx, dy);
        previewCorners(moved);
    }
    if (!alreadyEditing)
        commitTransform();
}
