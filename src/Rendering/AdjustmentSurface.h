#pragma once
#include "Document/DocumentLimits.h"
#include <QPainter>
#include <functional>

// Draws through an image surface over what the painter shows.
namespace AdjustmentSurface {
inline constexpr qint64 pixelBudget = DocumentLimits::maxSurfacePixels;
// Padding, in painter units, is drawn past what shows.
void draw(QPainter &context, const std::function<void(QPainter &)> &body, double padding = 0,
          qint64 pixelBudget = AdjustmentSurface::pixelBudget);
}
