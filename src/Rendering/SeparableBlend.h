#pragma once
#include "Document/LayerAppearance.h"

// Swift's SeparableBlend: channel-by-channel modes QPainter lacks or differs on.
namespace SeparableBlend {
// Blended by hand against a surface, Swift's `needsSurface`.
bool needsSurface(LayerBlendMode mode);
// Photoshop's formula on one channel: backdrop, source, 0 to 1.
float channel(LayerBlendMode mode, float backdrop, float source);
}
