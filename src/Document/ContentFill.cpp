#include "Document/ContentFill.h"
#include "Document/BrushStroke.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"

// The kernel shares this file's name: angle brackets skip here.
extern "C" {
#include <ContentFill.h>
}

ContentFillError::ContentFillError(Kind kind)
    : std::runtime_error("Not enough unselected, opaque image pixels to synthesize a fill. Use a smaller selection with some surrounding image."), kind(kind)
{
}

QImage ContentFill::run(const FilterJob &job)
{
    if (!job.selection)
        throw ContentFillError(ContentFillError::Kind::noSource);
    const int width = job.image.width(), height = job.image.height();
    QImage pixels = BrushRaster::context(width, height, false);
    QPainter painter(&pixels);
    BrushRaster::draw(job.image, QRectF(0, 0, width, height), painter);
    painter.end();
    // White through the selection: the pixels to make.
    const QImage mask = PixelAdjust::coverage(*job.selection, width, height, job.mapping);
    const int result = content_fill(pixels.bits(), size_t(pixels.bytesPerLine()), mask.constBits(), size_t(mask.bytesPerLine()), width, height);
    if (result == 0)
        throw ContentFillError(ContentFillError::Kind::noSource);
    if (result != 1)
        throw ExportError(ExportError::Kind::render);
    return pixels;
}
