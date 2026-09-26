#pragma once
#include "Document/LayerAppearance.h"
#include <QImage>

// Hue, Saturation, Color, Luminosity: PDF blend modes QPainter lacks.
namespace HslBlend {
bool handles(LayerBlendMode mode);
// Source over backdrop, same size, both RGBA8888 premultiplied.
QImage blend(const QImage &backdrop, const QImage &source, LayerBlendMode mode, double opacity);
}
