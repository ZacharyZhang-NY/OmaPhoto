#include "Document/LayerEffects.h"
#include <cmath>
#include <stdexcept>

namespace {
bool within(double value, double low, double high)
{
    return value >= low && value <= high;
}

bool colourIsValid(double red, double green, double blue)
{
    return within(red, 0, 1) && within(green, 0, 1) && within(blue, 0, 1);
}

// Away from the light; a layer's pixels count y downward.
QSizeF fallingFrom(double angle, double distance)
{
    const double radians = angle * M_PI / 180;
    return QSizeF(-std::cos(radians) * distance, std::sin(radians) * distance);
}

bool shadowIsValid(double angle, double distance, double blur, double opacity)
{
    return within(angle, -360, 360) && within(distance, 0, 5000) && within(blur, 0, 500) && within(opacity, 0, 1);
}
}

bool StrokeEffect::isValid() const
{
    return within(size, 0, maxSize) && within(opacity, 0, 1) && colourIsValid(red, green, blue);
}

QSizeF ShadowEffect::offset() const
{
    return fallingFrom(angle, distance);
}

bool ShadowEffect::isValid() const
{
    return shadowIsValid(angle, distance, blur, opacity) && colourIsValid(red, green, blue);
}

bool ColorOverlayEffect::isValid() const
{
    return within(opacity, 0, 1) && colourIsValid(red, green, blue);
}

QSizeF InnerShadowEffect::offset() const
{
    return fallingFrom(angle, distance);
}

bool InnerShadowEffect::isValid() const
{
    return shadowIsValid(angle, distance, blur, opacity) && colourIsValid(red, green, blue);
}

bool OuterGlowEffect::isValid() const
{
    return within(size, 0, 500) && within(opacity, 0, 1) && colourIsValid(red, green, blue);
}

bool InnerGlowEffect::isValid() const
{
    return within(size, 0, 500) && within(opacity, 0, 1) && colourIsValid(red, green, blue);
}

QString rawValue(LayerEffectKind kind)
{
    switch (kind) {
    case LayerEffectKind::stroke: return QStringLiteral("Stroke");
    case LayerEffectKind::shadow: return QStringLiteral("Drop Shadow");
    case LayerEffectKind::colorOverlay: return QStringLiteral("Color Overlay");
    case LayerEffectKind::innerShadow: return QStringLiteral("Inner Shadow");
    case LayerEffectKind::outerGlow: return QStringLiteral("Outer Glow");
    case LayerEffectKind::innerGlow: return QStringLiteral("Inner Glow");
    }
    throw std::logic_error("unknown effect kind");
}

bool LayerEffects::isValid() const
{
    return (!stroke || stroke->isValid()) && (!shadow || shadow->isValid()) && (!colorOverlay || colorOverlay->isValid())
        && (!innerShadow || innerShadow->isValid()) && (!outerGlow || outerGlow->isValid()) && (!innerGlow || innerGlow->isValid());
}

std::vector<LayerEffectKind> LayerEffects::kinds() const
{
    std::vector<LayerEffectKind> result;
    for (const LayerEffectKind kind : allLayerEffectKinds) {
        if (contains(kind))
            result.push_back(kind);
    }
    return result;
}

bool LayerEffects::contains(LayerEffectKind kind) const
{
    switch (kind) {
    case LayerEffectKind::stroke: return stroke.has_value();
    case LayerEffectKind::shadow: return shadow.has_value();
    case LayerEffectKind::colorOverlay: return colorOverlay.has_value();
    case LayerEffectKind::innerShadow: return innerShadow.has_value();
    case LayerEffectKind::outerGlow: return outerGlow.has_value();
    case LayerEffectKind::innerGlow: return innerGlow.has_value();
    }
    throw std::logic_error("unknown effect kind");
}

bool LayerEffects::isEnabled(LayerEffectKind kind) const
{
    switch (kind) {
    case LayerEffectKind::stroke: return stroke && stroke->isEnabled();
    case LayerEffectKind::shadow: return shadow && shadow->isEnabled();
    case LayerEffectKind::colorOverlay: return colorOverlay && colorOverlay->isEnabled();
    case LayerEffectKind::innerShadow: return innerShadow && innerShadow->isEnabled();
    case LayerEffectKind::outerGlow: return outerGlow && outerGlow->isEnabled();
    case LayerEffectKind::innerGlow: return innerGlow && innerGlow->isEnabled();
    }
    throw std::logic_error("unknown effect kind");
}

std::optional<PaletteColor> LayerEffects::color(LayerEffectKind kind) const
{
    switch (kind) {
    case LayerEffectKind::stroke: return stroke ? std::optional(stroke->color()) : std::nullopt;
    case LayerEffectKind::shadow: return shadow ? std::optional(shadow->color()) : std::nullopt;
    case LayerEffectKind::colorOverlay: return colorOverlay ? std::optional(colorOverlay->color()) : std::nullopt;
    case LayerEffectKind::innerShadow: return innerShadow ? std::optional(innerShadow->color()) : std::nullopt;
    case LayerEffectKind::outerGlow: return outerGlow ? std::optional(outerGlow->color()) : std::nullopt;
    case LayerEffectKind::innerGlow: return innerGlow ? std::optional(innerGlow->color()) : std::nullopt;
    }
    throw std::logic_error("unknown effect kind");
}

namespace {
// Swift's optional chaining: a missing effect takes nothing.
template <typename Effect> void paint(std::optional<Effect> &effect, const PaletteColor &color)
{
    if (!effect)
        return;
    effect->red = color.red;
    effect->green = color.green;
    effect->blue = color.blue;
}

template <typename Effect> void enable(std::optional<Effect> &effect, bool enabled)
{
    if (effect)
        effect->enabled = enabled;
}

template <typename Effect> std::optional<Effect> shown(const std::optional<Effect> &effect)
{
    return effect && effect->isEnabled() ? effect : std::nullopt;
}
}

void LayerEffects::setColor(const PaletteColor &color, LayerEffectKind kind)
{
    switch (kind) {
    case LayerEffectKind::stroke: paint(stroke, color); return;
    case LayerEffectKind::shadow: paint(shadow, color); return;
    case LayerEffectKind::colorOverlay: paint(colorOverlay, color); return;
    case LayerEffectKind::innerShadow: paint(innerShadow, color); return;
    case LayerEffectKind::outerGlow: paint(outerGlow, color); return;
    case LayerEffectKind::innerGlow: paint(innerGlow, color); return;
    }
    throw std::logic_error("unknown effect kind");
}

void LayerEffects::remove(LayerEffectKind kind)
{
    switch (kind) {
    case LayerEffectKind::stroke: stroke.reset(); return;
    case LayerEffectKind::shadow: shadow.reset(); return;
    case LayerEffectKind::colorOverlay: colorOverlay.reset(); return;
    case LayerEffectKind::innerShadow: innerShadow.reset(); return;
    case LayerEffectKind::outerGlow: outerGlow.reset(); return;
    case LayerEffectKind::innerGlow: innerGlow.reset(); return;
    }
    throw std::logic_error("unknown effect kind");
}

void LayerEffects::setEnabled(bool enabled, LayerEffectKind kind)
{
    switch (kind) {
    case LayerEffectKind::stroke: enable(stroke, enabled); return;
    case LayerEffectKind::shadow: enable(shadow, enabled); return;
    case LayerEffectKind::colorOverlay: enable(colorOverlay, enabled); return;
    case LayerEffectKind::innerShadow: enable(innerShadow, enabled); return;
    case LayerEffectKind::outerGlow: enable(outerGlow, enabled); return;
    case LayerEffectKind::innerGlow: enable(innerGlow, enabled); return;
    }
    throw std::logic_error("unknown effect kind");
}

LayerEffects LayerEffects::visible() const
{
    return LayerEffects{shown(stroke), shown(shadow), shown(colorOverlay), shown(innerShadow), shown(outerGlow), shown(innerGlow)};
}
