#pragma once
#include "Document/LayerEffects.h"
#include "Document/LayerTransform.h"
#include <QImage>
#include <optional>

// Swift's LayerEffectsRenderer: a layer with its effects round it.
namespace LayerEffectsRenderer {
// Swift's tuple: the grown image, the room round the pixels.
struct Rendered {
    QImage image;
    double inset;
};
// Reused for the same pixels, mask and effects; eight kept.
std::optional<Rendered> cached(const QImage &image, const std::optional<QImage> &mask, const std::optional<LayerEffects> &effects);
// The transform grown by the margin, the image in place.
LayerTransform placed(const LayerTransform &transform, const QImage &image, double inset);
double margin(const LayerEffects &effects);
// `mask` hides pixels first, so effects follow what shows.
Rendered render(const QImage &image, const std::optional<QImage> &mask, const LayerEffects &effects);
}
