#include "Rendering/RasterSnapshot.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace {
using Cell = std::pair<int, int>;

std::vector<Cell> cells(const QRectF &rect)
{
    std::vector<Cell> result;
    if (rect.isEmpty())
        return result;
    for (int y = int(std::floor(rect.top() / 256)); y <= int(std::ceil(rect.bottom() / 256)) - 1; ++y) {
        for (int x = int(std::floor(rect.left() / 256)); x <= int(std::ceil(rect.right() / 256)) - 1; ++x)
            result.push_back({x, y});
    }
    return result;
}

// Borrowed pixels, as CGImage.cropping; valid while image lives.
QImage view(const QImage &image, const QRect &rect)
{
    return QImage(image.constScanLine(rect.top()) + rect.left() * image.depth() / 8, rect.width(), rect.height(),
                  image.bytesPerLine(), image.format());
}

BrushPatch cropped(const BrushPatch &patch, const QRectF &rect)
{
    return {rect, patch.image.copy(rect.translated(-patch.rect.topLeft()).toAlignedRect())};
}

std::vector<BrushPatch> without(const BrushPatch &piece, const QRectF &overlap)
{
    const QRectF r = piece.rect;
    const QRectF rects[] = {QRectF(r.left(), r.top(), r.width(), overlap.top() - r.top()),
                            QRectF(r.left(), overlap.bottom(), r.width(), r.bottom() - overlap.bottom()),
                            QRectF(r.left(), overlap.top(), overlap.left() - r.left(), overlap.height()),
                            QRectF(overlap.right(), overlap.top(), r.right() - overlap.right(), overlap.height())};
    std::vector<BrushPatch> result;
    for (const QRectF &rect : rects) {
        if (rect.width() > 0 && rect.height() > 0)
            result.push_back(cropped(piece, rect));
    }
    return result;
}
}

RasterSnapshot::RasterSnapshot(int width, int height, QImage base, QRectF baseRect, std::vector<BrushPatch> patches,
                               bool isMask, std::optional<QPointF> alignment, double fill)
    : width(width), height(height), base(std::move(base)), baseRect(baseRect), patches(std::move(patches)),
      isMask(isMask), alignment(alignment.value_or(baseRect.topLeft())), fill(fill)
{
}

std::shared_ptr<const RasterSnapshot> RasterSnapshot::replacing(const std::optional<ImportedImage> &source,
                                                                const QRectF &sourceRect,
                                                                const std::vector<BrushPatch> &added,
                                                                const QRectF &crop, bool isMask, double fill)
{
    const std::shared_ptr<const RasterSnapshot> old = source ? source->raster : nullptr;
    const QPointF shift = sourceRect.topLeft() - crop.topLeft();
    std::vector<BrushPatch> additions;
    for (const BrushPatch &patch : added)
        additions.push_back({patch.rect.translated(-crop.topLeft()), patch.image});
    // Indexed by tile, so the handoff follows touched tiles.
    std::map<Cell, std::vector<int>> buckets;
    for (int index = 0; index < int(additions.size()); ++index) {
        for (const Cell &cell : cells(additions[index].rect))
            buckets[cell].push_back(index);
    }
    std::vector<BrushPatch> kept;
    for (const BrushPatch &oldPatch : old ? old->patches : std::vector<BrushPatch>()) {
        const BrushPatch patch{oldPatch.rect.translated(shift), oldPatch.image};
        std::set<int> candidates;
        for (const Cell &cell : cells(patch.rect)) {
            if (const auto found = buckets.find(cell); found != buckets.end())
                candidates.insert(found->second.begin(), found->second.end());
        }
        std::vector<BrushPatch> pieces{patch};
        for (int index : candidates) {
            std::vector<BrushPatch> split;
            for (const BrushPatch &piece : pieces) {
                const QRectF overlap = piece.rect.intersected(additions[index].rect);
                if (overlap.isEmpty()) {
                    split.push_back(piece);
                    continue;
                }
                const std::vector<BrushPatch> rest = without(piece, overlap);
                split.insert(split.end(), rest.begin(), rest.end());
            }
            pieces = std::move(split);
        }
        kept.insert(kept.end(), pieces.begin(), pieces.end());
    }
    kept.insert(kept.end(), additions.begin(), additions.end());
    const QRectF bounds(0, 0, crop.width(), crop.height());
    std::vector<BrushPatch> inside;
    for (const BrushPatch &patch : kept) {
        const QRectF rect = patch.rect.intersected(bounds);
        if (rect.isEmpty())
            continue;
        inside.push_back(rect == patch.rect ? patch : cropped(patch, rect));
    }
    const QImage base = old ? old->base : source ? source->image() : QImage();
    const QRectF baseRect = old ? old->baseRect.translated(shift) : sourceRect.translated(-crop.topLeft());
    const QPointF alignment = sourceRect.topLeft() + (old ? old->alignment : QPointF(0, 0)) - crop.topLeft();
    return std::make_shared<const RasterSnapshot>(int(crop.width()), int(crop.height()), base, baseRect,
                                                  std::move(inside), isMask, alignment, fill);
}

void RasterSnapshot::draw(const QRectF &rect, QPainter &context) const
{
    context.save();
    context.setClipRect(rect, Qt::IntersectClip);
    context.setRenderHint(QPainter::Antialiasing, false);
    // Past its pixels a grown mask is its background.
    if (isMask)
        context.fillRect(rect, QColor::fromRgbF(float(fill), float(fill), float(fill)));
    const double sx = rect.width() / width, sy = rect.height() / height;
    const auto mapped = [&](const QRectF &r) {
        return QRectF(rect.left() + r.left() * sx, rect.top() + r.top() * sy, r.width() * sx, r.height() * sy);
    };
    const QRectF visible = BrushRaster::visibleRect(context);
    if (!base.isNull()) {
        const QRectF target = mapped(baseRect);
        const QRectF overlap = target.intersected(visible);
        if (!overlap.isEmpty()) {
            const QRect crop = QRectF((overlap.left() - target.left()) / target.width() * base.width(),
                                      (overlap.top() - target.top()) / target.height() * base.height(),
                                      overlap.width() / target.width() * base.width(),
                                      overlap.height() / target.height() * base.height())
                                   .toAlignedRect().intersected(base.rect());
            if (!crop.isEmpty()) {
                const QRectF destination(target.left() + double(crop.left()) / base.width() * target.width(),
                                         target.top() + double(crop.top()) / base.height() * target.height(),
                                         double(crop.width()) / base.width() * target.width(),
                                         double(crop.height()) / base.height() * target.height());
                BrushRaster::draw(view(base, crop), destination, context);
            }
        }
    }
    for (const BrushPatch &patch : patches) {
        if (mapped(patch.rect).intersects(visible))
            BrushRaster::draw(patch.image, mapped(patch.rect), context);
    }
    context.restore();
}

QImage RasterSnapshot::makeImage() const
{
    const std::lock_guard<std::mutex> guard(m_lock);
    if (m_materialized.isNull()) {
        QImage surface = BrushRaster::context(width, height, isMask);
        QPainter painter(&surface);
        draw(QRectF(0, 0, width, height), painter);
        painter.end();
        m_materialized = surface;
    }
    return m_materialized;
}

bool RasterSnapshot::hasMaterializedPixels() const
{
    const std::lock_guard<std::mutex> guard(m_lock);
    return !m_materialized.isNull();
}

QImage RasterSnapshot::thumbnail() const
{
    const double factor = std::min(1.0, 96.0 / std::max(width, height));
    QImage surface = BrushRaster::context(std::max(1, int(width * factor)), std::max(1, int(height * factor)), isMask);
    QPainter painter(&surface);
    draw(QRectF(0, 0, surface.width(), surface.height()), painter);
    return surface;
}
