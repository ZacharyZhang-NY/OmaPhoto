#include "Document/EditorSession.h"

// Swift's session extension in LayerEffects.swift.
namespace {
// Swift's switch: one kind's effect, present or not.
void take(LayerEffects &into, const LayerEffects &from, LayerEffectKind kind)
{
    switch (kind) {
    case LayerEffectKind::stroke: into.stroke = from.stroke; return;
    case LayerEffectKind::shadow: into.shadow = from.shadow; return;
    case LayerEffectKind::colorOverlay: into.colorOverlay = from.colorOverlay; return;
    case LayerEffectKind::innerShadow: into.innerShadow = from.innerShadow; return;
    case LayerEffectKind::outerGlow: into.outerGlow = from.outerGlow; return;
    case LayerEffectKind::innerGlow: into.innerGlow = from.innerGlow; return;
    }
    throw std::logic_error("unknown effect kind");
}
}

// Swift's isGroup terms are dead: folders never hold pixels.
bool EditorSession::canEditEffects() const
{
    const std::optional<ImageLayer> layer = activeLayer();
    return canEditLayers() && layer && layer->asset;
}

LayerEffects EditorSession::activeEffects() const
{
    const std::optional<ImageLayer> layer = activeLayer();
    return layer && layer->effects ? *layer->effects : LayerEffects();
}

LayerEffects EditorSession::editingEffects() const
{
    const int index = m_document && m_effectsEditing ? indexOf(m_document->layers, m_effectsEditing->layerID) : -1;
    return index >= 0 ? m_document->layers[size_t(index)].effects.value_or(LayerEffects()) : LayerEffects();
}

std::optional<LayerEffectSelection> EditorSession::selectedEffect() const
{
    if (!m_effectSelection || m_effectSelection->layerID != m_activeLayerID || !activeEffects().contains(m_effectSelection->kind))
        return std::nullopt;
    return m_effectSelection;
}

void EditorSession::addEffect(LayerEffectKind kind)
{
    if (!canEditEffects())
        return;
    const QUuid id = m_activeLayerID.value();
    if (m_effectsEditing == LayerEffectSelection{id, kind})
        return;
    finishEffectsEditing(false);
    const LayerEffects original = activeEffects();
    LayerEffects effects = original;
    // A new stroke or overlay takes the background colour.
    const PaletteColor ground = m_backgroundColor;
    switch (kind) {
    case LayerEffectKind::stroke:
        if (!effects.stroke)
            effects.stroke = StrokeEffect{.red = ground.red, .green = ground.green, .blue = ground.blue};
        break;
    case LayerEffectKind::shadow:
        if (!effects.shadow)
            effects.shadow = ShadowEffect();
        break;
    case LayerEffectKind::colorOverlay:
        if (!effects.colorOverlay)
            effects.colorOverlay = ColorOverlayEffect{.red = ground.red, .green = ground.green, .blue = ground.blue};
        break;
    case LayerEffectKind::innerShadow:
        if (!effects.innerShadow)
            effects.innerShadow = InnerShadowEffect();
        break;
    case LayerEffectKind::outerGlow:
        if (!effects.outerGlow)
            effects.outerGlow = OuterGlowEffect();
        break;
    case LayerEffectKind::innerGlow:
        if (!effects.innerGlow)
            effects.innerGlow = InnerGlowEffect();
        break;
    }
    setEffects(effects, id, QStringLiteral("Add ") + rawValue(kind));
    selectEffect(kind, id, true);
    m_effectsEditingOriginal = original;
    notify();
}

void EditorSession::selectEffect(LayerEffectKind kind, QUuid id, bool editing)
{
    const int index = m_document ? indexOf(m_document->layers, id) : -1;
    if (!canEditLayers() || index < 0 || !m_document->layers[size_t(index)].effects || !m_document->layers[size_t(index)].effects->contains(kind))
        return;
    const LayerEffectSelection selection{id, kind};
    if (editing && m_effectsEditing != selection)
        finishEffectsEditing(false);
    // Swift's `selectedLayerIDs = [id]`: selectLayer sets it, unrefused here.
    selectLayer(id);
    setIsMaskSelected(false);
    m_effectSelection = selection;
    if (editing && m_effectsEditing != selection) {
        m_effectsEditingOriginal = m_document->layers[size_t(indexOf(m_document->layers, id))].effects.value_or(LayerEffects());
        m_effectsEditing = selection;
    }
    notify();
}

void EditorSession::finishEffectsEditing(bool commit)
{
    if (!m_effectsEditing)
        return;
    const LayerEffectSelection editing = *m_effectsEditing;
    closeEffectColorPicker(commit);
    const int index = m_document ? indexOf(m_document->layers, editing.layerID) : -1;
    if (!commit && m_effectsEditingOriginal && index >= 0 && m_document->layers[size_t(index)].effects) {
        LayerEffects effects = *m_document->layers[size_t(index)].effects;
        take(effects, *m_effectsEditingOriginal, editing.kind);
        setEffects(effects, editing.layerID, QStringLiteral("Cancel ") + rawValue(editing.kind));
    }
    m_effectsEditing.reset();
    m_effectsEditingOriginal.reset();
    if (!selectedEffect())
        m_effectSelection.reset();
    notify();
}

void EditorSession::setEffects(const LayerEffects &effects, std::optional<QUuid> id, const QString &name)
{
    const int index = m_document ? indexOf(m_document->layers, id ? id : m_activeLayerID) : -1;
    if (!canEditLayers() || !effects.isValid() || index < 0)
        return;
    const ImageLayer &layer = m_document->layers[size_t(index)];
    const std::optional<LayerEffects> stored = effects.isEmpty() ? std::nullopt : std::optional(effects);
    if (!layer.asset || layer.effects == stored)
        return;
    finishOpacityEdit();
    beginEdit(name);
    m_document->layers[size_t(index)].effects = stored;
    endEdit();
}

void EditorSession::changeEffects(const std::function<void(LayerEffects &)> &change)
{
    const int index = m_document && m_effectsEditing ? indexOf(m_document->layers, m_effectsEditing->layerID) : -1;
    if (index < 0 || !m_document->layers[size_t(index)].effects || !m_document->layers[size_t(index)].effects->contains(m_effectsEditing->kind))
        return;
    LayerEffects effects = *m_document->layers[size_t(index)].effects;
    change(effects);
    setEffects(effects, m_effectsEditing->layerID, QStringLiteral("Edit ") + rawValue(m_effectsEditing->kind));
}

bool EditorSession::canCopyEffect(LayerEffectKind kind, QUuid source, QUuid target) const
{
    const int from = m_document ? indexOf(m_document->layers, source) : -1, to = m_document ? indexOf(m_document->layers, target) : -1;
    if (!canEditLayers() || source == target || from < 0 || to < 0)
        return false;
    const ImageLayer &sourceLayer = m_document->layers[size_t(from)], &targetLayer = m_document->layers[size_t(to)];
    return sourceLayer.effects && sourceLayer.effects->contains(kind) && targetLayer.asset;
}

void EditorSession::copyEffect(LayerEffectKind kind, QUuid source, QUuid target)
{
    if (!canCopyEffect(kind, source, target))
        return;
    const LayerEffects original = m_document->layers[size_t(indexOf(m_document->layers, source))].effects.value();
    // Its editor closes first: a later Cancel keeps the copy.
    if (m_effectsEditing == LayerEffectSelection{target, kind})
        finishEffectsEditing(true);
    LayerEffects effects = m_document->layers[size_t(indexOf(m_document->layers, target))].effects.value_or(LayerEffects());
    take(effects, original, kind);
    setEffects(effects, target, QStringLiteral("Copy ") + rawValue(kind));
    selectEffect(kind, target);
}

void EditorSession::toggleEffect(LayerEffectKind kind, QUuid id)
{
    const int index = m_document ? indexOf(m_document->layers, id) : -1;
    if (index < 0 || !m_document->layers[size_t(index)].effects)
        return;
    LayerEffects effects = *m_document->layers[size_t(index)].effects;
    const bool enabled = effects.isEnabled(kind);
    effects.setEnabled(!enabled, kind);
    setEffects(effects, id, (enabled ? QStringLiteral("Hide ") : QStringLiteral("Show ")) + rawValue(kind));
}

void EditorSession::removeSelectedEffect()
{
    const std::optional<LayerEffectSelection> selected = selectedEffect();
    const int index = m_document && selected ? indexOf(m_document->layers, selected->layerID) : -1;
    if (!selected || !canEditLayers() || index < 0 || !m_document->layers[size_t(index)].effects)
        return;
    LayerEffects effects = *m_document->layers[size_t(index)].effects;
    if (m_effectsEditing == selected) {
        closeEffectColorPicker(false);
        m_effectsEditing.reset();
        m_effectsEditingOriginal.reset();
    }
    effects.remove(selected->kind);
    setEffects(effects, selected->layerID, QStringLiteral("Remove ") + rawValue(selected->kind));
    m_effectSelection.reset();
    notify();
}

void EditorSession::setEffectsEditing(std::optional<LayerEffectSelection> selection)
{
    if (std::exchange(m_effectsEditing, selection) != selection)
        notify();
}

void EditorSession::setEffectsEditingOriginal(std::optional<LayerEffects> original)
{
    if (std::exchange(m_effectsEditingOriginal, original) != original)
        notify();
}

void EditorSession::dropEffectSelection()
{
    if (std::exchange(m_effectSelection, std::nullopt))
        notify();
}

void EditorSession::closeEffectColorPicker(bool commit)
{
    if (m_colorPicker && m_colorPicker->target.kind == ColorPickerTarget::Kind::effect)
        closeColorPicker(commit);
}
