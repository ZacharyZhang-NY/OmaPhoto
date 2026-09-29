#include "IO/CanvasResizer.h"
#include "Document/DocumentLimits.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <new>

namespace {
ProjectSnapshot resized(const ProjectSnapshot &snapshot, const CanvasSizeOptions &options)
{
    if (options.width < 1 || options.width > DocumentLimits::maxSide || options.height < 1 || options.height > DocumentLimits::maxSide || options.anchor < 0
        || options.anchor > 8)
        throw ProjectError(ProjectError::Kind::tooLarge);
    const ProjectManifest &old = snapshot.manifest;
    const QPointF offset = options.offset(old.width, old.height);
    // NaN and infinity fail the comparison.
    if (!(std::abs(offset.x()) <= 1'000'000 && std::abs(offset.y()) <= 1'000'000))
        throw ProjectError(ProjectError::Kind::invalid);
    if (options.width == old.width && options.height == old.height && offset == QPointF(0, 0))
        return snapshot;
    ProjectManifest manifest{.resolution = old.resolution, .documentID = old.documentID, .width = options.width,
                             .height = options.height, .activeLayerID = old.activeLayerID, .layers = {}, .guides = old.guides};
    // Guides move with the pixels.
    if (manifest.guides) {
        for (CanvasGuide &guide : *manifest.guides)
            guide = guide.offset(offset.x(), offset.y());
    }
    for (const ProjectLayerRecord &layer : old.layers) {
        LayerTransform transform = layer.transform;
        transform.origin += offset;
        if (!transform.isValid())
            throw ProjectError(ProjectError::Kind::tooLarge);
        std::optional<LayerTransform> placement = layer.maskPlacement;
        if (placement)
            placement->origin += offset;
        // Field by field, as Swift: what it omits is dropped.
        manifest.layers.push_back({.id = layer.id, .name = layer.name, .isVisible = layer.isVisible, .transform = transform,
                                   .imageFile = layer.imageFile, .parentID = layer.parentID, .isGroup = layer.isGroup,
                                   .opacity = layer.opacity, .blendMode = layer.blendMode, .maskFile = layer.maskFile,
                                   .maskEnabled = layer.maskEnabled, .maskSourceID = layer.maskSourceID, .adjustment = layer.adjustment,
                                   .maskPlacement = placement,
                                   .maskLinked = layer.maskLinked, .shape = layer.shape,
                                   .text = layer.text});
    }
    std::map<QUuid, ImportedImage> images = snapshot.images;
    // The fill: a bottom layer, clear over the old canvas.
    if (options.fill && (options.width > old.width || options.height > old.height)) {
        qint64 used = 0;
        for (const auto &[id, image] : images)
            used += qint64(image.size().width()) * image.size().height();
        if (options.width * options.height > DocumentLimits::documentPixelBudget() - used || manifest.layers.size() >= 10'000)
            throw ProjectError(ProjectError::Kind::tooLarge);
        const CanvasExtensionColor &color = *options.fill;
        for (const double part : {color.red, color.green, color.blue}) {
            if (!(part >= 0 && part <= 1))
                throw ProjectError(ProjectError::Kind::invalid);
        }
        QImage image(int(options.width), int(options.height), QImage::Format_RGBA8888_Premultiplied);
        if (image.isNull())
            throw ExportError(ExportError::Kind::render);
        image.fill(QColor::fromRgbF(float(color.red), float(color.green), float(color.blue)));
        {
            QPainter context(&image);
            context.setCompositionMode(QPainter::CompositionMode_Clear);
            context.fillRect(QRectF(offset, QSizeF(double(old.width), double(old.height))), Qt::transparent);
        }
        const double factor = std::min(1.0, 96 / double(std::max(options.width, options.height)));
        const QSize small(std::max(1, int(double(options.width) * factor)), std::max(1, int(double(options.height) * factor)));
        const QImage thumbnail = image.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (thumbnail.isNull())
            throw ExportError(ExportError::Kind::render);
        const QUuid id = QUuid::createUuid();
        const QString name = QStringLiteral("Canvas Extension");
        images.insert({id, ImportedImage(image, thumbnail, name)});
        manifest.layers.insert(manifest.layers.begin(),
                               {.id = id, .name = name, .isVisible = true,
                                .transform = {.origin = {0, 0}, .size = {double(options.width), double(options.height)}},
                                .imageFile = uuidString(id) + QStringLiteral(".png")});
    }
    qCInfo(lcIO) << "canvas" << old.width << "x" << old.height << "becomes" << options.width << "x" << options.height;
    return {.manifest = manifest, .images = images, .masks = snapshot.masks};
}
}

ProjectSnapshot CanvasResizer::resize(const ProjectSnapshot &snapshot, const CanvasSizeOptions &options)
{
    // Records and maps without memory fail as a render.
    try {
        return resized(snapshot, options);
    } catch (const std::bad_alloc &) {
        throw ExportError(ExportError::Kind::render);
    }
}
