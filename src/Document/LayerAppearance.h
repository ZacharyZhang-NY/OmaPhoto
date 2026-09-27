#pragma once
#include <QString>
#include <array>
#include <optional>

enum class LayerBlendMode {
    normal,
    darken, multiply, colorBurn, linearBurn,
    lighten, screen, colorDodge, linearDodge,
    overlay, softLight, hardLight, vividLight, linearLight, pinLight, hardMix,
    difference, exclusion, subtract, divide,
    hue, saturation, color, luminosity
};

// Swift's `groups`, Photoshop's: the menu draws a line between.
int blendGroup(LayerBlendMode mode);

// Swift's `allCases`: the order of the blend menu.
inline constexpr std::array allLayerBlendModes{
    LayerBlendMode::normal, LayerBlendMode::darken, LayerBlendMode::multiply, LayerBlendMode::colorBurn, LayerBlendMode::linearBurn,
    LayerBlendMode::lighten, LayerBlendMode::screen, LayerBlendMode::colorDodge, LayerBlendMode::linearDodge,
    LayerBlendMode::overlay, LayerBlendMode::softLight, LayerBlendMode::hardLight, LayerBlendMode::vividLight, LayerBlendMode::linearLight,
    LayerBlendMode::pinLight, LayerBlendMode::hardMix, LayerBlendMode::difference, LayerBlendMode::exclusion, LayerBlendMode::subtract,
    LayerBlendMode::divide, LayerBlendMode::hue, LayerBlendMode::saturation, LayerBlendMode::color, LayerBlendMode::luminosity};

// The name users see and the manifest stores.
QString rawValue(LayerBlendMode mode);
std::optional<LayerBlendMode> layerBlendMode(const QString &rawValue);
