#pragma once
#include <QString>
#include <array>
#include <optional>

enum class LayerBlendMode {
    normal, multiply, screen, overlay, darken, lighten, difference,
    colorDodge, colorBurn, hue, saturation, color, luminosity
};

// Swift's `allCases`: the order of the blend menu.
inline constexpr std::array allLayerBlendModes{
    LayerBlendMode::normal, LayerBlendMode::multiply, LayerBlendMode::screen, LayerBlendMode::overlay,
    LayerBlendMode::darken, LayerBlendMode::lighten, LayerBlendMode::difference, LayerBlendMode::colorDodge,
    LayerBlendMode::colorBurn, LayerBlendMode::hue, LayerBlendMode::saturation, LayerBlendMode::color,
    LayerBlendMode::luminosity};

// The name users see and the manifest stores.
QString rawValue(LayerBlendMode mode);
std::optional<LayerBlendMode> layerBlendMode(const QString &rawValue);
