#pragma once
#include <QPainter>
#include <functional>

// Draws through an image surface over what the painter shows.
namespace AdjustmentSurface {
inline constexpr qint64 pixelBudget = 100'000'000;
void draw(QPainter &context, const std::function<void(QPainter &)> &body, qint64 pixelBudget = AdjustmentSurface::pixelBudget);
}
