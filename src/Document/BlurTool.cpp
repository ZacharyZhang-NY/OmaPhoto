#include "Document/EditorSession.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include "Rendering/LayerRenderer.h"
#include <cmath>

// Swift's BlurTool extension: the layer, or its mask, softened.
std::optional<QImage> EditorSession::blurSample(const CanvasDocument &document, bool mask) const
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
            return PixelAdjust::gaussianBlur(context, sigma, true);
        }
        if (!layer->asset)
            return std::nullopt;
        QImage context = BrushRaster::context(document.width, document.height, false);
        QPainter painter(&context);
        // A painted asset flattens here; the catch covers it too.
        const LayerTransform transform = displayedTransform(*layer);
        LayerRenderer::draw(layer->asset->image(), transform, transform.center(), painter, {});
        painter.end();
        return PixelAdjust::gaussianBlur(context, sigma, false);
    } catch (const ExportError &error) {
        qCWarning(lcApp) << "the Blur tool cannot sample the layer:" << error.what();
        return std::nullopt;
    }
}
