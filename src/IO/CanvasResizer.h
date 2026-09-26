#pragma once
#include "Document/CanvasSize.h"
#include "IO/ProjectStore.h"

namespace CanvasResizer {
// Moves the layers; a fill becomes a bottom layer.
ProjectSnapshot resize(const ProjectSnapshot &snapshot, const CanvasSizeOptions &options);
}
