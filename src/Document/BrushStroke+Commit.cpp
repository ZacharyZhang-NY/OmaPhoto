#include "Document/BrushStroke.h"
#include "Document/LayerMask.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore.h"
#include "Rendering/RasterSnapshot.h"
#include <QRandomGenerator>
#include <cstring>
#include <new>

extern "C" {
#include "BrushPixels.h"
#include "HealPixels.h"
}

// The stroke as a layer: bounds, placement, the commit's image.
namespace {
QRectF integral(const QRectF &rect)
{
    return QRectF(QPointF(std::floor(rect.left()), std::floor(rect.top())), QPointF(std::ceil(rect.right()), std::ceil(rect.bottom())));
}

// Half-open bounds of the pixels that are there, or empty.
QRectF alphaBounds(const QImage &pixels)
{
    size_t edges[4] = {0, 0, 0, 0};
    brush_alpha_bounds(pixels.constBits(), size_t(pixels.width()), size_t(pixels.height()), size_t(pixels.bytesPerLine()), edges);
    if (edges[2] <= edges[0] || edges[3] <= edges[1])
        return QRectF();
    return QRectF(edges[0], edges[1], edges[2] - edges[0], edges[3] - edges[1]);
}

void drawSource(const ImportedImage &source, const QRectF &rect, QPainter &painter)
{
    if (source.raster)
        source.raster->draw(rect, painter);
    else
        BrushRaster::draw(source.image(), rect, painter);
}
}

QRectF BrushStroke::committedBounds() const
{
    return integral(m_allocatedBounds.value_or(sourceRect));
}

LayerTransform BrushStroke::committedTransform() const
{
    return transform(committedBounds());
}

LayerTransform BrushStroke::transform(const QRectF &bounds) const
{
    const QPointF center = pixelToDocument.map(bounds.center());
    LayerTransform result = paintTransform;
    result.size = {bounds.width() * paintTransform.size.width() / width, bounds.height() * paintTransform.size.height() / height};
    result.origin = {center.x() - result.size.width() / 2, center.y() - result.size.height() / 2};
    return result;
}

// Offset into grid pixels, turned and scaled as the layer.
QRectF BrushStroke::gridRect(const QRectF &rect, QSizeF offset) const
{
    const QTransform toGrid = pixelToDocument.inverted();
    const QPointF shift = toGrid.map(QPointF(offset.width(), offset.height())) - toGrid.map(QPointF(0, 0));
    return rect.translated(-shift);
}

// Swift's heal: the painted spot rebuilt from nearby texture.
void BrushStroke::heal()
{
    if (!settings.healing || isMask)
        return;
    std::optional<QRectF> painted;
    for (const auto &[key, coverage] : m_coverage) {
        long edges[4];
        heal_coverage_bounds(coverage.constBits(), size_t(coverage.width()), size_t(coverage.height()), size_t(coverage.bytesPerLine()), edges);
        if (edges[2] <= edges[0] || edges[3] <= edges[1])
            continue;
        const QRectF rect = QRectF(edges[0], edges[1], edges[2] - edges[0], edges[3] - edges[1]).translated(m_tiles.at(key).rect.topLeft());
        painted = painted ? painted->united(rect) : rect;
    }
    if (!painted)
        return;
    // Room for the kernel's search, about three spots away.
    const double reach = (std::max(painted->width(), painted->height()) + 32) * 3.2;
    const QRect region = painted->adjusted(-reach, -reach, reach, reach).intersected(QRectF(0, 0, width, height)).toAlignedRect();
    QImage pixels = BrushRaster::context(region.width(), region.height(), false);
    {
        QPainter painter(&pixels);
        const QRectF placed = sourceRect.translated(-region.topLeft());
        if (m_source && m_source->raster)
            m_source->raster->draw(placed, painter);
        else if (m_source)
            BrushRaster::draw(m_source->image(), placed, painter);
    }
    // The kernel reads coverage rows packed, as Swift's contexts are.
    const std::unique_ptr<uint8_t[]> painting(new (std::nothrow) uint8_t[size_t(region.width()) * size_t(region.height())]());
    if (!painting)
        throw ExportError(ExportError::Kind::render);
    for (const auto &[key, coverage] : m_coverage) {
        const QRect tile = m_tiles.at(key).rect.toRect().translated(-region.topLeft());
        const QRect within = tile.intersected(QRect(QPoint(0, 0), region.size()));
        for (int y = within.top(); y <= within.bottom(); ++y)
            std::memcpy(painting.get() + size_t(y) * size_t(region.width()) + size_t(within.left()),
                        coverage.constScanLine(y - tile.top()) + (within.left() - tile.left()), size_t(within.width()));
    }
    if (spot_heal(pixels.bits(), painting.get(), size_t(region.width()), size_t(region.height()), size_t(pixels.bytesPerLine()),
                  float(settings.opacity), int(settings.healingMode), QRandomGenerator::global()->generate()) != 0)
        throw ProjectError(ProjectError::Kind::tooLarge);
    for (const auto &[key, coverage] : m_coverage) {
        Tile &tile = m_tiles.at(key);
        QImage healed = tile.base;
        healed.detach();
        if (healed.isNull())
            throw ExportError(ExportError::Kind::render);
        {
            QPainter painter(&healed);
            BrushRaster::draw(pixels, QRectF(region).translated(-tile.rect.topLeft()), painter);
        }
        // Swift clips the context; the selection blends here instead.
        tile.context = selectionClip ? PixelAdjust::blend(healed, tile.base, *selectionClip,
                                                          QTransform::fromTranslate(tile.rect.left(), tile.rect.top()) * pixelToDocument, false)
                                     : healed;
        tile.image = tile.context;
    }
}

// Painting only adds alpha: the touched tiles alone are inspected.
PaintSnapshot BrushStroke::paintSnapshot() const
{
    std::optional<QRectF> bounds = m_source ? std::optional(sourceRect) : std::nullopt;
    for (const auto &[key, tile] : m_tiles) {
        if (isMask)
            break;
        const QRectF rect = alphaBounds(tile.context);
        if (rect.isEmpty())
            continue;
        const QRectF placed = rect.translated(tile.rect.topLeft());
        bounds = bounds ? std::optional(bounds->united(placed)) : std::optional(placed);
    }
    // A mask keeps every tile touched: past its pixels, grows.
    const QRectF crop = isMask ? committedBounds() : bounds.value_or(committedBounds());
    const std::shared_ptr<const RasterSnapshot> raster = RasterSnapshot::replacing(m_source, sourceRect, patches(), crop, isMask, maskBackground);
    return {ImportedImage(raster, raster->thumbnail(), layer.name), transform(crop), crop};
}

BrushCommit::Input BrushStroke::commitInput() const
{
    const QRectF bounds = committedBounds();
    std::vector<BrushPatch> shifted;
    for (const BrushPatch &patch : patches())
        shifted.push_back({patch.rect.translated(-bounds.topLeft()), patch.image});
    return {int(bounds.width()), int(bounds.height()), m_source, shifted, isMask, layer.name, sourceRect.translated(-bounds.topLeft()), maskBackground};
}

ImportedImage BrushCommit::expandMask(const ImportedImage &asset, const Input &input, const QRectF &crop)
{
    if (input.sourceRect == crop)
        return asset;
    QImage context = BrushRaster::context(int(crop.width()), int(crop.height()), true);
    // New canvas area has no mask; old coverage stays aligned.
    context.fill(Qt::white);
    QPainter painter(&context);
    drawSource(asset, input.sourceRect.translated(-crop.topLeft()), painter);
    painter.end();
    return LayerMask::assetFrom(context);
}

BrushCommit::Output BrushCommit::render(const Input &input)
{
    QImage context = BrushRaster::context(input.width, input.height, input.mask);
    if (input.mask)
        context.fill(qRound(input.fill * 255));
    QPainter painter(&context);
    if (input.source)
        drawSource(*input.source, input.sourceRect, painter);
    for (const BrushPatch &patch : input.patches)
        BrushRaster::draw(patch.image, patch.rect, painter);
    painter.end();
    const QRectF fullBounds(0, 0, input.width, input.height);
    if (input.mask)
        return {LayerMask::assetFrom(context), fullBounds};
    // Every pixel with alpha stays, the soft rim included.
    const QRectF found = alphaBounds(context);
    const QRectF crop = found.isEmpty() ? fullBounds : found;
    const QImage image = crop == fullBounds ? context : context.copy(crop.toRect());
    if (image.isNull())
        throw ExportError(ExportError::Kind::render);
    return {ImportedImage(image, PixelAdjust::thumbnail(image), input.name), crop};
}
