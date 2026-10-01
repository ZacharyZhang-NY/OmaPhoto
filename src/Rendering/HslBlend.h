#pragma once
#include "Document/LayerAppearance.h"
#include <QImage>

// Modes QPainter lacks: four non-separable, SeparableBlend's nine.
namespace HslBlend {
bool handles(LayerBlendMode mode);
// Source over backdrop, same size, both RGBA8888 premultiplied.
QImage blend(const QImage &backdrop, const QImage &source, LayerBlendMode mode, double opacity);
}
