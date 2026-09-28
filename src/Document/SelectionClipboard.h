#pragma once
#include <QImage>
#include <QMimeData>
#include <QPointer>
#include <QPointF>
#include <QRectF>
#include <QUuid>
#include <vector>

// Pixels copied from the canvas, with where they came from.
struct PixelClipboard {
    QImage image;
    QPointF origin;
    // What we put there; another object means another copy.
    QPointer<QMimeData> data;
};

// Swift's (image, region) tuple: pixels and their canvas bounds.
struct CopiedPixels {
    QImage image;
    QRectF region;
};

// Layers Copy took whole: Paste brings them back complete.
struct CopiedLayer {
    // Document order; a copied folder's contents come with it.
    std::vector<QUuid> ids;
    // As PixelClipboard's: another copy since replaces it.
    QPointer<QMimeData> data;
};
