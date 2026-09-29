#pragma once
#include "Document/BrushStroke.h"
#include "Document/DocumentLimits.h"
#include "Rendering/RasterSnapshot.h"
#include <vector>

// A layer claiming its size in pixels, holding one.
inline ImportedImage claimed(int width, int height, const QString &name = QStringLiteral("Claimed"))
{
    const auto raster = std::make_shared<const RasterSnapshot>(width, height, BrushRaster::context(1, 1, false), QRectF(0, 0, width, height),
                                                               std::vector<BrushPatch>{});
    return ImportedImage(raster, QImage(), name);
}

// Layers claiming `pixels` in all, each within one surface.
inline std::vector<ImportedImage> claiming(qint64 pixels)
{
    std::vector<ImportedImage> layers;
    const int side = DocumentLimits::maxSide;
    while (pixels > 0) {
        const qint64 rows = std::min<qint64>(pixels / side, DocumentLimits::maxSurfacePixels / side);
        layers.push_back(rows > 0 ? claimed(side, int(rows)) : claimed(int(pixels), 1));
        pixels -= rows > 0 ? side * rows : pixels;
    }
    return layers;
}
