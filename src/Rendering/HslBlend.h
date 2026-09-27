#pragma once
#include "Document/LayerAppearance.h"
#include <QImage>

// Blend modes QPainter lacks: the four non-separable, SeparableBlend's eight.
namespace HslBlend {
bool handles(LayerBlendMode mode);
// Source over backdrop, same size, both RGBA8888 premultiplied.
QImage blend(const QImage &backdrop, const QImage &source, LayerBlendMode mode, double opacity);
}
