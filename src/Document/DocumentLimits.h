#pragma once
#include <QString>
#include <QtGlobal>

// Swift's DocumentLimits: one surface, and a whole document's raster.
namespace DocumentLimits {
// Longest side of any canvas, layer, mask or generated surface.
inline constexpr int maxSide = 30'000;
// Largest single surface; both ceilings stay under maxSide squared.
inline constexpr qint64 maxSurfacePixels = 200'000'000;
// All layers and masks: a quarter of memory, capped.
qint64 documentPixelBudget();
// The budget for this much memory, in bytes.
qint64 budgetFor(qint64 memory);
qint64 maxSurfaceMegapixels();
qint64 documentBudgetMegapixels();
// The side limit as Swift's `formatted()` prints it: 30,000.
QString maxSideText();
}
