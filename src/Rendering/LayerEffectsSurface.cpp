#include "Rendering/LayerEffectsSurface.h"
#include "Document/LayerEffects+Renderer.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include "Rendering/LayerEffectsKernel.h"
#include "Rendering/RasterSnapshot.h"
#include <QPainter>
#include <cmath>

namespace {
QRectF integral(const QRectF &rect)
{
    return QRectF(QPointF(std::floor(rect.left()), std::floor(rect.top())), QPointF(std::ceil(rect.right()), std::ceil(rect.bottom())));
}
}

LayerEffectsSurface::LayerEffectsSurface(QUuid layerID, const LayerEffects &effects, QSizeF grid, QRectF sourceRect, double margin, QImage context)
    : layerID(layerID), grid(grid), sourceRect(sourceRect), margin(margin), m_effects(effects), m_context(std::move(context))
{
}

std::unique_ptr<LayerEffectsSurface> LayerEffectsSurface::make(QUuid layerID, const LayerEffects &effects, QSizeF grid, QRectF sourceRect)
{
    const double margin = LayerEffectsRenderer::margin(effects);
    const qint64 width = qint64(grid.width() + margin * 2), height = qint64(grid.height() + margin * 2);
    if (width * height > 80'000'000) {
        qCWarning(lcRendering) << "an effects surface passes its pixel budget:" << width << "x" << height;
        return nullptr;
    }
    try {
        QImage context = BrushRaster::context(int(width), int(height), false);
        return std::unique_ptr<LayerEffectsSurface>(new LayerEffectsSurface(layerID, effects, grid, sourceRect, margin, std::move(context)));
    } catch (const ExportError &error) {
        qCWarning(lcRendering) << "an effects surface could not be allocated:" << error.what();
    }
    return nullptr;
}

bool LayerEffectsSurface::matches(QUuid layerID, const LayerEffects &effects, QSizeF grid, QRectF sourceRect) const
{
    return this->layerID == layerID && m_effects == effects && this->grid == grid && this->sourceRect == sourceRect;
}

// How far a change reaches: all within may need redoing.
double LayerEffectsSurface::reach() const
{
    double reach = 1;
    if (m_effects.stroke)
        reach = std::max(reach, m_effects.stroke->size + 2);
    if (m_effects.shadow)
        reach = std::max(reach, m_effects.shadow->distance + m_effects.shadow->blur * 3 + 2);
    // Swift forgets the inner shadow, whose blur reaches as far.
    if (m_effects.innerShadow)
        reach = std::max(reach, m_effects.innerShadow->distance + m_effects.innerShadow->blur * 3 + 2);
    if (m_effects.outerGlow)
        reach = std::max(reach, m_effects.outerGlow->size * 3 + 2);
    if (m_effects.innerGlow)
        reach = std::max(reach, m_effects.innerGlow->size * 3 + 2);
    return reach;
}

void LayerEffectsSurface::update(const std::optional<ImportedImage> &base, const std::vector<BrushPatch> &patches, const std::optional<QImage> &mask,
                                 std::optional<MaskStroke> maskStroke)
{
    m_maskStroke = std::move(maskStroke);
    std::optional<QRectF> dirty;
    std::map<std::pair<qint64, qint64>, qint64> seen;
    for (const BrushPatch &patch : m_maskStroke ? m_maskStroke->patches : patches) {
        const std::pair<qint64, qint64> key(qint64(patch.rect.left()), qint64(patch.rect.top()));
        seen.insert_or_assign(key, patch.image.cacheKey());
        if (m_taken.contains(key) && m_taken.at(key) == patch.image.cacheKey())
            continue;
        // A placed mask paints its grid; mapped into the layer's.
        const QRectF rect = m_maskStroke ? m_maskStroke->toGrid.mapRect(patch.rect).adjusted(-1, -1, 1, 1) : patch.rect;
        dirty = dirty ? dirty->united(rect) : rect;
    }
    const bool first = !m_image;
    const std::optional<QRectF> region = first ? std::optional(QRectF(QPointF(0, 0), grid).adjusted(-margin, -margin, margin, margin)) : dirty;
    if (!region)
        return;
    try {
        compose(*region, base, patches, mask);
        // Taken once redone: a failed pass is tried again.
        m_taken = seen;
        m_image = m_context;
    } catch (const ExportError &error) {
        qCWarning(lcRendering) << "a painted layer's effects could not be redone:" << error.what();
    }
}

// One region anew: its effects, with the pixels in them.
void LayerEffectsSurface::compose(const QRectF &region, const std::optional<ImportedImage> &base, const std::vector<BrushPatch> &patches,
                                  const std::optional<QImage> &mask)
{
    const QRectF bounds = QRectF(QPointF(0, 0), grid).adjusted(-margin, -margin, margin, margin);
    // Widened by the reach: Swift's margin misses inside effects.
    const double reach = this->reach();
    const QRectF inner = integral(region.adjusted(-reach, -reach, reach, reach)).intersected(bounds);
    // What reaches `inner`, cut where the export's padding ends.
    const QRectF outer = integral(inner.adjusted(-reach, -reach, reach, reach)).intersected(bounds);
    const QImage built = LayerEffectsKernel::render(window(outer, base, patches, mask), m_effects);
    // Painted on a copy: one without memory changes nothing.
    QImage next = m_context;
    QPainter painter(&next);
    if (!painter.isActive())
        throw ExportError(ExportError::Kind::render);
    // In the surface the grid starts at the margin.
    BrushRaster::draw(built, inner.translated(margin, margin), painter, inner.translated(-outer.left(), -outer.top()));
    painter.end();
    m_context = next;
}

// The layer as painted so far: pixels, tiles, mask.
QImage LayerEffectsSurface::window(const QRectF &region, const std::optional<ImportedImage> &base, const std::vector<BrushPatch> &patches,
                                   const std::optional<QImage> &mask) const
{
    QImage window = BrushRaster::context(int(region.width()), int(region.height()), false);
    QPainter painter(&window);
    painter.translate(-region.left(), -region.top());
    if (m_maskStroke) {
        // The pixels through the mask as the stroke leaves it.
        const QImage live = m_maskStroke->coverage(region);
        if (base && base->raster)
            base->raster->draw(sourceRect, painter);
        else if (base)
            BrushRaster::draw(base->image(), sourceRect, painter);
        painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        painter.drawImage(region, BrushRaster::alphaView(live));
        return window;
    }
    if (base && base->raster)
        base->raster->draw(sourceRect, painter);
    else if (base)
        BrushRaster::draw(base->image(), sourceRect, painter);
    for (const BrushPatch &patch : patches) {
        if (patch.rect.intersects(region))
            BrushRaster::draw(patch.image, patch.rect, painter);
    }
    if (mask) {
        // Only the layer's own pixels show through the mask.
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        painter.drawImage(sourceRect, BrushRaster::alphaView(*mask));
    }
    return window;
}
