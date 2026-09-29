#include "Document/LayerEffects+Renderer.h"
#include "Rendering/EditorCanvas.h"
#include "Rendering/RasterSnapshot.h"
#include "Rendering/TiledLayerRenderer.h"

// Swift's effects surface: a painted layer keeps its effects on.

// A stroke previews as the finished layer will look (TiledLayerRenderer).
void CanvasView::drawStroke(const BrushStroke &stroke, const ImageLayer &layer, const LayerTransform &transform, const std::optional<QImage> &mask,
                            const LayerRenderer::Options &options, double scale, const Center &center, QPainter &target)
{
    const std::optional<ImportedImage> &previous = stroke.layer.asset;
    const QImage image = previous && !previous->raster ? previous->image() : QImage();
    const std::shared_ptr<const RasterSnapshot> raster = previous ? previous->raster : nullptr;
    const LayerRenderer::Options bare{.scale = scale, .opacity = options.opacity, .blendMode = options.blendMode, .clip = options.clip};
    if (!stroke.isMask) {
        // The surface holds the wet pixels under their effects.
        if (LayerEffectsSurface *surface = strokeSurface(layer, stroke, mask); surface && surface->image()) {
            const LayerTransform grown = LayerEffectsRenderer::placed(transform, *surface->image(), surface->margin);
            surface->placement = grown;
            LayerRenderer::draw(*surface->image(), grown, center(grown.center()), target, bare);
            return;
        }
        if (const std::optional<EffectsPreviewCache::Result> effects = m_session.effectsPreviews.rendered(layer.id)) {
            const LayerTransform grown = effects->placement.value_or(LayerEffectsRenderer::placed(layer.transform, effects->image, effects->inset));
            LayerRenderer::draw(effects->image, grown, center(grown.center()), target, bare);
        }
        TiledLayerRenderer::drawStroke(stroke.width, stroke.height, stroke.sourceRect, stroke.patches(), image, raster, transform, center(transform.center()),
                                       target, options);
        return;
    }
    // With effects on, a surface redoes them per mask change.
    const auto drawSurface = [&](LayerEffectsSurface *surface) {
        if (!surface || !surface->image())
            return false;
        const LayerTransform grown = LayerEffectsRenderer::placed(transform, *surface->image(), surface->margin);
        surface->placement = grown;
        LayerRenderer::draw(*surface->image(), grown, center(grown.center()), target, bare);
        return true;
    };
    if (stroke.layer.mask && stroke.layer.mask->placement) {
        // A placed mask: the layer draws through its own grid.
        if (!layer.asset)
            return;
        const std::optional<QImage> preview = stroke.placedMaskPreview(stroke.paintTransform);
        if (preview && drawSurface(placedMaskSurface(layer, stroke, *preview)))
            return;
        LayerRenderer::Options through = options;
        through.mask = preview.value_or(QImage());
        if (layer.asset->raster)
            TiledLayerRenderer::drawRaster(layer.asset->raster, transform, center(transform.center()), target, through);
        else
            LayerRenderer::draw(layer.asset->image(), transform, center(transform.center()), target, through);
        return;
    }
    if (drawSurface(strokeSurface(layer, stroke, std::nullopt)))
        return;
    TiledLayerRenderer::drawMaskStroke(stroke.width, stroke.height, stroke.sourceRect, stroke.patches(),
                                       stroke.layer.mask ? std::optional(stroke.layer.mask->asset) : std::nullopt, image, raster, transform,
                                       center(transform.center()), target, bare);
}

// Made as the stroke starts, updated as it goes.
LayerEffectsSurface *CanvasView::strokeSurface(const ImageLayer &layer, const BrushStroke &stroke, const std::optional<QImage> &mask)
{
    const LayerEffects effects = layer.effects.value_or(LayerEffects()).visible();
    if (effects.isEmpty() || !effects.isValid())
        return nullptr;
    const QSizeF grid(stroke.width, stroke.height);
    if (!m_strokeSurface || !m_strokeSurface->matches(layer.id, effects, grid, stroke.sourceRect))
        m_strokeSurface = LayerEffectsSurface::make(layer.id, effects, grid, stroke.sourceRect);
    if (!m_strokeSurface)
        return nullptr;
    if (!stroke.isMask) {
        m_strokeSurface->update(stroke.layer.asset, stroke.patches(), mask);
        return m_strokeSurface.get();
    }
    // The old mask, then the stroke's tiles over it.
    const std::optional<QImage> old = stroke.layer.mask ? std::optional(stroke.layer.mask->asset.image()) : std::nullopt;
    const std::vector<BrushPatch> patches = stroke.patches();
    const QRectF sourceRect = stroke.sourceRect;
    m_strokeSurface->update(stroke.layer.asset, {}, std::nullopt, LayerEffectsSurface::MaskStroke{patches, QTransform(), [old, patches, sourceRect](const QRectF &region) {
        QImage coverage = BrushRaster::context(int(region.width()), int(region.height()), true);
        QPainter painter(&coverage);
        painter.translate(-region.left(), -region.top());
        if (old)
            BrushRaster::draw(*old, sourceRect, painter);
        for (const BrushPatch &patch : patches) {
            if (patch.rect.intersects(region))
                BrushRaster::draw(patch.image, patch.rect, painter);
        }
        return coverage;
    }});
    return m_strokeSurface.get();
}

// A placed mask paints its grid; the surface, the layer's.
LayerEffectsSurface *CanvasView::placedMaskSurface(const ImageLayer &layer, const BrushStroke &stroke, const QImage &preview)
{
    const LayerEffects effects = layer.effects.value_or(LayerEffects()).visible();
    if (effects.isEmpty() || !effects.isValid() || !stroke.layer.asset)
        return nullptr;
    const QSize base = stroke.layer.asset->size();
    const QRectF full(QPointF(0, 0), QSizeF(base));
    if (!m_strokeSurface || !m_strokeSurface->matches(layer.id, effects, full.size(), full))
        m_strokeSurface = LayerEffectsSurface::make(layer.id, effects, full.size(), full);
    if (!m_strokeSurface)
        return nullptr;
    const QTransform toGrid = BrushRaster::pixelToDocument(stroke.paintTransform, stroke.width, stroke.height)
        * BrushRaster::pixelToDocument(stroke.layer.transform, base.width(), base.height()).inverted();
    m_strokeSurface->update(stroke.layer.asset, {}, std::nullopt, LayerEffectsSurface::MaskStroke{stroke.patches(), toGrid, [preview, full](const QRectF &region) {
        QImage coverage = BrushRaster::context(int(region.width()), int(region.height()), true);
        QPainter painter(&coverage);
        painter.translate(-region.left(), -region.top());
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(full, preview);
        return coverage;
    }});
    return m_strokeSurface.get();
}

// The surface shows until the new effects land: nothing blinks.
void CanvasView::handOnStrokeSurface()
{
    if (m_session.brushStroke() || !m_strokeSurface)
        return;
    if (m_strokeSurface->image() && m_strokeSurface->placement)
        m_session.effectsPreviews.seed(m_strokeSurface->layerID, *m_strokeSurface->image(), *m_strokeSurface->placement);
    m_strokeSurface.reset();
}
