#include "Document/Filters.h"
#include "Document/DocumentLimits.h"
#include "Document/BrushStroke.h"
#include "Document/ContentFill.h"
#include "Document/EditorSession+Model.h"
#include "Document/PixelAdjust.h"
#include "Document/SubjectRemoval.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore.h"
#include "Rendering/RasterSnapshot.h"
#include <algorithm>
#include <cmath>
#include <numbers>

extern "C" {
#include "AdjustPixels.h"
#include "BrushPixels.h"
#include "LensPixels.h"
#include "NoisePixels.h"
}

QString rawValue(FilterKind kind)
{
    switch (kind) {
    case FilterKind::gaussianBlur: return QStringLiteral("Gaussian Blur");
    case FilterKind::motionBlur: return QStringLiteral("Motion Blur");
    case FilterKind::addNoise: return QStringLiteral("Add Noise");
    case FilterKind::vignette: return QStringLiteral("Vignette");
    case FilterKind::bloomGlow: return QStringLiteral("Bloom / Glow");
    case FilterKind::tonalContrast: return QStringLiteral("Tonal Contrast");
    case FilterKind::lensCorrection: return QStringLiteral("Lens Correction");
    case FilterKind::cameraRaw: return QStringLiteral("Camera Raw Filter");
    case FilterKind::removeBackground: return QStringLiteral("Remove Background");
    case FilterKind::contentAwareFill: return QStringLiteral("Content-Aware Fill");
    case FilterKind::curves: return QStringLiteral("Curves");
    case FilterKind::exposure: return QStringLiteral("Exposure");
    case FilterKind::gradientMap: return QStringLiteral("Gradient Map");
    case FilterKind::grain: return QStringLiteral("Grain");
    case FilterKind::blackWhite: return QStringLiteral("Black & White");
    case FilterKind::colorBalance: return QStringLiteral("Color Balance");
    }
    throw std::logic_error("unknown filter kind");
}

bool isAutomatic(FilterKind kind)
{
    return kind == FilterKind::contentAwareFill || kind == FilterKind::removeBackground;
}

QString rawValue(BackgroundQuality quality)
{
    return quality == BackgroundQuality::basic ? QStringLiteral("Basic") : QStringLiteral("Advanced");
}

bool isImageAdjustment(FilterKind kind)
{
    return kind == FilterKind::curves || kind == FilterKind::exposure || kind == FilterKind::gradientMap || kind == FilterKind::grain
        || kind == FilterKind::blackWhite || kind == FilterKind::colorBalance;
}

FilterSettings FilterSettings::normalized() const
{
    using ImageAdjustmentPixels::clamp;
    FilterSettings result = *this;
    result.radius = clamp(radius, 0.1, 250, 1);
    result.angle = clamp(angle, -90, 90, 0);
    result.distance = clamp(distance, 1, 2000, 10);
    result.amount = clamp(amount, 0.1, 400, 10);
    result.vignetteAmount = clamp(vignetteAmount, 0, 100, 35);
    result.vignetteColor = vignetteColor.clamped();
    result.vignetteMidpoint = clamp(vignetteMidpoint, 0, 100, 50);
    result.vignetteRoundness = clamp(vignetteRoundness, -100, 100, 100);
    result.vignetteFeather = clamp(vignetteFeather, 0, 100, 60);
    result.vignetteHighlights = clamp(vignetteHighlights, 0, 100, 25);
    result.bloomAmount = clamp(bloomAmount, 0, 100, 40);
    result.bloomRadius = clamp(bloomRadius, 1, 150, 24);
    result.tonalAmount = clamp(tonalAmount, 0, 100, 50);
    result.tonalRadius = clamp(tonalRadius, 1, 100, 16);
    result.tonalShadows = clamp(tonalShadows, -100, 100, 40);
    result.tonalMidtones = clamp(tonalMidtones, -100, 100, 60);
    result.tonalHighlights = clamp(tonalHighlights, -100, 100, 30);
    result.distortion = clamp(distortion, -100, 100, 0);
    result.refineEdges = clamp(refineEdges, 0, 40, 12);
    result.matteContrast = clamp(matteContrast, 0, 100, 25);
    result.shiftEdge = clamp(shiftEdge, -10, 10, 0);
    result.exposure = exposure.normalized();
    result.gradientMap = gradientMap.normalized();
    result.grain = grain.normalized();
    result.cameraRaw = cameraRaw.normalized();
    return result;
}

PixelFilter::Trimmed PixelFilter::trimmed(const QImage &image, const LayerTransform &placed)
{
    const QRectF full(image.rect());
    // Bounds read premultiplied bytes; other formats get drawn.
    QImage pixels = image;
    if (pixels.format() != QImage::Format_RGBA8888_Premultiplied) {
        pixels = BrushRaster::context(image.width(), image.height(), false);
        QPainter painter(&pixels);
        BrushRaster::draw(image, full, painter);
    }
    size_t edges[4] = {0, 0, 0, 0};
    brush_alpha_bounds(pixels.constBits(), size_t(pixels.width()), size_t(pixels.height()), size_t(pixels.bytesPerLine()), edges);
    const QRect crop(int(edges[0]), int(edges[1]), int(edges[2] - edges[0]), int(edges[3] - edges[1]));
    if (crop.width() < 1 || crop.height() < 1 || QRectF(crop) == full)
        return {image, placed};
    LayerTransform result = placed;
    result.size = {crop.width() * placed.size.width() / full.width(), crop.height() * placed.size.height() / full.height()};
    const QPointF middle = BrushRaster::pixelToDocument(placed, image.width(), image.height()).map(QRectF(crop).center());
    result.origin = {middle.x() - result.size.width() / 2, middle.y() - result.size.height() / 2};
    // Swift's crop shares pixels; a copy may find no memory.
    const QImage cropped = image.copy(crop);
    if (cropped.isNull())
        throw ExportError(ExportError::Kind::render);
    return {cropped, result};
}

namespace {
// The image drawn into a fresh premultiplied context.
QImage drawn(const QImage &image)
{
    QImage context = BrushRaster::context(image.width(), image.height(), false);
    QPainter painter(&context);
    BrushRaster::draw(image, QRectF(0, 0, image.width(), image.height()), painter);
    return context;
}

// CIBloom is private: the pixels plus their Gaussian glow, scaled.
QImage bloom(QImage pixels, double sigma, double intensity)
{
    const QImage glow = PixelAdjust::gaussianBlur(pixels, sigma, false);
    for (int y = 0; y < pixels.height(); ++y) {
        uchar *row = pixels.scanLine(y);
        const uchar *light = glow.constScanLine(y);
        for (int index = 0; index < pixels.width() * 4; ++index)
            row[index] = uchar(std::min(255.0, std::round(row[index] + intensity * light[index])));
    }
    return pixels;
}
}

QImage PixelFilter::run(const FilterJob &job)
{
    const FilterSettings settings = job.settings.normalized();
    const int width = job.image.width(), height = job.image.height();
    QImage image;
    switch (job.kind) {
    case FilterKind::curves: image = settings.curves.apply(job.image); break;
    case FilterKind::exposure: image = settings.exposure.apply(job.image); break;
    case FilterKind::gradientMap: image = settings.gradientMap.apply(job.image); break;
    case FilterKind::blackWhite: image = settings.blackWhite.apply(job.image); break;
    case FilterKind::colorBalance: image = settings.colorBalance.apply(job.image); break;
    case FilterKind::cameraRaw:
        image = settings.cameraRaw.apply(job.image, job.cameraRawClipping, job.scale, job.seed, job.visualizesPointColor, job.showsSharpenMask);
        break;
    // Grain sits in layer pixels; the job's seed patterns it.
    case FilterKind::grain: image = settings.grain.apply(job.image, QPointF(), 1 / job.scale, job.seed); break;
    case FilterKind::removeBackground: image = SubjectRemoval::run(job.image, settings); break;
    case FilterKind::contentAwareFill: image = ContentFill::run(job); break;
    // Unclamped: a blur spreads into the room made for it.
    case FilterKind::gaussianBlur: image = PixelAdjust::gaussianBlur(drawn(job.image), settings.radius * job.scale, false); break;
    case FilterKind::motionBlur:
        image = PixelAdjust::motionBlur(drawn(job.image), settings.distance * job.scale * motionRadiusPerPixel, settings.angle * std::numbers::pi / 180);
        break;
    case FilterKind::addNoise:
        image = drawn(job.image);
        noise_add_at(image.bits(), size_t(width), size_t(height), size_t(image.bytesPerLine()), float(settings.amount), settings.gaussian ? 1 : 0,
                     settings.monochromatic ? 1 : 0, job.seed, int64_t(std::floor(job.noiseOrigin.x())), int64_t(std::floor(job.noiseOrigin.y())));
        break;
    case FilterKind::vignette: {
        image = drawn(job.image);
        // The canvas in this image's pixels, else the image itself.
        const QRectF frame = job.canvas ? job.mapping.inverted().mapRect(*job.canvas) : QRectF(0, 0, width, height);
        adjust_colored_vignette(image.bits(), size_t(width), size_t(height), size_t(image.bytesPerLine()), frame.x(), frame.y(), frame.width(),
                                frame.height(), job.canvas ? 1 : 0, settings.vignetteAmount,
                                settings.vignetteMidpoint, settings.vignetteRoundness, settings.vignetteFeather, settings.vignetteHighlights,
                                settings.vignetteColor.red, settings.vignetteColor.green, settings.vignetteColor.blue);
        break;
    }
    case FilterKind::bloomGlow: image = bloom(drawn(job.image), settings.bloomRadius * job.scale, settings.bloomAmount / 50); break;
    case FilterKind::tonalContrast: {
        // The detail's base: the layer softened, clear past its edge.
        const QImage base = PixelAdjust::gaussianBlur(drawn(job.image), settings.tonalRadius * job.scale, false);
        image = drawn(job.image);
        adjust_tonal_contrast(image.bits(), base.constBits(), size_t(width), size_t(height), size_t(image.bytesPerLine()), size_t(base.bytesPerLine()),
                              settings.tonalAmount, settings.tonalShadows, settings.tonalMidtones, settings.tonalHighlights);
        break;
    }
    case FilterKind::lensCorrection: {
        // Relative to the image's size, so previews bend alike.
        const QImage source = drawn(job.image);
        image = BrushRaster::context(width, height, false);
        lens_distort(source.constBits(), image.bits(), size_t(width), size_t(height), size_t(source.bytesPerLine()), settings.distortion / 100 * lensStrength);
        break;
    }
    }
    if (!job.selection)
        return image;
    return PixelAdjust::blend(image, job.image, *job.selection, job.mapping, false);
}

namespace {
QRectF integral(const QRectF &rect)
{
    return QRectF(QPointF(std::floor(rect.left()), std::floor(rect.top())), QPointF(std::ceil(rect.right()), std::ceil(rect.bottom())));
}
}

FilterEdit::FilterEdit(FilterKind kind, const ImageLayer &layer, std::optional<SelectionClip> selection, const FilterSettings &settings,
                       std::optional<QRectF> area)
    : kind(kind), layerID(layer.id), original(layer.asset ? *layer.asset : throw ProjectError(ProjectError::Kind::invalid)),
      transform(layer.transform), selection(std::move(selection)), settings(settings.normalized())
{
    prepare(original, transform);
    if (area)
        grow(integral(mapping.inverted().mapRect(*area)));
    growForBlur();
}

double FilterEdit::blurMargin(FilterKind kind, const FilterSettings &settings)
{
    switch (kind) {
    case FilterKind::gaussianBlur: return settings.radius * 3 + 2;
    case FilterKind::motionBlur: return settings.distance / 2 + 2;
    case FilterKind::bloomGlow: return settings.bloomRadius * 3 + 2;
    default: return 0;
    }
}

void FilterEdit::growForBlur()
{
    const double margin = blurMargin(kind, settings);
    if (!(margin > grownMargin))
        return;
    const QSize size = original.size();
    grow(QRectF(-margin, -margin, size.width() + 2 * margin, size.height() + 2 * margin));
}

// The layer's pixels in a grid covering `extent` (layer pixels).
void FilterEdit::grow(const QRectF &extent)
{
    const QSize size = original.size();
    const QRectF bounds(0, 0, size.width(), size.height());
    const QRectF target = integral(bounds.united(extent));
    if (target == bounds)
        return;
    if (target.width() > DocumentLimits::maxSide || target.height() > DocumentLimits::maxSide || target.width() * target.height() > DocumentLimits::maxSurfacePixels)
        throw ProjectError(ProjectError::Kind::tooLarge);
    QImage grown = BrushRaster::context(int(target.width()), int(target.height()), false);
    const QRectF inside = bounds.translated(-target.left(), -target.top());
    QPainter painter(&grown);
    if (original.raster)
        original.raster->draw(inside, painter);
    else
        BrushRaster::draw(original.image(), inside, painter);
    painter.end();
    const QTransform toDocument = BrushRaster::pixelToDocument(transform, size.width(), size.height());
    LayerTransform expanded = transform;
    expanded.size = {target.width() * transform.size.width() / bounds.width(), target.height() * transform.size.height() / bounds.height()};
    const QPointF middle = toDocument.map(target.center());
    expanded.origin = {middle.x() - expanded.size.width() / 2, middle.y() - expanded.size.height() / 2};
    grownImage = grown;
    grownTransform = expanded;
    grownMargin = std::min({bounds.left() - target.left(), bounds.top() - target.top(), target.right() - bounds.right(), target.bottom() - bounds.bottom()});
    prepare(ImportedImage(grown, QImage(), QString()), expanded);
}

void FilterEdit::prepare(const ImportedImage &source, const LayerTransform &placed)
{
    const QSize size = source.size();
    ++previewSourceVersion;
    mapping = BrushRaster::pixelToDocument(placed, size.width(), size.height());
    // Noise and grain preview whole: enlarged, grain looks coarse.
    const bool whole = kind == FilterKind::addNoise || kind == FilterKind::grain || kind == FilterKind::contentAwareFill || kind == FilterKind::removeBackground;
    const double factor = whole ? 1 : std::min(1.0, previewLimit / std::max(size.width(), size.height()));
    if (!(factor < 1)) {
        previewSource = source.image();
        previewScale = 1;
        previewMapping = mapping;
        return;
    }
    const int width = std::max(1, int(size.width() * factor)), height = std::max(1, int(size.height() * factor));
    previewSource = BrushRaster::context(width, height, false);
    QPainter painter(&previewSource);
    const QRectF rect(0, 0, width, height);
    if (source.raster)
        source.raster->draw(rect, painter);
    else
        BrushRaster::draw(source.image(), rect, painter);
    painter.end();
    previewScale = double(width) / size.width();
    previewMapping = BrushRaster::pixelToDocument(placed, width, height);
}

std::optional<QImage> FilterEdit::previewImage(QUuid id) const
{
    return preview && id == layerID ? preparedPreview : std::nullopt;
}

FilterJob FilterEdit::previewJob() const
{
    return FilterJob{kind,
                     previewSource,
                     renderSettings(),
                     previewScale,
                     selection,
                     previewMapping,
                     seed,
                     rawPanel.clipping,
                     rawPanel.showsShadowClipping,
                     rawPanel.showsHighlightClipping,
                     rawPanel.pointColorVisualizeIndex(settings.cameraRaw),
                     rawPanel.sharpenMask,
                     {},
                     canvas};
}

FilterSettings FilterEdit::renderSettings() const
{
    FilterSettings value = settings;
    if (kind == FilterKind::cameraRaw)
        value.cameraRaw = value.cameraRaw.applying(rawPanel.shows);
    return value;
}
