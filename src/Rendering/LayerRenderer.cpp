#include "Rendering/LayerRenderer.h"
#include "Document/BrushStroke.h"
#include "IO/ImageExporter.h"
#include "Rendering/DownsampleCache.h"
#include "Rendering/HslBlend.h"
#include "Rendering/RasterSnapshot.h"
#include <QPainterPath>
#include <cmath>
#include <stdexcept>

namespace {
QPainter::CompositionMode compositionMode(LayerBlendMode mode)
{
    switch (mode) {
    case LayerBlendMode::normal:
        return QPainter::CompositionMode_SourceOver;
    case LayerBlendMode::multiply:
        return QPainter::CompositionMode_Multiply;
    case LayerBlendMode::screen:
        return QPainter::CompositionMode_Screen;
    case LayerBlendMode::overlay:
        return QPainter::CompositionMode_Overlay;
    case LayerBlendMode::darken:
        return QPainter::CompositionMode_Darken;
    case LayerBlendMode::lighten:
        return QPainter::CompositionMode_Lighten;
    case LayerBlendMode::difference:
        return QPainter::CompositionMode_Difference;
    case LayerBlendMode::colorDodge:
        return QPainter::CompositionMode_ColorDodge;
    case LayerBlendMode::colorBurn:
        return QPainter::CompositionMode_ColorBurn;
    case LayerBlendMode::hardLight:
        return QPainter::CompositionMode_HardLight;
    case LayerBlendMode::exclusion:
        return QPainter::CompositionMode_Exclusion;
    default:
        throw std::logic_error("QPainter has no composition mode for this blend mode");
    }
}

void setSampling(QPainter &painter, InterpolationQuality quality, LayerSampling sampling)
{
    painter.setRenderHint(QPainter::SmoothPixmapTransform, quality != InterpolationQuality::none);
    painter.setRenderHint(QPainter::Antialiasing, sampling != LayerSampling::nearest);
}
}

void LayerRenderer::draw(const QImage &image, const LayerTransform &transform, QPointF center, QPainter &context,
                         const Options &options)
{
    const double width = transform.size.width() * options.scale, height = transform.size.height() * options.scale;
    // Large reductions draw from sharp halvings; QPainter does the rest.
    const double device = deviceScale(context);
    const Reduced source = reduced(image, width, device, transform.sampling);
    const InterpolationQuality quality = interpolation(
        transform.sampling, width * device / std::max(1, image.width()) * (1 << source.level), transform.radians() == 0);
    const QRectF bounds(-width / 2, -height / 2, width, height);
    QTransform placement;
    placement.translate(center.x(), center.y());
    placement.rotateRadians(transform.radians());
    placement.scale(transform.flipX ? -1 : 1, transform.flipY ? -1 : 1);

    if (options.mask.isNull() && options.clip.isNull() && !HslBlend::handles(options.blendMode)) {
        const QPainter::CompositionMode mode = compositionMode(options.blendMode);
        context.save();
        context.setOpacity(options.opacity);
        context.setCompositionMode(mode);
        setSampling(context, quality, transform.sampling);
        context.setTransform(placement, true);
        context.drawImage(coverage(source, bounds), source.image);
        context.restore();
        return;
    }

    composite(context, placement, coverage(source, bounds), transform.sampling, quality, options, bounds,
              [&](QPainter &aside) { aside.drawImage(coverage(source, bounds), source.image); });
}

void LayerRenderer::drawBrushPreview(const QImage &image, const LayerTransform &transform, QPointF center, QPainter &context,
                                     const Options &options, const BrushPreview &preview)
{
    if (preview.pixelWidth <= 0 || preview.pixelHeight <= 0)
        throw std::logic_error("a brush preview needs a pixel grid");
    const double width = transform.size.width() * options.scale, height = transform.size.height() * options.scale;
    const QRectF bounds(-width / 2, -height / 2, width, height);
    const auto mapped = [](const QRectF &source, const QRectF &into, double columns, double rows) {
        return QRectF(into.left() + source.left() / columns * into.width(), into.top() + source.top() / rows * into.height(),
                      source.width() / columns * into.width(), source.height() / rows * into.height());
    };
    const auto tile = [&](const BrushPatch &patch) { return mapped(patch.rect, bounds, preview.pixelWidth, preview.pixelHeight); };
    const QRectF originalBounds = preview.sourceRect ? mapped(*preview.sourceRect, bounds, preview.pixelWidth, preview.pixelHeight) : bounds;
    // A raster is a source: naming one never flattens it.
    const bool hasSource = !image.isNull() || preview.raster;
    const auto drawSource = [&](QPainter &aside) {
        if (!preview.raster) {
            aside.drawImage(originalBounds, image);
            return;
        }
        const RasterSnapshot &raster = *preview.raster;
        const QImage &base = preview.rasterBase.isNull() ? raster.base : preview.rasterBase;
        if (!base.isNull()) {
            // A cropped raster's base shows only inside the source bounds.
            aside.save();
            aside.setClipRect(originalBounds, Qt::IntersectClip);
            aside.drawImage(mapped(raster.baseRect, originalBounds, raster.width, raster.height), base);
            aside.restore();
        }
        const QRectF visible = BrushRaster::visibleRect(aside);
        for (const BrushPatch &patch : raster.patches) {
            const QRectF target = mapped(patch.rect, originalBounds, raster.width, raster.height);
            if (target.intersects(visible))
                aside.drawImage(target, patch.image);
        }
    };
    QTransform placement;
    placement.translate(center.x(), center.y());
    placement.rotateRadians(transform.radians());
    placement.scale(transform.flipX ? -1 : 1, transform.flipY ? -1 : 1);
    QRectF extent = bounds;
    for (const BrushPatch &patch : preview.patches)
        extent = extent.united(tile(patch));
    // Tiles replace what lies under them, with hard edges.
    const auto body = [&](QPainter &aside) {
        aside.setCompositionMode(QPainter::CompositionMode_Source);
        if (hasSource) {
            // The source shows inside the grid only.
            aside.save();
            aside.setClipRect(bounds, Qt::IntersectClip);
            drawSource(aside);
            aside.restore();
        }
        for (const BrushPatch &patch : preview.patches) {
            if (!preview.paintingMask)
                aside.drawImage(tile(patch), patch.image);
        }
    };
    // A tile's part outside the old raster; paths fill odd-even.
    const auto outside = [&](const QRectF &rect) {
        QPainterPath path;
        path.addRect(rect);
        if (const QRectF overlap = rect.intersected(originalBounds); !overlap.isEmpty())
            path.addRect(overlap);
        return path;
    };
    const auto veil = [&](QPainter &masking) {
        masking.setCompositionMode(QPainter::CompositionMode_Source);
        for (const BrushPatch &patch : preview.patches) {
            // Painted coverage replaces the mask; paint outside is revealed.
            if (preview.paintingMask)
                masking.drawImage(tile(patch), BrushRaster::alphaView(patch.image));
            else
                masking.fillPath(outside(tile(patch)), Qt::black);
        }
    };
    const bool veiled = preview.paintingMask || !options.mask.isNull();
    // Nearest keeps every edge hard; the quality is the layer's.
    composite(context, placement, extent, LayerSampling::nearest, quality(transform.sampling), options, originalBounds, body,
              veiled ? std::function<void(QPainter &)>(veil) : std::function<void(QPainter &)>());
}

void LayerRenderer::composite(QPainter &context, const QTransform &placement, const QRectF &extent, LayerSampling sampling,
                              InterpolationQuality quality, const Options &options, const QRectF &maskBounds,
                              const std::function<void(QPainter &)> &body, const std::function<void(QPainter &)> &veil)
{
    // Qt has no mask clip: the layer composes aside first.
    const QTransform toDevice = placement * context.deviceTransform();
    const QRect deviceBounds(0, 0, context.device()->width(), context.device()->height());
    const QRect area = toDevice.mapRect(extent).toAlignedRect()
                           .intersected(context.deviceTransform().mapRect(BrushRaster::visibleRect(context)).toAlignedRect())
                           .intersected(deviceBounds);
    if (area.isEmpty())
        return;
    const QTransform toSurface = toDevice * QTransform::fromTranslate(-area.left(), -area.top());
    QImage surface(area.size(), QImage::Format_RGBA8888_Premultiplied);
    if (surface.isNull())
        throw ExportError(ExportError::Kind::render);
    surface.fill(0);
    {
        QPainter aside(&surface);
        setSampling(aside, quality, sampling);
        aside.setTransform(toSurface);
        body(aside);
    }
    if (!options.mask.isNull() || veil) {
        QImage shown(area.size(), QImage::Format_Alpha8);
        if (shown.isNull())
            throw ExportError(ExportError::Kind::render);
        // Nothing outside a mask survives; without one, all shows.
        shown.fill(options.mask.isNull() ? 255 : 0);
        {
            QPainter masking(&shown);
            setSampling(masking, quality, sampling);
            masking.setTransform(toSurface);
            if (!options.mask.isNull()) {
                const Reduced clip = reduced(options.mask, maskBounds.width(), deviceScale(context), sampling);
                masking.drawImage(coverage(clip, maskBounds), BrushRaster::alphaView(clip.image));
            }
            if (veil)
                veil(masking);
        }
        QPainter multiplying(&surface);
        multiplying.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        multiplying.drawImage(QRectF(surface.rect()), shown);
    }
    if (!options.clip.isNull()) {
        if (options.clip.format() != QImage::Format_Alpha8 || options.clip.size() != deviceBounds.size())
            throw std::logic_error("a folder clip must be device-sized alpha coverage");
        QPainter clipping(&surface);
        clipping.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        clipping.drawImage(QRectF(surface.rect()), options.clip, QRectF(area));
    }
    // Everything that can throw happens before the painter changes.
    const bool byHand = HslBlend::handles(options.blendMode);
    QImage composed = surface;
    if (byHand) {
        const QImage *target = dynamic_cast<const QImage *>(context.device());
        if (!target)
            throw std::logic_error("modes blended by hand need an image-backed painter");
        const QImage backdrop = target->copy(area).convertToFormat(QImage::Format_RGBA8888_Premultiplied);
        if (backdrop.isNull())
            throw ExportError(ExportError::Kind::render);
        composed = HslBlend::blend(backdrop, surface, options.blendMode, options.opacity);
    }
    const QPainter::CompositionMode mode = byHand ? QPainter::CompositionMode_Source : compositionMode(options.blendMode);
    context.save();
    context.setWorldTransform(context.deviceTransform().inverted() * context.worldTransform());
    context.setOpacity(byHand ? 1 : options.opacity);
    context.setCompositionMode(mode);
    context.drawImage(QRectF(area), composed);
    context.restore();
}

namespace {
// The mode where alpha-white equals a clip of that coverage.
QPainter::CompositionMode underCoverage(QPainter::CompositionMode mode)
{
    switch (mode) {
    // These erase where the source is clear; clips do not.
    case QPainter::CompositionMode_Source:
    case QPainter::CompositionMode_Plus:
        return QPainter::CompositionMode_SourceOver;
    case QPainter::CompositionMode_Clear:
        return QPainter::CompositionMode_DestinationOut;
    case QPainter::CompositionMode_SourceIn:
        return QPainter::CompositionMode_SourceAtop;
    case QPainter::CompositionMode_SourceOut:
        return QPainter::CompositionMode_Xor;
    case QPainter::CompositionMode_DestinationIn:
        return QPainter::CompositionMode_Destination;
    case QPainter::CompositionMode_DestinationAtop:
        return QPainter::CompositionMode_DestinationOver;
    default:
        // The rest are linear in the source, clear pixels kept.
        if (mode >= QPainter::RasterOp_SourceOrDestination)
            throw std::logic_error("coverage has no meaning under a raster operation");
        return mode;
    }
}
}

void LayerRenderer::drawCoverage(const QImage &image, const LayerTransform &transform, QPainter &context)
{
    if (image.format() != QImage::Format_Grayscale8)
        throw std::logic_error("coverage is drawn from a gray mask");
    const QPainter::CompositionMode mode = underCoverage(context.compositionMode());
    // Opacity is coverage too only where the mode is linear.
    if (mode != context.compositionMode() && context.opacity() < 1)
        throw std::logic_error("this composition mode takes no opacity under coverage");
    const QRectF bounds(-transform.size.width() / 2, -transform.size.height() / 2, transform.size.width(), transform.size.height());
    // CoreGraphics filters a shrinking mask; QPainter needs the halvings.
    const Reduced source = reduced(image, transform.size.width(), deviceScale(context), transform.sampling);
    // White, the mask as its alpha: the painter's mode blends.
    QImage white(source.image.size(), QImage::Format_ARGB32_Premultiplied);
    if (white.isNull())
        throw ExportError(ExportError::Kind::render);
    for (int y = 0; y < white.height(); ++y) {
        const uchar *mask = source.image.constScanLine(y);
        QRgb *pixels = reinterpret_cast<QRgb *>(white.scanLine(y));
        for (int x = 0; x < white.width(); ++x)
            pixels[x] = mask[x] * 0x01010101u;
    }
    QTransform placement;
    placement.translate(transform.center().x(), transform.center().y());
    placement.rotateRadians(transform.radians());
    placement.scale(transform.flipX ? -1 : 1, transform.flipY ? -1 : 1);
    context.save();
    setSampling(context, quality(transform.sampling), transform.sampling);
    context.setCompositionMode(mode);
    context.setTransform(placement, true);
    context.drawImage(coverage(source, bounds), white);
    context.restore();
}

InterpolationQuality LayerRenderer::interpolation(LayerSampling sampling, double finalFactor, bool upright)
{
    if (sampling == LayerSampling::nearest || (upright && std::abs(finalFactor - 1) < 0.001))
        return InterpolationQuality::none;
    return finalFactor <= 1 ? InterpolationQuality::low : quality(sampling);
}

LayerRenderer::Reduced LayerRenderer::reduced(const QImage &image, double width, double device, LayerSampling sampling)
{
    if (sampling == LayerSampling::nearest)
        return {image, 0, 1, 1};
    const int level = DownsampleCache::level(width * device / std::max(1, image.width()));
    const DownsampleCache::Reduced result = DownsampleCache::shared().imageAtLevel(image, level);
    return {result.image, result.level,
            double(result.image.width() << result.level) / std::max(1, image.width()),
            double(result.image.height() << result.level) / std::max(1, image.height())};
}

QRectF LayerRenderer::coverage(const Reduced &reduced, const QRectF &bounds)
{
    return {bounds.left(), bounds.top(), bounds.width() * reduced.widthScale, bounds.height() * reduced.heightScale};
}

double LayerRenderer::deviceScale(const QPainter &context)
{
    const QTransform device = context.deviceTransform();
    return std::hypot(device.m11(), device.m12());
}
