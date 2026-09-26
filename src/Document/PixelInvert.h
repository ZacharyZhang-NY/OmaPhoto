#pragma once
#include "Document/Selection.h"
#include <QImage>
#include <QTransform>
#include <optional>

// A whole-image invert in one pass, limited to a selection.
namespace PixelInvert {
struct Job {
    QImage image;
    bool isMask;
    // Maps the image's top-left pixel grid to document pixels.
    QTransform pixelToDocument;
    std::optional<SelectionClip> selection;
};

QImage run(const Job &job);
}
