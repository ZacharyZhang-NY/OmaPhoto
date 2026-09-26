#pragma once
#include "Document/LayerTransform.h"
#include <QImage>
#include <QPixmap>
#include <QSizeF>

// Thumbnails framed by the whole canvas, as Photoshop shows them.
namespace CanvasThumbnail {
// Pixels per point in the pictures, sharp on scaled displays.
inline constexpr double backingScale = 2;

// The canvas's shape fitted in a square of `box` points.
QSize fittedSize(QSizeF canvas, double box);
// The checkerboard with the pixels placed; null: empty canvas.
QPixmap layer(const QImage &image, const LayerTransform &transform, QSizeF canvas, double box);
// The mask placed; its edge tone fills the rest.
QPixmap mask(const QImage &image, const LayerTransform &transform, QSizeF canvas, double box);
// The mean gray, 0 to 1, of the outermost pixels.
double edgeTone(const QImage &image);
}
