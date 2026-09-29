#include "IO/ImageResizer.h"
#include "Document/DocumentLimits.h"
#include "Document/EditorSession.h"
#include "Document/LayerMask.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include "Rendering/LayerRenderer.h"
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <new>

namespace {
// A surface over the scaled layer, in document coordinates.
QImage drawn(QImage::Format format, qint64 width, qint64 height, QPointF topLeft, double sx, double sy,
             const std::function<void(QPainter &)> &body)
{
    QImage image(int(width), int(height), format);
    if (image.isNull())
        throw ExportError(ExportError::Kind::render);
    image.fill(0);
    QPainter context(&image);
    context.translate(-topLeft.x(), -topLeft.y());
    context.scale(sx, sy);
    body(context);
    return image;
}

ProjectSnapshot resized(const ProjectSnapshot &snapshot, const ImageSizeOptions &options)
{
    // NaN fails both tests of a range.
    if (options.width < 1 || options.width > DocumentLimits::maxSide || options.height < 1 || options.height > DocumentLimits::maxSide
        || !(options.resolution >= 1 && options.resolution <= 9600))
        throw ProjectError(ProjectError::Kind::tooLarge);
    const ProjectManifest &old = snapshot.manifest;
    ProjectManifest manifest{.resolution = options.resolution, .documentID = old.documentID, .width = options.width,
                             .height = options.height, .activeLayerID = old.activeLayerID, .layers = {}, .guides = old.guides};
    if (old.width == options.width && old.height == options.height) {
        manifest.layers = old.layers;
        return {.manifest = manifest, .images = snapshot.images, .masks = snapshot.masks};
    }
    if (options.width * options.height > DocumentLimits::maxSurfacePixels)
        throw ProjectError(ProjectError::Kind::tooLarge);
    const double sx = double(options.width) / double(old.width), sy = double(options.height) / double(old.height);
    // Guides scale with the pixels.
    if (manifest.guides) {
        for (CanvasGuide &guide : *manifest.guides)
            guide = guide.scaled(sx, sy);
    }
    std::map<QUuid, ImportedImage> images, masks;
    qint64 usedPixels = 0, usedMaskPixels = 0;
    for (const ProjectLayerRecord &layer : old.layers) {
        // Each layer is drawn again: unequal scaling shears turned ones.
        double left = INFINITY, top = INFINITY, right = -INFINITY, bottom = -INFINITY;
        for (const QPointF unit : {QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1)}) {
            const QPointF corner = layer.transform.point(unit);
            left = std::min(left, corner.x() * sx), right = std::max(right, corner.x() * sx);
            top = std::min(top, corner.y() * sy), bottom = std::max(bottom, corner.y() * sy);
        }
        left = std::floor(left), top = std::floor(top);
        const LayerTransform transform{.origin = {left, top}, .size = {std::ceil(right) - left, std::ceil(bottom) - top},
                                       .sampling = options.sampling};
        // Judged before any number becomes an integer.
        if (!transform.isValid())
            throw ProjectError(ProjectError::Kind::tooLarge);
        const qint64 width = qint64(transform.size.width()), height = qint64(transform.size.height());
        LayerTransform sourceTransform = layer.transform;
        sourceTransform.sampling = options.sampling;
        if (layer.imageFile) {
            if (width > DocumentLimits::maxSide || height > DocumentLimits::maxSide || width * height > DocumentLimits::documentPixelBudget() - usedPixels)
                throw ProjectError(ProjectError::Kind::tooLarge);
            usedPixels += width * height;
            const auto source = snapshot.images.find(layer.id);
            if (source == snapshot.images.end())
                throw ProjectError(ProjectError::Kind::missingImage);
            const QImage image = drawn(QImage::Format_RGBA8888_Premultiplied, width, height, {left, top}, sx, sy, [&](QPainter &context) {
                LayerRenderer::draw(source->second.image(), sourceTransform, sourceTransform.center(), context);
            });
            const double factor = std::min(1.0, 96 / double(std::max(width, height)));
            const QSize small(std::max(1, int(double(width) * factor)), std::max(1, int(double(height) * factor)));
            const QImage thumbnail = image.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            if (thumbnail.isNull())
                throw ExportError(ExportError::Kind::render);
            images.insert({layer.id, ImportedImage(image, thumbnail, source->second.name)});
        }
        if (layer.maskFile) {
            const auto source = snapshot.masks.find(layer.id);
            if (source == snapshot.masks.end())
                throw ProjectError(ProjectError::Kind::missingImage);
            // Solid masks have no resolution; placed ones keep their pixels.
            if (source->second.size() == QSize(1, 1) || layer.maskPlacement) {
                masks.insert({layer.id, source->second});
            } else {
                if (width > DocumentLimits::maxSide || height > DocumentLimits::maxSide || width * height > DocumentLimits::documentPixelBudget() - usedMaskPixels)
                    throw ProjectError(ProjectError::Kind::tooLarge);
                usedMaskPixels += width * height;
                const QImage image = drawn(QImage::Format_Grayscale8, width, height, {left, top}, sx, sy, [&](QPainter &context) {
                    LayerRenderer::drawCoverage(source->second.image(), sourceTransform, context);
                });
                masks.insert({layer.id, LayerMask::assetFrom(image)});
            }
        }
        std::optional<LayerTransform> placement;
        if (layer.maskPlacement)
            placement = layer.maskPlacement->placing(layer.maskPlacement->unitToDocument() * QTransform::fromScale(sx, sy));
        manifest.layers.push_back({.id = layer.id, .name = layer.name, .isVisible = layer.isVisible, .transform = transform,
                                   .imageFile = layer.imageFile, .parentID = layer.parentID, .isGroup = layer.isGroup,
                                   .opacity = layer.opacity, .blendMode = layer.blendMode, .maskFile = layer.maskFile,
                                   .maskEnabled = layer.maskEnabled, .maskSourceID = layer.maskSourceID, .adjustment = layer.adjustment,
                                   .maskPlacement = placement,
                                   .maskLinked = layer.maskLinked});
    }
    qCInfo(lcIO) << "resized" << old.width << "x" << old.height << "to" << options.width << "x" << options.height;
    return {.manifest = manifest, .images = images, .masks = masks};
}
}

ProjectSnapshot ImageResizer::resize(const ProjectSnapshot &snapshot, const ImageSizeOptions &options)
{
    // Records, maps and tiles without memory fail as a render.
    try {
        return resized(snapshot, options);
    } catch (const std::bad_alloc &) {
        throw ExportError(ExportError::Kind::render);
    }
}

void EditorSession::applyImageSize(const ProjectSnapshot &snapshot)
{
    applyDocumentSize(snapshot, QStringLiteral("Image Size"));
}

// Staged aside, then published: running out of memory changes nothing.
void EditorSession::applyDocumentSize(const ProjectSnapshot &snapshot, const QString &actionName)
{
    if (!m_document || m_document->id != snapshot.manifest.documentID)
        return;
    std::optional<CanvasDocument> next = CanvasDocument(snapshot);
    DocumentHistory recorded = history;
    recorded.begin(actionName, m_document, m_activeLayerID);
    recorded.end(next, m_activeLayerID);
    m_document = std::move(next);
    history.adopt(std::move(recorded));
    viewport.fit(m_document->size());
    notify();
}
