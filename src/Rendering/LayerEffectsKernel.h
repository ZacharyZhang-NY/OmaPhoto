#pragma once
#include "Document/LayerEffects.h"
#include <QImage>

// Swift's MetalLayerEffects: its passes on the CPU, in float.
namespace LayerEffectsKernel {
// The shown layer with room round it; same size back.
QImage render(const QImage &pixels, const LayerEffects &effects);
}
