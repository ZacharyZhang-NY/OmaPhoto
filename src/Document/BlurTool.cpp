#include "Document/EditorSession.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include <cmath>

// Swift's BlurTool extension: the layer's pixels, or mask, softened.
std::optional<BrushStroke::Clone> EditorSession::blurSample(const BrushStroke &stroke) const
{
    const ImageLayer &layer = stroke.layer;
    const std::optional<ImportedImage> &asset = stroke.isMask ? (layer.mask ? std::optional(layer.mask->asset) : std::nullopt) : layer.asset;
    if (!asset)
        return std::nullopt;
    // The canvas's softening carried into the layer's pixels.
    const QTransform &map = stroke.pixelToDocument;
    const double perPixel = std::max(1e-6, std::sqrt(std::abs(map.m11() * map.m22() - map.m12() * map.m21())));
    const double sidePixels = std::max(stroke.sourceRect.width(), stroke.sourceRect.height());
    const double sigma = std::min(std::min(30.0, std::max(1.5, m_brushSettings.diameter / 10)) / perPixel, sidePixels / 2);
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
        return BrushStroke::Clone{PixelAdjust::gaussianBlur(context, sigma * fit, stroke.isMask), region, true};
    } catch (const ExportError &error) {
        qCWarning(lcApp) << "the Blur tool cannot sample the layer:" << error.what();
        return std::nullopt;
    }
}
