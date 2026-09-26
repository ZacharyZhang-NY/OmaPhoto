#pragma once
#include "Document/BrushStroke.h"
#include "Document/Selection.h"
#include <memory>

// Selected pixels being dragged: the lifted raster and the outline.
class PixelMove {
public:
    PixelMove(std::shared_ptr<BrushStroke> raster, DocumentSelection origin, bool duplicate);
    // The outline shifted by the drag so far.
    DocumentSelection movedSelection() const;

    // Shared: the canvas draws it while the commit runs.
    const std::shared_ptr<BrushStroke> raster;
    const DocumentSelection origin;
    const bool duplicate;
    QSizeF offset{0, 0};
};
