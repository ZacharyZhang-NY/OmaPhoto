#include "Document/ImageTrim.h"
#include "Document/CanvasSize.h"
#include "IO/CanvasResizer.h"
#include "IO/ImageExporter.h"
#include <cstdlib>

extern "C" {
#include "BrushPixels.h"
}

QString rawValue(TrimBasedOn basedOn)
{
    switch (basedOn) {
    case TrimBasedOn::transparentPixels: return QStringLiteral("Transparent Pixels");
    case TrimBasedOn::topLeftPixelColor: return QStringLiteral("Top Left Pixel Color");
    case TrimBasedOn::bottomRightPixelColor: return QStringLiteral("Bottom Right Pixel Color");
    }
    throw std::logic_error("unknown trim basis");
}

bool TrimOptions::trimsAny() const
{
    return top || bottom || left || right;
}

namespace {
// Unchecked edges stay at the image's; content is never empty.
QRect kept(int left, int top, int right, int bottom, int width, int height, const TrimOptions &options)
{
    const int minX = options.left ? left : 0, minY = options.top ? top : 0;
    const int maxX = options.right ? right : width, maxY = options.bottom ? bottom : height;
    return QRect(minX, minY, maxX - minX, maxY - minY);
}

// Swift's calculateColorTrimRect: rows scanned in from both ends.
std::optional<QRect> colorTrimRect(const QImage &pixels, QPoint sample, const TrimOptions &options)
{
    const int width = pixels.width(), height = pixels.height(), tolerance = options.tolerance;
    const uchar *target = pixels.constScanLine(sample.y()) + sample.x() * 4;
    const auto matches = [&](const uchar *row, int x) {
        const uchar *pixel = row + x * 4;
        for (int channel = 0; channel < 4; ++channel) {
            if (std::abs(pixel[channel] - target[channel]) > tolerance)
                return false;
        }
        return true;
    };
    int left = width, right = 0, top = height, bottom = 0;
    for (int y = 0; y < height; ++y) {
        const uchar *row = pixels.constScanLine(y);
        int first = 0;
        while (first < width && matches(row, first))
            ++first;
        if (first == width)
            continue;
        // The unmatched pixel at `first` stops this scan.
        int last = width;
        while (matches(row, last - 1))
            --last;
        left = std::min(left, first);
        right = std::max(right, last);
        top = std::min(top, y);
        bottom = y + 1;
    }
    // Every pixel matched the sample.
    if (right == 0)
        return std::nullopt;
    return kept(left, top, right, bottom, width, height, options);
}
}

std::optional<QRect> ImageTrim::calculateTrimRect(const QImage &image, const TrimOptions &options)
{
    if (!options.trimsAny() || image.isNull())
        return std::nullopt;
    // Swift's try? returns nil; running out here is explained instead.
    const QImage pixels = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    if (pixels.isNull())
        throw ExportError(ExportError::Kind::render);
    const int width = pixels.width(), height = pixels.height();
    switch (options.basedOn) {
    case TrimBasedOn::transparentPixels: {
        size_t edges[4] = {0, 0, 0, 0};
        brush_alpha_bounds(pixels.constBits(), size_t(width), size_t(height), size_t(pixels.bytesPerLine()), edges);
        // A right edge of zero: every pixel is clear.
        if (edges[2] == 0)
            return std::nullopt;
        return kept(int(edges[0]), int(edges[1]), int(edges[2]), int(edges[3]), width, height, options);
    }
    case TrimBasedOn::topLeftPixelColor: return colorTrimRect(pixels, QPoint(0, 0), options);
    case TrimBasedOn::bottomRightPixelColor: return colorTrimRect(pixels, QPoint(width - 1, height - 1), options);
    }
    throw std::logic_error("unknown trim basis");
}

std::optional<ProjectSnapshot> ImageTrim::trim(const ProjectSnapshot &snapshot, const TrimOptions &options)
{
    const std::optional<QRect> rect = calculateTrimRect(ImageExporter::render(snapshot).image, options);
    if (!rect)
        return std::nullopt;
    if (*rect == QRect(0, 0, int(snapshot.manifest.width), int(snapshot.manifest.height)))
        return snapshot;
    return CanvasResizer::resize(snapshot, CanvasSizeOptions{.width = rect->width(), .height = rect->height(),
                                                             .contentOffset = QPointF(-rect->left(), -rect->top())});
}
