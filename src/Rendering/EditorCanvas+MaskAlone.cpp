#include "Rendering/EditorCanvas.h"

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
