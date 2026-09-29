#include "Document/BrushStroke.h"
#include "Document/DocumentLimits.h"
#include "Rendering/PoolMap.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore.h"
#include "Rendering/RasterSnapshot.h"
#include <QtConcurrent>
#include <cmath>
#include <functional>
#include <stdexcept>

// The stroke's geometry, samples, tiles and coverage.
namespace {
QRectF integral(const QRectF &rect)
{
    return QRectF(QPointF(std::floor(rect.left()), std::floor(rect.top())), QPointF(std::ceil(rect.right()), std::ceil(rect.bottom())));
}

// The clone through coverage and a clip: over, or copied.
void fillClone(const QImage &sample, const QImage &coverage, double alpha, QImage &context, const QImage &clip, bool replaces)
{
    const bool gray = context.format() == QImage::Format_Grayscale8;
    if (sample.format() != QImage::Format_RGBA8888_Premultiplied || (gray && replaces))
        throw std::logic_error("a clone samples premultiplied colour, and a mask never copies");
    const int channels = gray ? 1 : 4;
    for (int y = 0; y < coverage.height(); ++y) {
        const uchar *mask = coverage.constScanLine(y), *source = sample.constScanLine(y);
        const uchar *within = clip.isNull() ? nullptr : clip.constScanLine(y);
        uchar *row = context.scanLine(y);
        for (int x = 0; x < coverage.width(); ++x) {
            const double k = mask[x] / 255.0 * alpha * (within ? within[x] / 255.0 : 1.0);
            // Copying replaces by coverage alone, so clear clears.
            const double m = mask[x] / 255.0 * (within ? within[x] / 255.0 : 1.0);
            if (replaces ? m <= 0 : k <= 0)
                continue;
            // A gray sample's edge keeps its coverage, as CoreGraphics draws.
            const uchar *from = source + x * 4;
            uchar *pixel = row + x * channels;
            const double keep = replaces ? 1 - m : 1 - from[3] / 255.0 * k;
            for (int c = 0; c < channels; ++c)
                pixel[c] = uchar(pixel[c] * keep + from[c] * k + 0.5);
        }
    }
}

// The tiles a pixel rect touches, by key.
template <typename Body> void eachTile(const QRectF &affected, int width, const Body &body)
{
    if (affected.isEmpty())
        return;
    const qint64 columns = (width + BrushStroke::tileSize - 1) / BrushStroke::tileSize;
    const qint64 firstY = qint64(affected.top()) / BrushStroke::tileSize, lastY = (qint64(std::ceil(affected.bottom())) - 1) / BrushStroke::tileSize;
    const qint64 firstX = qint64(affected.left()) / BrushStroke::tileSize, lastX = (qint64(std::ceil(affected.right())) - 1) / BrushStroke::tileSize;
    for (qint64 y = firstY; y <= lastY; ++y) {
        for (qint64 x = firstX; x <= lastX; ++x)
            body(y * columns + x, x, y);
    }
}
}

double BrushStroke::spacingFraction(double hardness)
{
    return hardness >= 1 ? 0.015 : 0.025;
}

BrushStroke::Grid BrushStroke::grid(const ImageLayer &layer, bool mask, const BrushSettings &settings, const QRectF &canvas, bool growsMask)
{
    // A placed mask paints in its grid; else the layer's.
    const bool placedMask = mask && layer.mask && layer.mask->placement;
    const LayerTransform base = placedMask ? *layer.mask->placement : layer.transform;
    // A solid placed mask gets a pixel per document pixel.
    const bool solidPlaced = placedMask && layer.mask->asset.size().width() <= 2 && layer.mask->asset.size().height() <= 2;
    const int originalWidth = solidPlaced ? std::max(1, int(std::round(base.size.width())))
        : placedMask            ? layer.mask->asset.size().width()
        : layer.asset           ? layer.asset->size().width()
                                : int(std::round(layer.size().width()));
    const int originalHeight = solidPlaced ? std::max(1, int(std::round(base.size.height())))
        : placedMask             ? layer.mask->asset.size().height()
        : layer.asset            ? layer.asset->size().height()
                                 : int(std::round(layer.size().height()));
    if (originalWidth < 1 || originalWidth > DocumentLimits::maxSide || originalHeight < 1 || originalHeight > DocumentLimits::maxSide)
        throw ProjectError(ProjectError::Kind::tooLarge);
    const QTransform originalMapping = BrushRaster::pixelToDocument(base, originalWidth, originalHeight);
    const QRectF originalBounds(0, 0, originalWidth, originalHeight);
    const QRectF extent = mask && !growsMask ? originalBounds : originalBounds.united(integral(originalMapping.inverted().mapRect(canvas)));
    // Judged as a double: past int the conversion is undefined.
    if (extent.width() < 1 || extent.width() > 1'000'000'000 || extent.height() < 1 || extent.height() > 1'000'000'000)
        throw ProjectError(ProjectError::Kind::tooLarge);
    Grid grid{int(extent.width()), int(extent.height()), originalBounds.translated(-extent.topLeft()),
              QTransform::fromTranslate(extent.left(), extent.top()) * originalMapping, base};
    grid.paintTransform.size = {grid.width * base.size.width() / originalWidth, grid.height * base.size.height() / originalHeight};
    const QPointF center = originalMapping.map(extent.center());
    grid.paintTransform.origin = {center.x() - grid.paintTransform.size.width() / 2, center.y() - grid.paintTransform.size.height() / 2};
    if (!std::isfinite(settings.diameter) || settings.diameter < 1 || settings.diameter > 2000
        || !std::isfinite(settings.hardness) || settings.hardness < 0 || settings.hardness > 1
        || !std::isfinite(settings.opacity) || settings.opacity < 0.01 || settings.opacity > 1)
        throw ProjectError(ProjectError::Kind::tooLarge);
    return grid;
}

BrushStroke::BrushStroke(const ImageLayer &layer, bool mask, const BrushSettings &settings, QSizeF canvas, bool growsMask)
    : BrushStroke(layer, mask, settings, QRectF(QPointF(0, 0), canvas), grid(layer, mask, settings, QRectF(QPointF(0, 0), canvas), growsMask))
{
}

BrushStroke::BrushStroke(const ImageLayer &layer, bool mask, const BrushSettings &settings, const QRectF &canvas, const Grid &grid)
    : layer(layer), isMask(mask), settings(settings), canvas(canvas), width(grid.width), height(grid.height), sourceRect(grid.sourceRect),
      pixelToDocument(grid.pixelToDocument), paintTransform(grid.paintTransform),
      maskBackground(mask && layer.mask ? LayerMask::background(layer.mask->asset.thumbnail) : 1),
      m_source(mask ? (layer.mask ? std::optional(layer.mask->asset) : std::nullopt) : layer.asset),
      m_paintColor(mask ? QColor::fromRgbF(settings.red, settings.red, settings.red) : QColor::fromRgbF(settings.red, settings.green, settings.blue))
{
}

void BrushStroke::append(QPointF point)
{
    if (!std::isfinite(point.x()) || !std::isfinite(point.y()) || std::abs(point.x()) > 10'000'000 || std::abs(point.y()) > 10'000'000)
        return;
    if (!m_samples.empty() && m_samples.back() == point)
        return;
    m_samples.push_back(point);
    if (m_samples.size() > 4)
        m_samples.erase(m_samples.begin());
    const size_t n = m_samples.size();
    std::vector<BrushSegment> settled;
    if (n == 1)
        settled = {BrushSegment{float(point.x()), float(point.y()), float(point.x()), float(point.y())}};
    else if (n >= 3)
        settled = continuousCurve(m_samples[n - 3], m_samples[n - 2], m_samples[n >= 4 ? n - 4 : 0], point);
    std::vector<BrushSegment> tail;
    if (n >= 2)
        tail = {BrushSegment{float(m_samples[n - 2].x()), float(m_samples[n - 2].y()), float(point.x()), float(point.y())}};
    renderContinuous(settled, tail);
}

void BrushStroke::flush()
{
    const size_t n = m_samples.size();
    if (n < 2)
        return;
    const std::vector<BrushSegment> settled = continuousCurve(m_samples[n - 2], m_samples[n - 1], m_samples[n >= 3 ? n - 3 : 0], m_samples[n - 1]);
    renderContinuous(settled, {});
    m_samples = {m_samples[n - 1]};
}

// Centripetal Catmull–Rom, subdivided within 0.2 document pixels.
std::vector<BrushSegment> BrushStroke::continuousCurve(QPointF start, QPointF end, QPointF before, QPointF after) const
{
    const auto knot = [](double t, QPointF a, QPointF b) { return t + std::max(0.0001, std::sqrt(std::hypot(b.x() - a.x(), b.y() - a.y()))); };
    const auto mix = [](QPointF a, QPointF b, double ta, double tb, double t) {
        const double wa = (tb - t) / (tb - ta), wb = (t - ta) / (tb - ta);
        return QPointF(a.x() * wa + b.x() * wb, a.y() * wa + b.y() * wb);
    };
    const double t0 = 0, t1 = knot(t0, before, start), t2 = knot(t1, start, end), t3 = knot(t2, end, after);
    const auto point = [&](double u) {
        if (u == 0)
            return start;
        if (u == 1)
            return end;
        const double t = t1 + (t2 - t1) * u;
        const QPointF a = mix(before, start, t0, t1, t), b = mix(start, end, t1, t2, t), c = mix(end, after, t2, t3, t);
        return mix(mix(a, b, t0, t2, t), mix(b, c, t1, t3, t), t1, t2, t);
    };
    std::vector<BrushSegment> result;
    const std::function<void(QPointF, QPointF, double, double, int)> subdivide = [&](QPointF a, QPointF b, double lo, double hi, int depth) {
        const double dx = b.x() - a.x(), dy = b.y() - a.y(), lengthSquared = dx * dx + dy * dy;
        const auto error = [&](QPointF p) {
            const double t = lengthSquared > 0 ? std::min(1.0, std::max(0.0, ((p.x() - a.x()) * dx + (p.y() - a.y()) * dy) / lengthSquared)) : 0;
            return std::hypot(p.x() - a.x() - t * dx, p.y() - a.y() - t * dy);
        };
        const double mid = (lo + hi) / 2;
        const QPointF m = point(mid);
        const double deviation = std::max({error(m), error(point((lo + mid) / 2)), error(point((mid + hi) / 2))});
        if (deviation <= 0.2 || depth >= 10) {
            result.push_back(BrushSegment{float(a.x()), float(a.y()), float(b.x()), float(b.y())});
            return;
        }
        subdivide(a, m, lo, mid, depth + 1);
        subdivide(m, b, mid, hi, depth + 1);
    };
    subdivide(start, end, 0, 1, 0);
    return result;
}

std::set<qint64> BrushStroke::continuousKeys(const std::vector<BrushSegment> &segments) const
{
    std::set<qint64> keys;
    const double reach = settings.diameter / 2 + 2;
    const QTransform inverse = pixelToDocument.inverted();
    for (const BrushSegment &s : segments) {
        const QRectF box = QRectF(QPointF(std::min(s.x0, s.x1), std::min(s.y0, s.y1)), QPointF(std::max(s.x0, s.x1), std::max(s.y0, s.y1)))
                               .adjusted(-reach, -reach, reach, reach).intersected(canvas);
        if (box.isEmpty())
            continue;
        const QRectF affected = integral(inverse.mapRect(box)).intersected(QRectF(0, 0, width, height));
        eachTile(affected, width, [&](qint64 key, qint64, qint64) { keys.insert(key); });
    }
    return keys;
}

void BrushStroke::renderContinuous(const std::vector<BrushSegment> &settled, const std::vector<BrushSegment> &tail)
{
    const std::set<qint64> tailKeys = continuousKeys(tail);
    std::set<qint64> changed = continuousKeys(settled);
    changed.insert(tailKeys.begin(), tailKeys.end());
    changed.insert(m_tailKeys.begin(), m_tailKeys.end());
    const qint64 columns = (width + tileSize - 1) / tileSize;
    std::vector<BrushCoverage::Work> work;
    for (qint64 key : changed) {
        allocateTile(key, key % columns, key / columns);
        const Tile &tile = m_tiles.at(key);
        if (!m_coverage.contains(key)) {
            m_coverage.emplace(key, BrushRaster::context(int(tile.rect.width()), int(tile.rect.height()), true));
            m_coverageTiles.emplace(key, BrushCoverage::tile(int(tile.rect.width()), int(tile.rect.height())));
        }
        work.push_back({&m_coverageTiles.at(key), tile.rect, &m_coverage.at(key)});
    }
    BrushCoverage::render(work, settled, tail, pixelToDocument, settings, canvas.size());
    m_tailKeys = tailKeys;
    publish(changed);
}

// Each tile anew: original, then colour × coverage × opacity.
void BrushStroke::publish(const std::set<qint64> &changed)
{
    struct Fill {
        Tile *tile;
        const QImage *coverage;
        QImage clip;
        QImage sample;
    };
    // Copies here, where a failure throws; fills side by side.
    std::vector<Fill> work;
    m_dirtyDocumentRect = std::nullopt;
    for (qint64 key : changed) {
        Tile &tile = m_tiles.at(key);
        tile.context = tile.base;
        tile.context.detach();
        if (tile.context.isNull())
            throw ExportError(ExportError::Kind::render);
        work.push_back({&tile, &m_coverage.at(key), selectionCoverage(tile), clonePixels(tile)});
        const QRectF rect = pixelToDocument.mapRect(tile.rect).intersected(canvas);
        m_dirtyDocumentRect = m_dirtyDocumentRect ? std::optional(m_dirtyDocumentRect->united(rect)) : std::optional(rect);
    }
    // Healing shows Swift's dark wash until heal() rebuilds the spot.
    const bool healing = settings.healing && !isMask;
    const QPainter::CompositionMode mode = !healing && settings.erasing && !isMask ? QPainter::CompositionMode_DestinationOut : QPainter::CompositionMode_SourceOver;
    const QColor colour = healing ? QColor::fromRgbF(0.12, 0.12, 0.12) : m_paintColor;
    const double alpha = healing ? 0.45 : settings.opacity;
    PoolMap::blocking(work, [&](const Fill &item) {
        Tile &tile = *item.tile;
        if (clone && (!isMask || isBlur))
            fillClone(item.sample, *item.coverage, settings.opacity, tile.context, item.clip, replacesWithClone);
        else
            BrushRaster::fill(colour, *item.coverage, QRectF(QPointF(0, 0), tile.rect.size()), alpha, tile.context, mode, item.clip);
        tile.image = tile.context;
    });
}

// Swift clips the context; the fill takes the selection instead.
QImage BrushStroke::selectionCoverage(const Tile &tile) const
{
    if (!selectionClip)
        return QImage();
    return PixelAdjust::coverage(*selectionClip, int(tile.rect.width()), int(tile.rect.height()),
                                 QTransform::fromTranslate(tile.rect.left(), tile.rect.top()) * pixelToDocument);
}

// The clone as this tile sees it, before the fill.
QImage BrushStroke::clonePixels(const Tile &tile) const
{
    if (!clone)
        return QImage();
    const int width = int(tile.rect.width()), height = int(tile.rect.height());
    // Tile pixels to clone pixels: grid, document, then offset.
    const QTransform toClone = QTransform::fromTranslate(tile.rect.left(), tile.rect.top()) * pixelToDocument
        * QTransform::fromTranslate(clone->offset.width(), clone->offset.height());
    // Colour even for gray clones: edges carry their coverage.
    const QImage &image = clone->image;
    if (toClone.type() > QTransform::TxTranslate) {
        // Swift's medium quality: bilinear under a scale or turn.
        QImage sample = BrushRaster::context(width, height, false);
        QPainter painter(&sample);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setTransform(toClone.inverted());
        painter.drawImage(QRectF(QPointF(0, 0), image.size()), image);
        painter.end();
        return sample;
    }
    // QPainter snaps a translation; a fraction is resampled by hand.
    const double left = std::floor(toClone.dx()), top = std::floor(toClone.dy());
    const bool whole = left == toClone.dx() && top == toClone.dy();
    QImage sample = BrushRaster::context(whole ? width : width + 1, whole ? height : height + 1, false);
    QPainter painter(&sample);
    BrushRaster::draw(image, QRectF(-left, -top, image.width(), image.height()), painter);
    painter.end();
    if (whole)
        return sample;
    const QImage part = BrushRaster::shiftedByFraction(sample, 1 - (toClone.dx() - left), 1 - (toClone.dy() - top)).copy(1, 1, width, height);
    if (part.isNull())
        throw ExportError(ExportError::Kind::render);
    return part;
}

void BrushStroke::allocateTile(qint64 key, qint64 x, qint64 y)
{
    if (m_tiles.contains(key))
        return;
    const QRectF rect(x * tileSize, y * tileSize, std::min<qint64>(tileSize, width - x * tileSize), std::min<qint64>(tileSize, height - y * tileSize));
    const QRectF nextBounds = m_allocatedBounds ? m_allocatedBounds->united(rect) : m_source ? sourceRect.united(rect) : rect;
    if (nextBounds.width() > DocumentLimits::maxSide || nextBounds.height() > DocumentLimits::maxSide || nextBounds.width() * nextBounds.height() > pixelLimit)
        throw ProjectError(ProjectError::Kind::tooLarge);
    m_allocatedBounds = nextBounds;
    QImage context = BrushRaster::context(int(rect.width()), int(rect.height()), isMask);
    // Past the mask's pixels a tile starts as its background.
    if (isMask)
        context.fill(qRound(maskBackground * 255));
    QPainter painter(&context);
    const QRectF initialRect = sourceRect.translated(-rect.topLeft());
    if (m_source && m_source->raster) {
        m_source->raster->draw(initialRect, painter);
    } else if (m_source) {
        // This tile's share of the source; a 1×1 stretches.
        const QImage source = m_source->image();
        const QRectF overlap = rect.intersected(sourceRect);
        if (source.width() > 2 && source.height() > 2 && !overlap.isEmpty()) {
            const double scaleX = source.width() / sourceRect.width(), scaleY = source.height() / sourceRect.height();
            const QRectF crop = integral(QRectF((overlap.left() - sourceRect.left()) * scaleX, (overlap.top() - sourceRect.top()) * scaleY,
                                                overlap.width() * scaleX, overlap.height() * scaleY));
            BrushRaster::draw(source, overlap.translated(-rect.topLeft()), painter, crop);
        } else {
            BrushRaster::draw(source, initialRect, painter);
        }
    }
    painter.end();
    m_tiles.emplace(key, Tile{rect, context, std::nullopt, context});
}

std::vector<BrushPatch> BrushStroke::patches() const
{
    std::vector<BrushPatch> result;
    for (const auto &[key, tile] : m_tiles) {
        if (tile.image)
            result.push_back({tile.rect, *tile.image});
    }
    return result;
}

// Redrawing restarts from each tile's original content.
void BrushStroke::paintCanvas(bool withinSource, const std::function<void(QPainter &)> &draw)
{
    QRectF area = canvas;
    if (selectionClip)
        area = area.intersected(selectionClip->rect);
    if (area.isEmpty())
        return;
    const QTransform inverse = pixelToDocument.inverted();
    QRectF affected = integral(inverse.mapRect(area)).intersected(QRectF(0, 0, width, height));
    if (withinSource)
        affected = affected.intersected(sourceRect);
    if (affected.isEmpty())
        return;
    eachTile(affected, width, [&](qint64 key, qint64 x, qint64 y) {
        allocateTile(key, x, y);
        Tile &tile = m_tiles.at(key);
        QImage drawn = m_source ? tile.base : BrushRaster::context(int(tile.rect.width()), int(tile.rect.height()), isMask);
        drawn.detach();
        if (drawn.isNull())
            throw ExportError(ExportError::Kind::render);
        QPainter painter(&drawn);
        painter.setTransform(inverse * QTransform::fromTranslate(-tile.rect.left(), -tile.rect.top()));
        painter.setClipRect(canvas);
        draw(painter);
        painter.end();
        // Swift clips to the selection; the twin mixes through it.
        if (selectionClip)
            drawn = PixelAdjust::blend(drawn, tile.base, *selectionClip, QTransform::fromTranslate(tile.rect.left(), tile.rect.top()) * pixelToDocument, isMask);
        tile.context = drawn;
        tile.image = drawn;
    });
    m_dirtyDocumentRect = canvas;
}

void BrushStroke::fillGradient(GradientShape shape, QPointF start, QPointF end, const std::array<QColor, 2> &colors, double opacity)
{
    // Padded both ways, as Swift's before and after locations.
    QGradient gradient = shape == GradientShape::linear
                             ? QGradient(QLinearGradient(start, end))
                             : QGradient(QRadialGradient(start, std::hypot(end.x() - start.x(), end.y() - start.y())));
    gradient.setColorAt(0, colors[0]);
    gradient.setColorAt(1, colors[1]);
    paintCanvas(false, [&](QPainter &painter) {
        painter.setOpacity(std::min(1.0, std::max(0.0, opacity)));
        painter.fillRect(canvas, gradient);
    });
}

void BrushStroke::fill(const QColor &color)
{
    paintCanvas(false, [&](QPainter &painter) { painter.fillRect(canvas, color); });
}

void BrushStroke::clearPixels()
{
    paintCanvas(true, [&](QPainter &painter) {
        painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        painter.fillRect(canvas, Qt::black);
    });
}

// Cuts the selected pixels from the source; false with none.
bool BrushStroke::liftSelection()
{
    if (isMask || !m_source || !selectionClip || !selectionClip->coverage)
        return false;
    const QRectF region = integral(pixelToDocument.inverted().mapRect(selectionClip->rect)).intersected(sourceRect);
    if (region.width() < 1 || region.height() < 1)
        return false;
    QImage lifted = BrushRaster::context(int(region.width()), int(region.height()), false);
    QPainter painter(&lifted);
    painter.translate(-region.left(), -region.top());
    if (m_source->raster)
        m_source->raster->draw(sourceRect, painter);
    else
        BrushRaster::draw(m_source->image(), sourceRect, painter);
    painter.end();
    // Swift clips the context; the selection's coverage multiplies in.
    const QImage clip = PixelAdjust::coverage(*selectionClip, lifted.width(), lifted.height(), QTransform::fromTranslate(region.left(), region.top()) * pixelToDocument);
    QPainter multiplying(&lifted);
    multiplying.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    multiplying.drawImage(QRectF(lifted.rect()), BrushRaster::alphaView(clip));
    multiplying.end();
    m_lifted = Lifted{lifted, region};
    return true;
}

// Tiles anew: a hole, and the pixels `offset` away.
void BrushStroke::moveLifted(QSizeF offset, bool duplicate)
{
    if (!m_lifted || !selectionClip)
        return;
    const QTransform inverse = pixelToDocument.inverted();
    const QPointF zero = inverse.map(QPointF(0, 0)), moved = inverse.map(QPointF(offset.width(), offset.height()));
    const QRectF target = m_lifted->rect.translated(moved.x() - zero.x(), moved.y() - zero.y());
    const bool whole = target.left() == std::round(target.left()) && target.top() == std::round(target.top());
    // QPainter snaps a fraction; Swift's high quality resamples it.
    const QPointF place(std::floor(target.left()), std::floor(target.top()));
    const QImage lifted = whole ? m_lifted->image : BrushRaster::shiftedByFraction(m_lifted->image, target.left() - place.x(), target.top() - place.y());
    const QRectF needed = integral(m_lifted->rect.united(target)).intersected(QRectF(0, 0, width, height));
    std::set<qint64> keys = m_moveTiles;
    eachTile(needed, width, [&](qint64 key, qint64 x, qint64 y) {
        allocateTile(key, x, y);
        keys.insert(key);
    });
    for (qint64 key : keys) {
        Tile &tile = m_tiles.at(key);
        QImage drawn = tile.base;
        drawn.detach();
        if (drawn.isNull())
            throw ExportError(ExportError::Kind::render);
        QPainter painter(&drawn);
        if (!duplicate) {
            const QImage clip = PixelAdjust::coverage(*selectionClip, drawn.width(), drawn.height(),
                                                      QTransform::fromTranslate(tile.rect.left(), tile.rect.top()) * pixelToDocument);
            painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
            painter.drawImage(QRectF(drawn.rect()), BrushRaster::alphaView(clip));
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }
        painter.drawImage(QRectF(place - tile.rect.topLeft(), lifted.size()), lifted);
        painter.end();
        tile.context = drawn;
        tile.image = drawn;
    }
    m_moveTiles = keys;
    m_dirtyDocumentRect = canvas;
}
