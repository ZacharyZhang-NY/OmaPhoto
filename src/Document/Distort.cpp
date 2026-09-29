#include "Document/Distort.h"
#include "Document/DocumentLimits.h"
#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "Document/LayerEffects+Renderer.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore.h"
#include <QPainterPath>
#include <QPolygonF>
#include <QRegion>
#include <cmath>
#include <functional>

extern "C" {
#include "BrushPixels.h"
}

namespace {
// Swift divides by w whatever its sign; Qt 6.10 clamps.
QPointF project(const QTransform &map, QPointF point)
{
    const double w = map.m13() * point.x() + map.m23() * point.y() + map.m33();
    return QPointF((map.m11() * point.x() + map.m21() * point.y() + map.m31()) / w, (map.m12() * point.x() + map.m22() * point.y() + map.m32()) / w);
}

double area(QPointF a, QPointF b, QPointF c)
{
    return (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
}

using Triangle = std::array<QPointF, 3>;

// The affine map taking three points to three others.
std::optional<QTransform> affine(const Triangle &source, const Triangle &target)
{
    const QPointF u = source[1] - source[0], v = source[2] - source[0];
    const QPointF uu = target[1] - target[0], vv = target[2] - target[0];
    const double det = u.x() * v.y() - v.x() * u.y();
    if (std::abs(det) <= 1e-9)
        return std::nullopt;
    const double a = (uu.x() * v.y() - vv.x() * u.y()) / det, c = (vv.x() * u.x() - uu.x() * v.x()) / det;
    const double b = (uu.y() * v.y() - vv.y() * u.y()) / det, d = (vv.y() * u.x() - uu.y() * v.x()) / det;
    return QTransform(a, b, c, d, target[0].x() - (a * source[0].x() + c * source[0].y()), target[0].y() - (b * source[0].x() + d * source[0].y()));
}

struct Target {
    QPointF topLeft, topRight, bottomRight, bottomLeft;
};

// A flipped layer's pixels go to the opposite corners.
Target imageCorners(const Corners &corners, bool flipX, bool flipY)
{
    const auto corner = [&](int x, int y) {
        const int u = flipX ? 1 - x : x, v = flipY ? 1 - y : y;
        return corners[size_t(std::array<int, 4>{0, 1, 3, 2}[size_t(v * 2 + u)])];
    };
    return {corner(0, 0), corner(1, 0), corner(1, 1), corner(0, 1)};
}

QRectF shapeBounds(const Corners &corners)
{
    QPointF low = corners[0], high = corners[0];
    for (const QPointF corner : corners) {
        low = {std::min(low.x(), corner.x()), std::min(low.y(), corner.y())};
        high = {std::max(high.x(), corner.x()), std::max(high.y(), corner.y())};
    }
    return QRectF(QPointF(std::floor(low.x()), std::floor(low.y())), QPointF(std::ceil(high.x()), std::ceil(high.y())));
}

// Pixels whose centres a triangle holds, left bound inclusive.
QRegion triangleRegion(const Triangle &triangle)
{
    QRegion region;
    const double top = std::min({triangle[0].y(), triangle[1].y(), triangle[2].y()});
    const double bottom = std::max({triangle[0].y(), triangle[1].y(), triangle[2].y()});
    for (int row = int(std::ceil(top - 0.5)); row + 0.5 < bottom; ++row) {
        const double y = row + 0.5;
        std::optional<double> left, right;
        for (size_t index = 0; index < 3; ++index) {
            // Ends ordered by y: a shared edge gives one x.
            const QPointF a = std::min(triangle[index], triangle[(index + 1) % 3], [](QPointF p, QPointF q) { return p.y() < q.y(); });
            const QPointF b = a == triangle[index] ? triangle[(index + 1) % 3] : triangle[index];
            // An edge holds its lower end, not its upper one.
            if (a.y() == b.y() || y < a.y() || y >= b.y())
                continue;
            const double x = a.x() + (b.x() - a.x()) * (y - a.y()) / (b.y() - a.y());
            left = std::min(left.value_or(x), x);
            right = std::max(right.value_or(x), x);
        }
        if (!left)
            continue;
        const int first = int(std::ceil(*left - 0.5)), last = int(std::ceil(*right - 0.5)) - 1;
        if (last >= first)
            region += QRect(first, row, last - first + 1, 1);
    }
    return region;
}

// A folded shape: each half by its own affine map.
void warpFolded(const QImage &image, const Target &target, const std::function<QPointF(QPointF)> &place, QPainter &painter)
{
    const QRectF source(0, 0, image.width(), image.height());
    const std::array<std::pair<Triangle, Triangle>, 2> halves = {
        std::pair{Triangle{source.topLeft(), source.topRight(), source.bottomRight()}, Triangle{place(target.topLeft), place(target.topRight), place(target.bottomRight)}},
        std::pair{Triangle{source.topLeft(), source.bottomRight(), source.bottomLeft()}, Triangle{place(target.topLeft), place(target.bottomRight), place(target.bottomLeft)}}};
    for (const auto &[from, to] : halves) {
        const std::optional<QTransform> map = affine(from, to);
        if (!map)
            continue;
        painter.save();
        // A hard region along the diagonal: the halves meet exactly.
        painter.setClipRegion(triangleRegion(to));
        painter.setTransform(*map);
        BrushRaster::draw(image, source, painter);
        painter.restore();
    }
}
}

Corners DistortWarp::corners(const LayerTransform &transform)
{
    return {transform.point({0, 0}), transform.point({1, 0}), transform.point({1, 1}), transform.point({0, 1})};
}

bool DistortWarp::isUsable(const Corners &corners)
{
    for (const QPointF corner : corners) {
        if (!std::isfinite(corner.x()) || !std::isfinite(corner.y()) || std::abs(corner.x()) > 1'000'000 || std::abs(corner.y()) > 1'000'000)
            return false;
    }
    // Both halves need area, or one has nothing to draw.
    return std::abs(area(corners[0], corners[1], corners[2])) > 0.01 && std::abs(area(corners[0], corners[2], corners[3])) > 0.01;
}

bool DistortWarp::isConvex(const Corners &corners)
{
    if (!isUsable(corners))
        return false;
    double sign = 0;
    for (size_t index = 0; index < 4; ++index) {
        const QPointF a = corners[index], b = corners[(index + 1) % 4], c = corners[(index + 2) % 4];
        const double cross = (b.x() - a.x()) * (c.y() - b.y()) - (b.y() - a.y()) * (c.x() - b.x());
        if (std::abs(cross) <= 0.01)
            return false;
        if (sign == 0)
            sign = cross < 0 ? -1 : 1;
        else if ((cross < 0) != (sign < 0))
            return false;
    }
    return true;
}

QTransform DistortWarp::homography(const Corners &c)
{
    const double sx = c[0].x() - c[1].x() + c[2].x() - c[3].x(), sy = c[0].y() - c[1].y() + c[2].y() - c[3].y();
    double g = 0, h = 0;
    if (std::abs(sx) > 1e-9 || std::abs(sy) > 1e-9) {
        const double dx1 = c[1].x() - c[2].x(), dx2 = c[3].x() - c[2].x(), dy1 = c[1].y() - c[2].y(), dy2 = c[3].y() - c[2].y();
        const double den = dx1 * dy2 - dx2 * dy1;
        if (std::abs(den) > 1e-12) {
            g = (sx * dy2 - dx2 * sy) / den;
            h = (dx1 * sy - sx * dy1) / den;
        }
    }
    const double a = c[1].x() - c[0].x() + g * c[1].x(), b = c[3].x() - c[0].x() + h * c[3].x();
    const double d = c[1].y() - c[0].y() + g * c[1].y(), e = c[3].y() - c[0].y() + h * c[3].y();
    // Qt divides by the third row, as Swift by w.
    return QTransform(a, d, g, b, e, h, c[0].x(), c[0].y(), 1);
}

DistortWarp::Warped DistortWarp::warp(const QImage &image, const LayerTransform &transform, const Corners &corners, bool isMask,
                                      std::optional<double> limit)
{
    if (!isUsable(corners))
        throw ProjectError(ProjectError::Kind::invalid);
    const QRectF bounds = shapeBounds(corners);
    if (bounds.width() < 1 || bounds.height() < 1 || bounds.width() > DocumentLimits::maxSide || bounds.height() > DocumentLimits::maxSide || bounds.width() * bounds.height() > DocumentLimits::maxSurfacePixels)
        throw ProjectError(ProjectError::Kind::tooLarge);
    const LayerTransform placed{.origin = bounds.topLeft(), .size = bounds.size(), .sampling = transform.sampling};
    // A uniform 1 × 1 mask already covers any shape.
    if (isMask && image.width() == 1 && image.height() == 1)
        return {image, placed};
    const double factor = limit ? std::min(1.0, *limit / std::max(bounds.width(), bounds.height())) : 1;
    const int width = std::max(1, int(std::ceil(bounds.width() * factor))), height = std::max(1, int(std::ceil(bounds.height() * factor)));
    const Target target = imageCorners(corners, transform.flipX, transform.flipY);
    const auto place = [&](QPointF point) { return QPointF((point.x() - bounds.left()) * factor, (point.y() - bounds.top()) * factor); };
    QImage surface = BrushRaster::context(width, height, isMask);
    QPainter painter(&surface);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    if (!isConvex(corners)) {
        warpFolded(image, target, place, painter);
        return {surface, placed};
    }
    const QRectF source(0, 0, image.width(), image.height());
    QTransform map;
    if (!QTransform::quadToQuad(QPolygonF({source.topLeft(), source.topRight(), source.bottomRight(), source.bottomLeft()}),
                                QPolygonF({place(target.topLeft), place(target.topRight), place(target.bottomRight), place(target.bottomLeft)}), map))
        throw ExportError(ExportError::Kind::render);
    painter.setTransform(map);
    painter.drawImage(source, image);
    return {surface, placed};
}

DistortWarp::Trimmed DistortWarp::warpTrimmed(const QImage &image, const LayerTransform &transform, const Corners &corners)
{
    const Warped warped = warp(image, transform, corners, false);
    const QRect full = warped.image.rect();
    size_t edges[4] = {0, 0, 0, 0};
    brush_alpha_bounds(warped.image.constBits(), size_t(full.width()), size_t(full.height()), size_t(warped.image.bytesPerLine()), edges);
    const QRect crop(int(edges[0]), int(edges[1]), int(edges[2] - edges[0]), int(edges[3] - edges[1]));
    // Nothing visible, or nothing to trim: the warp itself.
    if (crop.width() < 1 || crop.height() < 1 || crop == full)
        return {warped.image, warped.transform, full};
    LayerTransform placed = warped.transform;
    placed.origin += crop.topLeft();
    placed.size = crop.size();
    return {warped.image.copy(crop), placed, crop};
}

Corners DistortWarp::carried(const LayerTransform &placement, const LayerTransform &transform, const Corners &corners)
{
    QTransform toDocument;
    toDocument.translate(transform.center().x(), transform.center().y());
    toDocument.rotateRadians(transform.radians());
    toDocument.scale(transform.size.width(), transform.size.height());
    toDocument.translate(-0.5, -0.5);
    const QTransform toUnit = toDocument.inverted();
    const QTransform map = homography(corners);
    const Corners own = DistortWarp::corners(placement);
    Corners result;
    for (size_t index = 0; index < 4; ++index)
        result[index] = project(map, toUnit.map(own[index]));
    return result;
}

DistortWarp::Warped DistortWarp::warpMask(const QImage &image, const LayerTransform &transform, const Corners &corners, double background,
                                          std::optional<double> limit)
{
    const Warped warped = warp(image, transform, corners, true, limit);
    if (background <= 0 || warped.image.cacheKey() == image.cacheKey())
        return warped;
    const int width = warped.image.width(), height = warped.image.height();
    QImage surface = BrushRaster::context(width, height, true);
    surface.fill(qRound(background * 255));
    QPainter painter(&surface);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const double sx = width / warped.transform.size.width(), sy = height / warped.transform.size.height();
    QPainterPath shape;
    for (size_t index = 0; index < 4; ++index) {
        const QPointF point((corners[index].x() - warped.transform.origin.x()) * sx, (corners[index].y() - warped.transform.origin.y()) * sy);
        if (index == 0)
            shape.moveTo(point);
        else
            shape.lineTo(point);
    }
    shape.closeSubpath();
    painter.setClipPath(shape);
    BrushRaster::draw(warped.image, QRectF(0, 0, width, height), painter);
    return {surface, warped.transform};
}

// Ctrl-drag on a handle: the corners move freely; Apply resamples.
void EditorSession::beginDistort()
{
    if (!m_transformEdit || m_transformEdit->corners || !m_transformEdit->draft.isValid())
        return;
    m_transformEdit->corners = DistortWarp::corners(m_transformEdit->draft);
    m_transformEdit->persistent = true;
    notify();
}

// A twisted or collapsed shape is ignored.
void EditorSession::previewCorners(const Corners &corners)
{
    if (!m_transformEdit || !m_transformEdit->corners || !DistortWarp::isUsable(corners))
        return;
    m_transformEdit->corners = corners;
    notify();
}

// A group's layers take the box's perspective each.
std::optional<DistortTarget> EditorSession::distortTarget(const ImageLayer &layer, const TransformEdit &edit, const Corners &shape) const
{
    if (!edit.group)
        return edit.layerID == layer.id ? std::optional(DistortTarget{edit.draft, shape}) : std::nullopt;
    const auto original = edit.group->originals.constFind(layer.id);
    if (original == edit.group->originals.constEnd())
        return std::nullopt;
    const LayerTransform transform = original->following(edit.group->box, edit.draft);
    const Corners corners = DistortWarp::carried(transform, edit.draft, shape);
    return DistortWarp::isUsable(corners) ? std::optional(DistortTarget{transform, corners}) : std::nullopt;
}

// `image` is the layer with effects round it, mask applied.
std::optional<DistortWarp::Warped> EditorSession::distortedEffects(const ImageLayer &layer, const QImage &image, double inset) const
{
    if (!m_transformEdit || m_transformEdit->mask || !m_transformEdit->corners)
        return std::nullopt;
    const std::optional<DistortTarget> target = distortTarget(layer, *m_transformEdit, *m_transformEdit->corners);
    if (!target)
        return std::nullopt;
    return distortedEffects(layer, image, inset, *target);
}

std::optional<DistortWarp::Warped> EditorSession::distortedEffects(const ImageLayer &layer, const QImage &image, double inset,
                                                                   const DistortTarget &target) const
{
    // The grown box takes the same perspective as the layer's.
    const LayerTransform grown = LayerEffectsRenderer::placed(target.transform, image, inset);
    const Corners carried = DistortWarp::carried(grown, target.transform, target.corners);
    if (m_distortEffectsCache.contains(layer.id)) {
        const DistortEffectsCache &cache = m_distortEffectsCache.at(layer.id);
        if (cache.corners == carried && cache.image.cacheKey() == image.cacheKey())
            return cache.result;
    }
    std::optional<DistortWarp::Warped> result;
    // Swift's try?: effects that cannot warp are left off.
    try {
        result = DistortWarp::warp(image, grown, carried, false, 2048);
    } catch (const ProjectError &) {
    } catch (const ExportError &) {
    }
    m_distortEffectsCache.insert_or_assign(layer.id, DistortEffectsCache{carried, image, result});
    return result;
}

// The layer warped into the pending distortion, at preview size.
std::optional<DistortPreview> EditorSession::distortPreview(const ImageLayer &layer) const
{
    if (!m_transformEdit || m_transformEdit->mask || !m_transformEdit->corners || !layer.asset)
        return std::nullopt;
    const std::optional<DistortTarget> target = distortTarget(layer, *m_transformEdit, *m_transformEdit->corners);
    if (!target)
        return std::nullopt;
    const std::optional<QImage> mask = layer.mask ? layer.mask->enabledImage() : std::nullopt;
    const std::optional<ImageIdentity> maskID = mask ? std::optional(layer.mask->asset.identity()) : std::nullopt;
    const auto cached = m_distortPreviewCache.find(layer.id);
    if (cached != m_distortPreviewCache.end() && cached->second.corners == target->corners && cached->second.draft == target->transform
        && cached->second.image == layer.asset->identity() && cached->second.mask == maskID)
        return cached->second.result;
    std::optional<DistortPreview> result;
    try {
        const DistortWarp::Warped warped = DistortWarp::warp(layer.asset->image(), target->transform, target->corners, false, 2048);
        std::optional<QImage> warpedMask;
        const std::optional<LayerMask> &owned = layer.mask;
        if (owned && !owned->placement && owned->isLinked) {
            // Swift's try?: a mask that cannot warp draws none.
            try {
                if (mask)
                    warpedMask = DistortWarp::warp(*mask, target->transform, target->corners, true, 2048).image;
            } catch (const ProjectError &) {
            } catch (const ExportError &) {
            }
        } else if (owned) {
            std::optional<QImage> carriedMask;
            if (owned->isLinked && owned->placement) {
                // A linked mask placed apart takes the same perspective.
                const LayerTransform placement = owned->placement->following(layer.transform, target->transform);
                const Corners carried = DistortWarp::carried(placement, target->transform, target->corners);
                try {
                    if (DistortWarp::isConvex(carried)) {
                        const DistortWarp::Warped moved = DistortWarp::warpMask(owned->asset.image(), placement, carried, LayerMask::background(owned->asset.thumbnail), 2048);
                        carriedMask = LayerMask(ImportedImage(moved.image, owned->asset.thumbnail, owned->asset.name), owned->isEnabled)
                                          .clipImage(moved.transform, warped.transform, warped.image.width(), warped.image.height(), 2048);
                    }
                } catch (const ProjectError &) {
                } catch (const ExportError &) {
                }
            }
            // Otherwise the mask stays where it is on the document.
            warpedMask = carriedMask ? carriedMask : owned->clipImage(owned->placement.value_or(layer.transform), warped.transform, warped.image.width(), warped.image.height(), 2048);
        }
        result = DistortPreview{warped.image, warpedMask, warped.transform};
    } catch (const ProjectError &) {
    } catch (const ExportError &) {
    }
    m_distortPreviewCache.insert_or_assign(layer.id, DistortPreviewCache{target->corners, target->transform, layer.asset->identity(), maskID, result});
    return result;
}

// Apply: each layer resampled into its shape, one step.
void EditorSession::commitDistort(const TransformEdit &edit, const Corners &shape)
{
    m_distortPreviewCache.clear();
    std::vector<QUuid> ids;
    if (edit.group) {
        for (auto original = edit.group->originals.constBegin(); original != edit.group->originals.constEnd(); ++original)
            ids.push_back(original.key());
    } else {
        ids.push_back(edit.layerID);
    }
    beginEdit(edit.group ? QStringLiteral("Distort Layers") : QStringLiteral("Distort"));
    for (const QUuid &id : ids) {
        const int index = indexOf(m_document->layers, id);
        if (index < 0)
            continue;
        const std::optional<DistortTarget> target = distortTarget(m_document->layers[index], edit, shape);
        if (!target)
            continue;
        // Warped effects stay on until the new pixels' effects land.
        const std::optional<EffectsPreviewCache::Result> effects = effectsPreviews.rendered(id);
        const std::optional<DistortWarp::Warped> warpedEffects =
            effects ? distortedEffects(m_document->layers[index], effects->image, effects->inset, *target) : std::nullopt;
        try {
            distort(index, target->transform, target->corners);
        } catch (const ProjectError &error) {
            setBrushError(QString::fromUtf8(error.what()));
        } catch (const ExportError &error) {
            setBrushError(QString::fromUtf8(error.what()));
        }
        // Placed where warped: the commit also crops the layer.
        if (warpedEffects)
            effectsPreviews.seed(id, warpedEffects->image, warpedEffects->transform);
    }
    endEdit();
    m_distortEffectsCache.clear();
}

// The layer at `index` resampled onto `corners`.
void EditorSession::distort(int index, const LayerTransform &transform, const Corners &corners)
{
    const ImageLayer layer = m_document->layers[index];
    if (!layer.asset)
        return;
    const DistortWarp::Trimmed warped = DistortWarp::warpTrimmed(layer.asset->image(), transform, corners);
    const ImportedImage asset(warped.image, PixelAdjust::thumbnail(warped.image), layer.name);
    std::optional<LayerMask> mask = layer.mask;
    if (layer.mask && !layer.mask->placement && layer.mask->isLinked) {
        const DistortWarp::Warped warpedMask = DistortWarp::warp(layer.mask->asset.image(), transform, corners, true);
        // A uniform mask passes through; others crop with the pixels.
        const bool uniform = warpedMask.image.cacheKey() == layer.mask->asset.image().cacheKey();
        mask = layer.mask->replacing(uniform ? layer.mask->asset : LayerMask::assetFrom(warpedMask.image.copy(warped.crop)));
    } else if (layer.mask && layer.mask->isLinked && layer.mask->placement
               && DistortWarp::isConvex(DistortWarp::carried(layer.mask->placement->following(layer.transform, transform), transform, corners))) {
        // A linked mask placed apart takes the same perspective.
        const LayerTransform placement = layer.mask->placement->following(layer.transform, transform);
        const DistortWarp::Warped moved = DistortWarp::warpMask(layer.mask->asset.image(), placement, DistortWarp::carried(placement, transform, corners),
                                                                LayerMask::background(layer.mask->asset.thumbnail));
        const bool uniform = moved.image.cacheKey() == layer.mask->asset.image().cacheKey();
        mask = LayerMask(uniform ? layer.mask->asset : LayerMask::assetFrom(moved.image), layer.mask->isEnabled, moved.transform, true);
    } else if (layer.mask) {
        // An unlinked mask keeps its place on the document.
        mask->placement = layer.mask->placement.value_or(layer.transform);
    }
    m_document->layers[index].asset = asset;
    m_document->layers[index].transform = warped.transform;
    m_document->layers[index].mask = mask;
}

// The selection's outline carried through the distortion.
std::optional<QPainterPath> DistortWarp::mapPath(const QPainterPath &path, const QTransform &pixelToDocument, QSizeF pixelSize,
                                                 const LayerTransform &transform, const Corners &corners)
{
    if (!isConvex(corners) || pixelSize.width() <= 0 || pixelSize.height() <= 0)
        return std::nullopt;
    const QTransform toPixels = pixelToDocument.inverted();
    const QTransform map = homography(corners);
    const auto carry = [&](QPointF point) {
        const QPointF pixel = toPixels.map(point);
        double u = pixel.x() / pixelSize.width(), v = pixel.y() / pixelSize.height();
        if (transform.flipX)
            u = 1 - u;
        if (transform.flipY)
            v = 1 - v;
        return project(map, QPointF(u, v));
    };
    QPainterPath result;
    result.setFillRule(path.fillRule());
    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element element = path.elementAt(i);
        if (element.isMoveTo()) {
            result.moveTo(carry(element));
        } else if (element.isLineTo()) {
            result.lineTo(carry(element));
        } else {
            // A curve is its first control, then two data elements.
            result.cubicTo(carry(element), carry(path.elementAt(i + 1)), carry(path.elementAt(i + 2)));
            i += 2;
        }
    }
    return result;
}
