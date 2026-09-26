#include "Document/LayerAppearance.h"
#include "Document/EditorSession.h"
#include <algorithm>
#include <cmath>
#include <array>
#include <utility>

namespace {
const std::array<std::pair<LayerBlendMode, const char *>, 14> names{{
    {LayerBlendMode::normal, "Normal"}, {LayerBlendMode::multiply, "Multiply"}, {LayerBlendMode::screen, "Screen"},
    {LayerBlendMode::overlay, "Overlay"}, {LayerBlendMode::softLight, "Soft Light"}, {LayerBlendMode::darken, "Darken"}, {LayerBlendMode::lighten, "Lighten"},
    {LayerBlendMode::difference, "Difference"}, {LayerBlendMode::colorDodge, "Color Dodge"},
    {LayerBlendMode::colorBurn, "Color Burn"}, {LayerBlendMode::hue, "Hue"}, {LayerBlendMode::saturation, "Saturation"},
    {LayerBlendMode::color, "Color"}, {LayerBlendMode::luminosity, "Luminosity"},
}};
}

QString rawValue(LayerBlendMode mode)
{
    return QString::fromLatin1(names.at(size_t(mode)).second);
}

std::optional<LayerBlendMode> layerBlendMode(const QString &rawValue)
{
    for (const auto &[mode, name] : names) {
        if (rawValue == QLatin1String(name))
            return mode;
    }
    return std::nullopt;
}

LayerBlendMode EditorSession::displayedBlendMode(const ImageLayer &layer) const
{
    if (m_blendPreview && m_blendPreview->layerID == layer.id && m_activeLayerID == layer.id)
        return m_blendPreview->mode;
    return layer.blendMode;
}

void EditorSession::previewBlendMode(std::optional<LayerBlendMode> mode, std::optional<QUuid> id)
{
    std::optional<BlendPreview> preview;
    if (mode && id && id == m_activeLayerID && canEditAppearance())
        preview = BlendPreview{*id, *mode};
    // Hovering over the same entry again says nothing.
    if (preview == m_blendPreview)
        return;
    m_blendPreview = preview;
    notify();
}

bool EditorSession::canEditAppearance() const
{
    const std::optional<ImageLayer> active = activeLayer();
    return canEditLayers() && m_selectedLayerIDs.size() == 1 && active && !active->isGroup;
}

bool EditorSession::canEditOpacity() const
{
    return canEditLayers() && m_selectedLayerIDs.size() == 1 && activeLayer();
}

void EditorSession::beginOpacityEdit()
{
    if (!canEditOpacity() || m_opacityEditLayerID)
        return;
    beginEdit(QStringLiteral("Layer Opacity"));
    m_opacityEditLayerID = m_activeLayerID;
}

void EditorSession::finishOpacityEdit()
{
    if (!m_opacityEditLayerID)
        return;
    m_opacityEditLayerID = std::nullopt;
    endEdit();
}

void EditorSession::setLayerOpacity(double opacity)
{
    if (!std::isfinite(opacity) || !canEditOpacity())
        return;
    const QUuid id = m_opacityEditLayerID.value_or(*m_activeLayerID);
    const auto layer = std::find_if(m_document->layers.begin(), m_document->layers.end(), [&](const ImageLayer &each) { return each.id == id; });
    if (layer == m_document->layers.end())
        return;
    // Inside a drag this nests in the drag's one step.
    beginEdit(QStringLiteral("Layer Opacity"));
    layer->opacity = std::clamp(opacity, 0.0, 1.0);
    endEdit();
}

void EditorSession::setSelectedLayersOpacity(double opacity)
{
    if (!std::isfinite(opacity) || !canEditLayers())
        return;
    // A selected folder takes it too, dimming what it holds.
    const double value = std::clamp(opacity, 0.0, 1.0);
    const auto changes = [&](const ImageLayer &layer) { return m_selectedLayerIDs.contains(layer.id) && layer.opacity != value; };
    if (std::none_of(m_document->layers.begin(), m_document->layers.end(), changes))
        return;
    finishOpacityEdit();
    beginEdit(QStringLiteral("Layer Opacity"));
    for (ImageLayer &layer : m_document->layers) {
        if (changes(layer))
            layer.opacity = value;
    }
    endEdit();
}

void EditorSession::cycleBlendMode(bool forward)
{
    const std::optional<ImageLayer> layer = activeLayer();
    if (!canEditAppearance() || !layer)
        return;
    const auto found = std::find(allLayerBlendModes.begin(), allLayerBlendModes.end(), layer->blendMode);
    const size_t count = allLayerBlendModes.size(), index = size_t(found - allLayerBlendModes.begin());
    setLayerBlendMode(allLayerBlendModes[(index + (forward ? 1 : count - 1)) % count]);
}

void EditorSession::setLayerBlendMode(LayerBlendMode mode)
{
    if (m_blendPreview) {
        m_blendPreview = std::nullopt;
        notify();
    }
    if (!canEditAppearance())
        return;
    finishOpacityEdit();
    beginEdit(QStringLiteral("Layer Blend Mode"));
    for (ImageLayer &layer : m_document->layers) {
        if (layer.id == m_activeLayerID)
            layer.blendMode = mode;
    }
    endEdit();
}
