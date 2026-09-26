#pragma once
#include "Document/EditorSession+Model.h"
#include "Document/LayerTransform.h"
#include <QImage>

// Ctrl+T with a selection: the pixels float, then merge back.
struct FloatingTransform {
    QUuid sourceID;
    CanvasDocument before;
    std::optional<QUuid> beforeActive;
    LayerTransform original;
    QSizeF pixelSize;
};

// Floating pixels drawn back onto their layer, grown as needed.
namespace FloatingMerge {
struct Merged {
    ImportedImage asset;
    LayerTransform transform;
    std::optional<LayerMask> mask;
};
Merged merge(const QImage &pixels, const LayerTransform &transform, const ImageLayer &source);
}
