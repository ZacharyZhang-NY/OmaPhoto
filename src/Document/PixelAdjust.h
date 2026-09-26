#pragma once
#include "Document/Selection.h"
#include <QImage>
#include <QTransform>

// Shared plumbing for whole-image adjustments.
namespace PixelAdjust {
// A small preview for the Layers panel, like an import's.
QImage thumbnail(const QImage &image);
// Selection coverage on the image's own pixel grid.
QImage coverage(const SelectionClip &selection, int width, int height, const QTransform &pixelToDocument);
// coverage × adjusted + (1 − coverage) × original.
QImage blend(const QImage &adjusted, const QImage &original, const SelectionClip &selection,
             const QTransform &pixelToDocument, bool isMask);
// Core Image's Gaussian on stored values; `clamped` repeats the edge.
QImage gaussianBlur(const QImage &image, double sigma, bool clamped);
// Core Image's motion blur: a Gaussian taper along the angle.
QImage motionBlur(const QImage &image, double radius, double angle);
}
