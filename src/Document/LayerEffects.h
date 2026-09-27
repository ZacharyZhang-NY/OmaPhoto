#pragma once
#include "Document/ColorPalette.h"
#include <QSizeF>
#include <QString>
#include <QUuid>
#include <array>
#include <optional>
#include <vector>

// A line round what the layer shows, outside or in.
struct StrokeEffect {
    // Swift's supported width in document pixels.
    static constexpr double maxSize = 500;
    // Absent in older projects, which means shown.
    std::optional<bool> enabled = std::nullopt;
    double size = 4;
    double red = 0;
    double green = 0;
    double blue = 0;
    double opacity = 1;
    bool inside = false;
    bool isEnabled() const { return enabled.value_or(true); }
    PaletteColor color() const { return {red, green, blue}; }
    bool isValid() const;
    friend bool operator==(const StrokeEffect &, const StrokeEffect &) = default;
};

// The layer's shape behind it, offset and softened.
struct ShadowEffect {
    std::optional<bool> enabled = std::nullopt;
    // Where the light comes from, counterclockwise from the right.
    double angle = 90;
    double distance = 20;
    double blur = 20;
    double red = 0;
    double green = 0;
    double blue = 0;
    double opacity = 0.5;
    bool isEnabled() const { return enabled.value_or(true); }
    PaletteColor color() const { return {red, green, blue}; }
    // In layer pixels, y down: away from the light.
    QSizeF offset() const;
    bool isValid() const;
    friend bool operator==(const ShadowEffect &, const ShadowEffect &) = default;
};

// A flat colour over everything the layer shows.
struct ColorOverlayEffect {
    std::optional<bool> enabled = std::nullopt;
    double red = 0;
    double green = 0;
    double blue = 0;
    double opacity = 1;
    bool isEnabled() const { return enabled.value_or(true); }
    PaletteColor color() const { return {red, green, blue}; }
    bool isValid() const;
    friend bool operator==(const ColorOverlayEffect &, const ColorOverlayEffect &) = default;
};

// A shadow inside the layer's own edges.
struct InnerShadowEffect {
    std::optional<bool> enabled = std::nullopt;
    double angle = 90;
    double distance = 10;
    double blur = 10;
    double red = 0;
    double green = 0;
    double blue = 0;
    double opacity = 0.5;
    bool isEnabled() const { return enabled.value_or(true); }
    PaletteColor color() const { return {red, green, blue}; }
    QSizeF offset() const;
    bool isValid() const;
    friend bool operator==(const InnerShadowEffect &, const InnerShadowEffect &) = default;
};

// A soft glow round the outside of the layer.
struct OuterGlowEffect {
    std::optional<bool> enabled = std::nullopt;
    double size = 20;
    double red = 1;
    double green = 1;
    double blue = 1;
    double opacity = 0.75;
    bool isEnabled() const { return enabled.value_or(true); }
    PaletteColor color() const { return {red, green, blue}; }
    bool isValid() const;
    friend bool operator==(const OuterGlowEffect &, const OuterGlowEffect &) = default;
};

// Swift's raw values are the panel's and the menu's words.
enum class LayerEffectKind { stroke, shadow, colorOverlay, innerShadow, outerGlow };
inline constexpr std::array allLayerEffectKinds{LayerEffectKind::stroke, LayerEffectKind::shadow, LayerEffectKind::colorOverlay,
                                                LayerEffectKind::innerShadow, LayerEffectKind::outerGlow};
QString rawValue(LayerEffectKind kind);

// What a layer draws round itself; its pixels stay untouched.
struct LayerEffects {
    std::optional<StrokeEffect> stroke = std::nullopt;
    std::optional<ShadowEffect> shadow = std::nullopt;
    std::optional<ColorOverlayEffect> colorOverlay = std::nullopt;
    std::optional<InnerShadowEffect> innerShadow = std::nullopt;
    std::optional<OuterGlowEffect> outerGlow = std::nullopt;
    bool isEmpty() const { return !stroke && !shadow && !colorOverlay && !innerShadow && !outerGlow; }
    bool isValid() const;
    std::vector<LayerEffectKind> kinds() const;
    bool contains(LayerEffectKind kind) const;
    bool isEnabled(LayerEffectKind kind) const;
    std::optional<PaletteColor> color(LayerEffectKind kind) const;
    void setColor(const PaletteColor &color, LayerEffectKind kind);
    void remove(LayerEffectKind kind);
    void setEnabled(bool enabled, LayerEffectKind kind);
    // The effects that are switched on.
    LayerEffects visible() const;
    friend bool operator==(const LayerEffects &, const LayerEffects &) = default;
};

// One effect of one layer: the panel's and the list's.
struct LayerEffectSelection {
    QUuid layerID;
    LayerEffectKind kind;
    friend bool operator==(const LayerEffectSelection &, const LayerEffectSelection &) = default;
};
