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
    if (stroke.layer.mask && stroke.layer.mask->placement) {
        // A placed mask: the layer draws through its own grid.
        if (!layer.asset)
            return;
        LayerRenderer::Options through = options;
        through.mask = stroke.placedMaskPreview(*stroke.layer.mask->placement).value_or(QImage());
        if (layer.asset->raster)
            TiledLayerRenderer::drawRaster(layer.asset->raster, transform, center(transform.center()), target, through);
        else
            LayerRenderer::draw(layer.asset->image(), transform, center(transform.center()), target, through);
        return;
    }
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
    m_strokeSurface->update(stroke.layer.asset, stroke.patches(), mask);
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
