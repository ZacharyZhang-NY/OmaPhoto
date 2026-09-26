#pragma once
#include <QImage>
#include <QPointF>
#include <QRectF>

// Pixels copied from the canvas, with where they came from.
struct PixelClipboard {
    QImage image;
    QPointF origin;
    // The clipboard's changes when written; more means another copy since.
    int changeCount;
};

// Swift's (image, region) tuple: pixels and their canvas bounds.
struct CopiedPixels {
    QImage image;
    QRectF region;
};
