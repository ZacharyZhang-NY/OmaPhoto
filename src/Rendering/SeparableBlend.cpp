#include "Rendering/SeparableBlend.h"
#include <algorithm>
#include <stdexcept>

namespace {
// A zero divisor gives infinity; the clamp takes it home.
float burn(float backdrop, float source)
{
    if (backdrop >= 1)
        return 1;
    return 1 - std::min(1.0f, (1 - backdrop) / source);
}

float dodge(float backdrop, float source)
{
    if (backdrop <= 0)
        return 0;
    return std::min(1.0f, backdrop / (1 - source));
}
}

bool SeparableBlend::needsSurface(LayerBlendMode mode)
{
    switch (mode) {
    case LayerBlendMode::linearBurn:
    case LayerBlendMode::linearDodge:
    case LayerBlendMode::vividLight:
    case LayerBlendMode::linearLight:
    case LayerBlendMode::pinLight:
    case LayerBlendMode::hardMix:
    case LayerBlendMode::subtract:
    case LayerBlendMode::divide:
        return true;
    default:
        return false;
    }
}

float SeparableBlend::channel(LayerBlendMode mode, float backdrop, float source)
{
    switch (mode) {
    case LayerBlendMode::linearBurn:
        return std::max(0.0f, backdrop + source - 1);
    case LayerBlendMode::linearDodge:
        return std::min(1.0f, backdrop + source);
    case LayerBlendMode::vividLight:
        return source <= 0.5f ? burn(backdrop, 2 * source) : dodge(backdrop, 2 * source - 1);
    case LayerBlendMode::linearLight:
        return std::clamp(backdrop + 2 * source - 1, 0.0f, 1.0f);
    case LayerBlendMode::pinLight:
        return source <= 0.5f ? std::min(backdrop, 2 * source) : std::max(backdrop, 2 * source - 1);
    case LayerBlendMode::hardMix:
        return backdrop + source >= 1 ? 1 : 0;
    case LayerBlendMode::subtract:
        return std::max(0.0f, backdrop - source);
    case LayerBlendMode::divide:
        return source <= 0 ? 1 : std::min(1.0f, backdrop / source);
    default:
        throw std::logic_error("SeparableBlend blends only the modes QPainter lacks");
    }
}
