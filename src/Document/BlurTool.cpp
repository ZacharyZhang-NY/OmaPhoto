#include "Document/EditorSession.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include "Rendering/LayerRenderer.h"
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
        const int margin = int(std::ceil(3 * sigma));
        const QImage piece = PixelAdjust::gaussianBlur(padded(sharp, part, margin, mask), sigma, mask).copy(margin, margin, part.width(), part.height());
        if (piece.isNull())
            throw ExportError(ExportError::Kind::render);
        return piece;
    };
}
}

// Swift's BlurTool extension: the layer, or its mask, softened.
std::optional<BrushStroke::Clone> EditorSession::blurSample(const CanvasDocument &document, bool mask) const
{
    const std::optional<ImageLayer> layer = activeLayer();
    if (!layer)
        return std::nullopt;
    // The bar's Radius, whatever the brush's size.
    const double sigma = std::min(50.0, std::max(0.5, m_brushSettings.blurRadius));
    try {
        if (mask) {
            if (!layer->mask)
                return std::nullopt;
            QImage context = BrushRaster::context(document.width, document.height, true);
            // Past its pixels a mask keeps its edge tone.
            const int tone = int(std::round(LayerMask::background(layer->mask->asset.thumbnail) * 255));
            context.fill(QColor(tone, tone, tone));
            QPainter painter(&context);
            // Coverage only adds white: the mask's area starts black.
            const LayerTransform placement = layer->maskTransform();
            painter.save();
            QTransform box;
            box.translate(placement.center().x(), placement.center().y());
            box.rotateRadians(placement.radians());
            painter.setTransform(box, true);
            painter.fillRect(QRectF(-placement.size.width() / 2, -placement.size.height() / 2, placement.size.width(), placement.size.height()),
                             Qt::black);
            painter.restore();
            LayerRenderer::drawCoverage(layer->mask->asset.image(), placement, painter);
            painter.end();
            return BrushStroke::Clone{context, QSizeF(0, 0), softened(context, sigma, true)};
        }
        if (!layer->asset)
            return std::nullopt;
        QImage context = BrushRaster::context(document.width, document.height, false);
        QPainter painter(&context);
        // A painted asset flattens here; the catch covers it too.
        const LayerTransform transform = displayedTransform(*layer);
        LayerRenderer::draw(layer->asset->image(), transform, transform.center(), painter, {});
        painter.end();
        return BrushStroke::Clone{context, QSizeF(0, 0), softened(context, sigma, false)};
    } catch (const ExportError &error) {
        qCWarning(lcApp) << "the Blur tool cannot sample the layer:" << error.what();
        return std::nullopt;
    }
}
