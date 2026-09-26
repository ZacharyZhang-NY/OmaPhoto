#include "Rendering/TiledLayerRenderer.h"
#include "Rendering/PoolMap.h"
#include "Logging.h"
#include "Rendering/DownsampleCache.h"
#include <QRegion>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <map>
#include <stdexcept>

namespace {
// Pieces compose and halve side by side, kept in order.
std::vector<TiledLayerRenderer::Piece> sideBySide(const std::vector<QRectF> &squares,
                                                  const std::function<std::optional<TiledLayerRenderer::Piece>(const QRectF &)> &build)
{
    std::vector<std::optional<TiledLayerRenderer::Piece>> built(squares.size());
    std::vector<size_t> order(squares.size());
    std::iota(order.begin(), order.end(), size_t(0));
    PoolMap::blocking(order, [&](size_t index) { built[index] = build(squares[index]); });
    std::vector<TiledLayerRenderer::Piece> pieces;
    for (std::optional<TiledLayerRenderer::Piece> &one : built) {
        if (one)
            pieces.push_back(std::move(*one));
    }
    return pieces;
}

using TiledLayerRenderer::Frame;
using TiledLayerRenderer::Piece;

// Columns whose centres satisfy low <= a*x+b < high.
std::pair<int, int> columns(double a, double b, double low, double high, int first, int last)
{
    if (a == 0)
        return low <= b && b < high ? std::pair(first, last) : std::pair(first, first);
    const double from = (low - b) / a, to = (high - b) / a;
    // A negative slope turns the half-open interval around.
    const double begin = a > 0 ? std::ceil(from - 0.5) : std::floor(to - 0.5) + 1;
    const double end = a > 0 ? std::ceil(to - 0.5) : std::floor(from - 0.5) + 1;
    // Near a right angle the slope is tiny: clamp first.
    return {int(std::clamp(begin, double(first), double(last))), int(std::clamp(end, double(first), double(last)))};
}

// Device pixels to grid pixels.
QTransform toGrid(const Frame &frame, const QPainter &context)
{
    return context.deviceTransform().inverted()
        * QTransform::fromTranslate(-frame.bounds.left(), -frame.bounds.top())
        * QTransform::fromScale(frame.pixelWidth / frame.bounds.width(), frame.pixelHeight / frame.bounds.height());
}

// Device pixels whose centres lie inside `area`, half-open.
QRegion pixelsInside(const QRectF &area, const Frame &frame, const QPainter &context)
{
    const QTransform grid = toGrid(frame, context);
    const QRect device(0, 0, context.device()->width(), context.device()->height());
    const QRect rows = context.deviceTransform().mapRect(frame.mapped(area)).toAlignedRect().intersected(device);
    QRegion region;
    for (int y = rows.top(); y <= rows.bottom(); ++y) {
        const double row = y + 0.5;
        const auto [left, right] = columns(grid.m11(), grid.m21() * row + grid.dx(), area.left(), area.right(),
                                           device.left(), device.right() + 1);
        const auto [begin, end] = columns(grid.m12(), grid.m22() * row + grid.dy(), area.top(), area.bottom(), left, right);
        if (end > begin)
            region += QRect(begin, y, end - begin, 1);
    }
    return region;
}

// The unchanged image, reduced by the frame's halvings.
void drawBase(const QImage &image, const QRectF &rect, const Frame &frame, QPainter &context)
{
    const DownsampleCache::Reduced reduced = DownsampleCache::shared().imageAtLevel(image, frame.level);
    const double step = 1 << reduced.level;
    const QRectF covered(rect.left(), rect.top(), reduced.image.width() * step * rect.width() / std::max(1, image.width()),
                         reduced.image.height() * step * rect.height() / std::max(1, image.height()));
    context.drawImage(frame.mapped(covered), reduced.image);
}

// An interior ending with its image opens one device pixel.
QRectF opened(const Piece &piece, const Frame &frame, const QPainter &context)
{
    // How far one device pixel reaches across each grid axis.
    const QTransform grid = toGrid(frame, context);
    const double dx = std::hypot(grid.m11(), grid.m21()), dy = std::hypot(grid.m12(), grid.m22());
    const QRectF &inside = piece.interior, &image = piece.region;
    return inside.adjusted(inside.left() <= image.left() ? -dx : 0, inside.top() <= image.top() ? -dy : 0,
                           inside.right() >= image.right() ? dx : 0, inside.bottom() >= image.bottom() ? dy : 0);
}

// A piece replaces its interior: cleared, then drawn.
void draw(const Piece &piece, const Frame &frame, QPainter &context)
{
    const QRegion pixels = pixelsInside(opened(piece, frame, context), frame, context);
    if (pixels.isEmpty())
        return;
    context.save();
    // A region in device pixels stays hard under antialiasing.
    context.setWorldMatrixEnabled(false);
    context.setClipRegion(pixels, Qt::IntersectClip);
    context.setCompositionMode(QPainter::CompositionMode_Clear);
    context.fillRect(pixels.boundingRect(), Qt::transparent);
    context.setCompositionMode(QPainter::CompositionMode_SourceOver);
    context.setWorldMatrixEnabled(true);
    context.drawImage(frame.mapped(piece.region), piece.image);
    context.restore();
}

// A committed raster placed at `offset` in the frame's grid.
void drawCommitted(const std::shared_ptr<const RasterSnapshot> &raster, QPointF offset, const Frame &frame, QPainter &context)
{
    if (!raster->base.isNull())
        drawBase(raster->base, raster->baseRect.translated(offset), frame, context);
    for (const Piece &cached : TiledPieceCache::shared().pieces(raster, frame.level)) {
        const Piece piece = cached.offsetBy(offset);
        if (opened(piece, frame, context).intersects(frame.visible))
            draw(piece, frame, context);
    }
}

// A mask piece replaces its interior of the mask coverage.
void drawCoverage(const Piece &piece, const Frame &frame, QPainter &veil)
{
    draw(Piece{piece.interior, piece.region, BrushRaster::alphaView(piece.image)}, frame, veil);
}

// The part of `image`, placed at `rect`, inside `region`.
void drawCropped(const QImage &image, const QRectF &rect, const QRectF &region, QPainter &context)
{
    if (image.width() != rect.width() || image.height() != rect.height()) {
        // A solid 1 x 1 mask, say, stretches over `rect`.
        BrushRaster::draw(image, rect, context);
        return;
    }
    const QRectF local = QRectF(region.translated(-rect.topLeft()).intersected(QRectF(image.rect())).toAlignedRect());
    if (!local.isEmpty())
        BrushRaster::draw(image, local.translated(rect.topLeft()), context, local);
}

using Body = std::function<void(const Frame &, QPainter &)>;

// Composes a grid holding `painted` aside; `maskRect` places the mask.
void withFrame(int pixelWidth, int pixelHeight, const QRectF &painted, const QRectF &maskRect, const LayerTransform &transform,
               QPointF center, QPainter &context, const LayerRenderer::Options &options, const Body &body, const Body &veil)
{
    const double width = transform.size.width() * options.scale, height = transform.size.height() * options.scale;
    if (width <= 0 || height <= 0 || pixelWidth <= 0 || pixelHeight <= 0)
        return;
    const double device = LayerRenderer::deviceScale(context);
    const int level = transform.sampling == LayerSampling::nearest ? 0 : DownsampleCache::level(width * device / pixelWidth);
    const InterpolationQuality quality = LayerRenderer::interpolation(transform.sampling, width * device / pixelWidth * (1 << level));
    QTransform placement;
    placement.translate(center.x(), center.y());
    placement.rotateRadians(transform.radians());
    placement.scale(transform.flipX ? -1 : 1, transform.flipY ? -1 : 1);
    const QRectF bounds(-width / 2, -height / 2, width, height);
    const QRectF shown = placement.inverted().mapRect(BrushRaster::visibleRect(context));
    const double sx = pixelWidth / width, sy = pixelHeight / height;
    const Frame frame{bounds, double(pixelWidth), double(pixelHeight), level, device, transform.sampling,
                      QRectF((shown.left() - bounds.left()) * sx, (shown.top() - bounds.top()) * sy, shown.width() * sx, shown.height() * sy)};
    // Halvings and aligned pieces overhang by less than a step.
    const double step = 1 << level;
    const QRectF extent = frame.mapped(painted.adjusted(-step, -step, step, step));
    const auto framed = [&](const Body &paint) {
        return paint ? std::function<void(QPainter &)>([&, paint](QPainter &aside) { paint(frame, aside); }) : std::function<void(QPainter &)>();
    };
    LayerRenderer::composite(context, placement, extent, transform.sampling, quality, options, frame.mapped(maskRect), framed(body), framed(veil));
}
}

TiledLayerRenderer::Piece TiledLayerRenderer::Piece::offsetBy(QPointF offset) const
{
    return {interior.translated(offset), region.translated(offset), image};
}

QRectF TiledLayerRenderer::Frame::mapped(const QRectF &rect) const
{
    return {bounds.left() + rect.left() / pixelWidth * bounds.width(), bounds.top() + rect.top() / pixelHeight * bounds.height(),
            rect.width() / pixelWidth * bounds.width(), rect.height() / pixelHeight * bounds.height()};
}

double TiledLayerRenderer::support(int level)
{
    return level == 0 ? 8 : 16 << level;
}

void TiledLayerRenderer::drawRaster(const std::shared_ptr<const RasterSnapshot> &raster, const LayerTransform &transform,
                                    QPointF center, QPainter &context, const LayerRenderer::Options &options)
{
    const QRectF full(0, 0, raster->width, raster->height);
    withFrame(raster->width, raster->height, full, full, transform, center, context, options,
              [&](const Frame &frame, QPainter &aside) { drawCommitted(raster, QPointF(0, 0), frame, aside); }, {});
}

void TiledLayerRenderer::drawStroke(int width, int height, const QRectF &sourceRect, const std::vector<BrushPatch> &patches,
                                    const QImage &image, const std::shared_ptr<const RasterSnapshot> &raster,
                                    const LayerTransform &transform, QPointF center, QPainter &context,
                                    const LayerRenderer::Options &options)
{
    QRectF painted(0, 0, width, height);
    std::vector<QRectF> changed;
    for (const BrushPatch &patch : patches) {
        painted = painted.united(patch.rect);
        changed.push_back(patch.rect);
    }
    std::vector<Piece> pieces;
    const QPointF origin = sourceRect.topLeft() + (raster ? raster->alignment : QPointF(0, 0));
    const Body body = [&](const Frame &frame, QPainter &aside) {
        const std::vector<QRectF> squares = interiors(changed, support(frame.level), strokeCell, 1 << frame.level, origin, frame.visible);
        const std::vector<Piece> built = sideBySide(squares, [&](const QRectF &square) {
            return piece(square, frame.level, origin, painted, false, [&](QPainter &painter, const QRectF &region) {
                if (raster)
                    raster->draw(QRectF(sourceRect.topLeft(), QSizeF(raster->width, raster->height)), painter);
                else if (!image.isNull())
                    drawCropped(image, sourceRect, region, painter);
                for (const BrushPatch &patch : patches) {
                    if (patch.rect.intersects(region))
                        BrushRaster::draw(patch.image, patch.rect, painter);
                }
            });
        });
        pieces.insert(pieces.end(), built.begin(), built.end());
        if (raster)
            drawCommitted(raster, sourceRect.topLeft(), frame, aside);
        else if (!image.isNull())
            drawBase(image, sourceRect, frame, aside);
        for (const Piece &piece : pieces)
            draw(piece, frame, aside);
    };
    const Body veil = [&](const Frame &frame, QPainter &masking) {
        // Paint past the old bounds is revealed, not masked.
        std::vector<QRectF> past;
        for (const Piece &built : pieces) {
            if (!sourceRect.contains(built.interior))
                past.push_back(built.interior);
        }
        const std::vector<Piece> shown = sideBySide(past, [&](const QRectF &interior) {
            return piece(interior, frame.level, origin, painted, true, [&](QPainter &painter, const QRectF &region) {
                painter.fillRect(region, Qt::white);
                drawCropped(options.mask, sourceRect, region, painter);
            });
        });
        for (const Piece &one : shown)
            drawCoverage(one, frame, masking);
    };
    withFrame(width, height, painted, sourceRect, transform, center, context, options, body, options.mask.isNull() ? Body() : veil);
}

void TiledLayerRenderer::drawMaskStroke(int width, int height, const QRectF &sourceRect, const std::vector<BrushPatch> &patches,
                                        const std::optional<ImportedImage> &oldMask, const QImage &image,
                                        const std::shared_ptr<const RasterSnapshot> &raster, const LayerTransform &transform,
                                        QPointF center, QPainter &context, const LayerRenderer::Options &options)
{
    if (!options.mask.isNull())
        throw std::logic_error("a mask stroke draws through the mask it paints");
    LayerRenderer::Options masked = options;
    if (oldMask)
        masked.mask = oldMask->image();
    std::vector<QRectF> changed;
    for (const BrushPatch &patch : patches)
        changed.push_back(patch.rect);
    const Body body = [&](const Frame &frame, QPainter &aside) {
        if (raster)
            drawCommitted(raster, sourceRect.topLeft(), frame, aside);
        else if (!image.isNull())
            drawBase(image, sourceRect, frame, aside);
    };
    const Body veil = [&](const Frame &frame, QPainter &masking) {
        // Pieces halve on the mask's own grid and levels.
        const int level = frame.sampling == LayerSampling::nearest
            ? 0 : DownsampleCache::level(frame.mapped(sourceRect).width() * frame.device / std::max(1.0, sourceRect.width()));
        const QPointF origin = sourceRect.topLeft();
        const std::vector<Piece> built = sideBySide(interiors(changed, support(level), strokeCell, 1 << level, origin, frame.visible), [&](const QRectF &interior) {
            return piece(interior, level, origin, sourceRect, true, [&](QPainter &painter, const QRectF &region) {
                // Beyond the old mask an edit reveals.
                painter.fillRect(region, Qt::white);
                if (oldMask)
                    drawCropped(masked.mask, sourceRect, region, painter);
                for (const BrushPatch &patch : patches) {
                    if (patch.rect.intersects(region))
                        BrushRaster::draw(patch.image, patch.rect, painter);
                }
            });
        });
        for (const Piece &one : built)
            drawCoverage(one, frame, masking);
    };
    withFrame(width, height, QRectF(0, 0, width, height), sourceRect, transform, center, context, masked, body, veil);
}

std::optional<TiledLayerRenderer::Piece> TiledLayerRenderer::piece(const QRectF &interior, int level, QPointF origin, const std::optional<QRectF> &bounds,
                                               bool mask, const std::function<void(QPainter &, const QRectF &)> &compose)
{
    const double margin = support(level), step = 1 << level;
    QRectF region = aligned(interior.adjusted(-margin, -margin, margin, margin), step, origin);
    if (bounds) {
        // Past the pixels' edge there is nothing to compose.
        region = region.intersected(aligned(*bounds, step, origin));
        if (region.isEmpty())
            return std::nullopt;
    }
    const int width = int(region.width()), height = int(region.height());
    if (width <= 0 || height <= 0 || qint64(width) * height > 64'000'000)
        return std::nullopt;
    QImage image(width, height, mask ? QImage::Format_Grayscale8 : QImage::Format_RGBA8888_Premultiplied);
    if (image.isNull()) {
        qCWarning(lcRendering) << "a tile piece could not be allocated:" << width << "x" << height;
        return std::nullopt;
    }
    image.fill(0);
    {
        QPainter painter(&image);
        painter.translate(-region.topLeft());
        compose(painter, region);
    }
    for (int halving = 0; halving < level; ++halving) {
        const std::optional<QImage> half = DownsampleCache::halve(image, false);
        if (!half) {
            qCWarning(lcRendering) << "a tile piece could not be halved";
            return std::nullopt;
        }
        image = *half;
    }
    const QRectF kept = bounds ? interior.intersected(region) : interior;
    if (kept.isEmpty())
        return std::nullopt;
    return Piece{kept, region, image};
}

std::vector<QRectF> TiledLayerRenderer::interiors(const std::vector<QRectF> &rects, double margin, double size, double step,
                                                  QPointF origin, const std::optional<QRectF> &visible)
{
    std::map<std::pair<int, int>, QRectF> parts;
    for (const QRectF &rect : rects) {
        const QRectF grown = rect.adjusted(-margin, -margin, margin, margin);
        if (visible && !grown.intersects(*visible))
            continue;
        const int x0 = int(std::floor((grown.left() - origin.x()) / size)), x1 = int(std::ceil((grown.right() - origin.x()) / size));
        const int y0 = int(std::floor((grown.top() - origin.y()) / size)), y1 = int(std::ceil((grown.bottom() - origin.y()) / size));
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const QRectF part = grown.intersected(QRectF(origin.x() + x * size, origin.y() + y * size, size, size));
                if (part.isEmpty())
                    continue;
                const auto found = parts.find({y, x});
                if (found == parts.end())
                    parts.insert({{y, x}, part});
                else
                    found->second = found->second.united(part);
            }
        }
    }
    // Squares sit on the step grid: grown parts stay inside.
    std::vector<QRectF> result;
    for (const auto &[cell, part] : parts) {
        const QRectF interior = aligned(part, step, origin);
        if (!visible || interior.intersects(*visible))
            result.push_back(interior);
    }
    return result;
}

QRectF TiledLayerRenderer::aligned(const QRectF &rect, double step, QPointF origin)
{
    const double left = origin.x() + std::floor((rect.left() - origin.x()) / step) * step;
    const double top = origin.y() + std::floor((rect.top() - origin.y()) / step) * step;
    const double right = origin.x() + std::ceil((rect.right() - origin.x()) / step) * step;
    const double bottom = origin.y() + std::ceil((rect.bottom() - origin.y()) / step) * step;
    return {left, top, right - left, bottom - top};
}

TiledPieceCache::TiledPieceCache(qint64 pixelBudget) : m_pixelBudget(pixelBudget) {}

TiledPieceCache &TiledPieceCache::shared()
{
    static TiledPieceCache cache;
    return cache;
}

std::size_t TiledPieceCache::KeyHash::operator()(const Key &key) const
{
    return std::hash<const RasterSnapshot *>()(key.raster) ^ (std::hash<int>()(key.level) << 1);
}

std::vector<TiledLayerRenderer::Piece> TiledPieceCache::pieces(const std::shared_ptr<const RasterSnapshot> &raster, int level)
{
    const Key key{raster.get(), level};
    {
        const std::lock_guard<std::mutex> guard(m_lock);
        m_clock += 1;
        if (const auto found = m_entries.find(key); found != m_entries.end()) {
            found->second.lastUse = m_clock;
            return found->second.pieces;
        }
    }
    const QPointF origin = raster->alignment;
    const QRectF full(0, 0, raster->width, raster->height);
    std::vector<QRectF> changed;
    for (const BrushPatch &patch : raster->patches)
        changed.push_back(patch.rect);
    const std::vector<TiledLayerRenderer::Piece> pieces = sideBySide(
        TiledLayerRenderer::interiors(changed, TiledLayerRenderer::support(level), TiledLayerRenderer::committedCell, 1 << level, origin, std::nullopt),
        [&](const QRectF &square) {
            return TiledLayerRenderer::piece(square, level, origin, full, raster->isMask, [&](QPainter &painter, const QRectF &) { raster->draw(full, painter); });
        });
    qint64 pixels = 0;
    for (const TiledLayerRenderer::Piece &piece : pieces)
        pixels += qint64(piece.image.width()) * piece.image.height();
    const std::lock_guard<std::mutex> guard(m_lock);
    m_entries.insert_or_assign(key, Entry{raster, pieces, m_clock, pixels});
    qint64 total = 0;
    for (const auto &[entryKey, entry] : m_entries)
        total += entry.pixels;
    while (total > m_pixelBudget) {
        auto oldest = m_entries.end();
        for (auto entry = m_entries.begin(); entry != m_entries.end(); ++entry) {
            if (!(entry->first == key) && (oldest == m_entries.end() || entry->second.lastUse < oldest->second.lastUse))
                oldest = entry;
        }
        if (oldest == m_entries.end())
            break;
        total -= oldest->second.pixels;
        m_entries.erase(oldest);
    }
    return pieces;
}
