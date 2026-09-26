#pragma once
#include "Document/LayerTransform.h"
#include "IO/ProjectStore.h"

struct ImageSizeOptions {
    qint64 width;
    qint64 height;
    double resolution;
    LayerSampling sampling = LayerSampling::high;
};

namespace ImageResizer {
// Scales the document; every layer is drawn again upright.
ProjectSnapshot resize(const ProjectSnapshot &snapshot, const ImageSizeOptions &options);
}
