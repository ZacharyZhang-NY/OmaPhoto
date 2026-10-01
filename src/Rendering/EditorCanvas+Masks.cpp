#include "Rendering/EditorCanvas.h"
#include "IO/ImageExporter.h"
#include "Rendering/DownsampleCache.h"
#include "Rendering/LayerRenderer.h"
#include "Rendering/RasterSnapshot.h"
#include <QPainter>

// Swift's drawMaskAlone: gray across the canvas, edge tone past it.
void CanvasView::drawMaskAlone(const ImageLayer &layer, const CanvasDocument &document, double scale, const Center &center, QPainter &context)
{
    const LayerMask &mask = layer.mask.value();
    const int tone = qRound(LayerMask::background(mask.asset.thumbnail) * 255);
    context.fillRect(QRectF(center(QPointF(0, 0)), document.size() * scale), QColor(tone, tone, tone));
    const std::optional<GradientEdit> &gradient = m_session.gradientEdit();
    const BrushStroke *stroke = m_session.brushStroke() ? m_session.brushStroke() : gradient ? gradient->raster.get() : nullptr;
    // A stroke or gradient laid into it shows at once.
    if (stroke && stroke->isMask && stroke->layer.id == layer.id) {
        const LayerTransform &placed = stroke->paintTransform;
        const std::shared_ptr<const RasterSnapshot> &raster = mask.asset.raster;
        LayerRenderer::drawBrushPreview(raster ? QImage() : mask.asset.image(), placed, center(placed.center()), context, {.scale = scale},
                                        {.patches = stroke->patches(), .pixelWidth = stroke->width, .pixelHeight = stroke->height,
                                         .paintingMask = false, .sourceRect = stroke->sourceRect, .raster = raster});
        return;
    }
    // Where a transform drag shows it, as the composite.
    const LayerTransform placed = m_session.displayedMaskPlacement(layer).value_or(m_session.displayedTransform(layer));
    LayerRenderer::draw(mask.asset.image(), placed, center(placed.center()), context, {.scale = scale});
}

// The folder's live mask, multiplied into the coverage.
FolderMaskClip::Applier CanvasView::liveFolderMaskClip(const BrushStroke &edit, double scale, const Center &center) const
{
    return [&edit, scale, center](const QPainter &painter, QImage &coverage) {
        QImage live(coverage.size(), QImage::Format_Grayscale8);
        if (live.isNull())
            throw ExportError(ExportError::Kind::render);
        // Outside the mask's bounds stays hidden, as when committed.
        live.fill(0);
        LayerTransform transform = edit.paintTransform;
        transform.sampling = LayerSampling::nearest;
        const std::optional<ImportedImage> &base = edit.layer.mask ? std::optional(edit.layer.mask->asset) : std::nullopt;
        {
            QPainter drawing(&live);
            drawing.setTransform(painter.deviceTransform());
            // Swift's displayImage: halved near the size drawn.
            const double device = LayerRenderer::deviceScale(drawing);
            const auto shown = [device](const QImage &image, double width) {
                return DownsampleCache::shared().imageDrawnAt(image, width * device / std::max(1, image.width()));
            };
            const std::shared_ptr<const RasterSnapshot> raster = base ? base->raster : nullptr;
            const QImage image = base && !raster ? shown(base->image(), transform.size.width() * scale) : QImage();
            const QImage rasterBase = raster && !raster->base.isNull()
                ? shown(raster->base, transform.size.width() * scale * raster->baseRect.width() / std::max(1, raster->width))
                : QImage();
            LayerRenderer::drawBrushPreview(image, transform, center(transform.center()), drawing, {.scale = scale},
                                            {.patches = edit.patches(), .pixelWidth = edit.width, .pixelHeight = edit.height, .paintingMask = false,
                                             .sourceRect = edit.sourceRect, .raster = raster, .rasterBase = rasterBase});
        }
        QPainter multiplying(&coverage);
        if (!multiplying.isActive())
            throw ExportError(ExportError::Kind::render);
        multiplying.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        multiplying.drawImage(QRectF(coverage.rect()), BrushRaster::alphaView(live));
    };
}
