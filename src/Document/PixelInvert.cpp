#include "Document/PixelInvert.h"
#include "Document/BrushStroke.h"
#include "Document/PixelAdjust.h"

QImage PixelInvert::run(const Job &job)
{
    const QImage original = job.image.convertToFormat(job.isMask ? QImage::Format_Grayscale8 : QImage::Format_RGBA8888_Premultiplied);
    QImage inverted = BrushRaster::context(original.width(), original.height(), job.isMask);
    for (int y = 0; y < original.height(); ++y) {
        const uchar *source = original.constScanLine(y);
        uchar *target = inverted.scanLine(y);
        if (job.isMask) {
            for (int x = 0; x < original.width(); ++x)
                target[x] = uchar(255 - source[x]);
            continue;
        }
        // Premultiplied: alpha − colour keeps transparency.
        for (int x = 0; x < original.width() * 4; x += 4) {
            const uchar alpha = source[x + 3];
            target[x] = uchar(alpha - source[x]);
            target[x + 1] = uchar(alpha - source[x + 1]);
            target[x + 2] = uchar(alpha - source[x + 2]);
            target[x + 3] = alpha;
        }
    }
    if (!job.selection)
        return inverted;
    return PixelAdjust::blend(inverted, original, *job.selection, job.pixelToDocument, job.isMask);
}
