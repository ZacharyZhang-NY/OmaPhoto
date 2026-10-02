#include "Document/EditorSession.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include <cmath>

namespace {
// `part` padded by `margin`: clear outside, or edges repeated.
QImage padded(const QImage &image, const QRect &part, int margin, bool repeat)
{
    const QRect around = part.adjusted(-margin, -margin, margin, margin);
    if (!repeat) {
        const QImage copy = image.copy(around);
        if (copy.isNull())
            throw ExportError(ExportError::Kind::render);
        return copy;
    }
    QImage copy = BrushRaster::context(around.width(), around.height(), true);
    for (int y = 0; y < around.height(); ++y) {
        const uchar *row = image.constScanLine(std::clamp(around.top() + y, 0, image.height() - 1));
        uchar *out = copy.scanLine(y);
        for (int x = 0; x < around.width(); ++x)
            out[x] = row[std::clamp(around.left() + x, 0, image.width() - 1)];
    }
    return copy;
}

// A piece blurred alone equals that part of the whole.
std::function<QImage(const QRect &)> softened(const QImage &sharp, double sigma, bool mask)
{
    return [sharp, sigma, mask](const QRect &part) {
        // The pad holds the edges; the blur's rule never reaches.
        const int margin = int(std::ceil(3 * sigma));
        const QImage piece = PixelAdjust::gaussianBlur(padded(sharp, part, margin, mask), sigma, false).copy(margin, margin, part.width(), part.height());
        if (piece.isNull())
            throw ExportError(ExportError::Kind::render);
        return piece;
    };
}
}

// Swift's BlurTool extension: the layer's pixels, or mask, softened.
std::optional<BrushStroke::Clone> EditorSession::blurSample(const BrushStroke &stroke) const
{
    const ImageLayer &layer = stroke.layer;
    const std::optional<ImportedImage> &asset = stroke.isMask ? (layer.mask ? std::optional(layer.mask->asset) : std::nullopt) : layer.asset;
    if (!asset)
        return std::nullopt;
    // The bar's Radius, carried into the layer's pixels.
    const QTransform &map = stroke.pixelToDocument;
    const double perPixel = std::max(1e-6, std::sqrt(std::abs(map.m11() * map.m22() - map.m12() * map.m21())));
    const double sidePixels = std::max(stroke.sourceRect.width(), stroke.sourceRect.height());
    const double sigma = std::min(std::min(50.0, std::max(0.5, m_brushSettings.blurRadius)) / perPixel, sidePixels / 2);
    // Room for the blur to spread past the pixels' edges.
    const double margin = std::ceil(3 * sigma);
    const QRectF region = stroke.sourceRect.adjusted(-margin, -margin, margin, margin);
    // A huge layer's sample is made coarser, within several canvases.
    const qint64 canvas = m_document ? qint64(m_document->width) * m_document->height : 0;
    const double budget = double(std::min(DocumentLimits::maxSurfacePixels, std::max<qint64>(16'000'000, 4 * canvas)));
    const double fit = std::min(1.0, std::sqrt(budget / (region.width() * region.height())));
    const int width = std::max(1, int(std::ceil(region.width() * fit))), height = std::max(1, int(std::ceil(region.height() * fit)));
    const QRectF placed(margin * fit, margin * fit, stroke.sourceRect.width() * fit, stroke.sourceRect.height() * fit);
    try {
        QImage context = BrushRaster::context(width, height, stroke.isMask);
        // Past its pixels a mask keeps its edge tone.
        if (stroke.isMask && layer.mask)
            context.fill(int(std::round(LayerMask::background(layer.mask->asset.thumbnail) * 255)));
        QPainter painter(&context);
        // A painted asset flattens here; the catch covers it too.
        BrushRaster::draw(asset->image(), placed, painter);
        painter.end();
        // Sharp; only the parts the brush reaches are softened.
        return BrushStroke::Clone{context, region, true, softened(context, sigma * fit, stroke.isMask)};
    } catch (const ExportError &error) {
        qCWarning(lcApp) << "the Blur tool cannot sample the layer:" << error.what();
        return std::nullopt;
    }
}
