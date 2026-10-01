#include "Document/Distort.h"
#include "Document/DocumentLimits.h"
#include "Document/LayerMask.h"
#include "Document/PixelAdjust.h"
#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include "Rendering/DownsampleCache.h"
#include "IO/ProjectStore.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

LayerMask::LayerMask(ImportedImage asset, bool isEnabled, std::optional<LayerTransform> placement, bool isLinked)
    : asset(std::move(asset)), isEnabled(isEnabled), placement(placement), isLinked(isLinked)
{
}

std::optional<QImage> LayerMask::enabledImage() const
{
    return isEnabled ? std::optional(asset.image()) : std::nullopt;
}

std::optional<LayerMask> ProjectSnapshot::mask(const ProjectLayerRecord &layer) const
{
    const auto asset = masks.find(layer.id);
    if (!layer.maskFile || asset == masks.end())
        return std::nullopt;
    return LayerMask(asset->second, layer.maskEnabled.value_or(true), layer.maskPlacement, layer.maskLinked.value_or(true));
}

bool operator==(const LayerMask &lhs, const LayerMask &rhs)
{
    return lhs.asset.identity() == rhs.asset.identity() && lhs.isEnabled == rhs.isEnabled
        && lhs.placement == rhs.placement && lhs.isLinked == rhs.isLinked;
}

LayerMask LayerMask::replacing(ImportedImage replacement) const
{
    return LayerMask(std::move(replacement), isEnabled, placement, isLinked);
}

bool LayerMask::isValid(const QImage &image)
{
    return image.format() == QImage::Format_Grayscale8;
}

LayerMask LayerMask::solid(bool revealing)
{
    QImage image = BrushRaster::context(1, 1, true);
    image.fill(revealing ? 255 : 0);
    return LayerMask(ImportedImage(image, image, QStringLiteral("Layer Mask")));
}

ImportedImage LayerMask::assetFrom(const QImage &image)
{
    if (!isValid(image))
        throw ProjectError(ProjectError::Kind::invalid);
    const double factor = std::min(1.0, 96.0 / std::max(image.width(), image.height()));
    const QSize size(std::max(1, int(image.width() * factor)), std::max(1, int(image.height() * factor)));
    const QImage thumbnail = image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (thumbnail.isNull())
        throw ExportError(ExportError::Kind::render);
    return ImportedImage(image, thumbnail, QStringLiteral("Layer Mask"));
}

std::optional<LayerTransform> LayerMask::placementMovingLayer(const LayerTransform &old,
                                                              const LayerTransform &updated) const
{
    // A uniform mask looks the same wherever it sits.
    if (asset.size().width() <= 1 && asset.size().height() <= 1)
        return std::nullopt;
    std::optional<LayerTransform> moved;
    if (!isLinked)
        moved = placement.value_or(old);
    else if (placement)
        moved = placement->following(old, updated);
    if (moved && moved->samePlacement(updated))
        return std::nullopt;
    return moved;
}

double LayerMask::background(const QImage &thumbnail)
{
    const int width = thumbnail.width(), height = thumbnail.height();
    if (width <= 0 || height <= 0)
        return 1;
    if (!isValid(thumbnail))
        throw std::logic_error("a mask thumbnail must be 8-bit gray");
    int total = 0, count = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (y != 0 && y != height - 1 && x != 0 && x != width - 1)
                continue;
            total += thumbnail.constScanLine(y)[x];
            count += 1;
        }
    }
    return total * 2 >= count * 255 ? 1 : 0;
}

QImage LayerMask::placed(int width, int height, const LayerTransform &layer, const LayerTransform &placement,
                         int maskWidth, int maskHeight, double background,
                         const std::function<void(QPainter &)> &compose)
{
    QImage surface = BrushRaster::context(width, height, true);
    surface.fill(qRound(background * 255));
    QPainter context(&surface);
    context.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    context.setTransform(BrushRaster::pixelToDocument(placement, maskWidth, maskHeight)
                         * BrushRaster::pixelToDocument(layer, width, height).inverted());
    compose(context);
    context.end();
    return surface;
}

void LayerMask::drawSmooth(const QImage &image, const QRectF &rect, QPainter &context)
{
    context.save();
    context.setRenderHint(QPainter::SmoothPixmapTransform, true);
    context.drawImage(rect, image);
    context.restore();
}

std::optional<QImage> LayerMask::clipImage(const std::optional<LayerTransform> &placement, const LayerTransform &layer,
                                           int width, int height, std::optional<double> limit) const
{
    const std::optional<QImage> image = enabledImage();
    if (!image)
        return std::nullopt;
    if (!placement || placement->samePlacement(layer) || width <= 0 || height <= 0)
        return image;
    const double factor = limit ? std::min(1.0, std::max(1.0, *limit) / std::max(width, height)) : 1;
    const int w = std::max(1, int(std::ceil(width * factor))), h = std::max(1, int(std::ceil(height * factor)));
    return MaskPlacementCache::shared().image(*image, *placement, layer, w, h, [&]() -> std::optional<QImage> {
        // Drawn from a sharp halving near the covered size.
        const double covered = placement->size.width() / std::max(1.0, layer.size.width()) * w;
        const QImage source = DownsampleCache::shared().imageDrawnAt(*image, covered / image->width());
        try {
            return placed(w, h, layer, *placement, image->width(), image->height(), background(asset.thumbnail),
                          [&](QPainter &context) { drawSmooth(source, QRectF(0, 0, image->width(), image->height()), context); });
        } catch (const ExportError &error) {
            qCWarning(lcRendering) << "a placed mask could not be rendered:" << error.what();
            return std::nullopt;
        }
    });
}

MaskPlacementCache &MaskPlacementCache::shared()
{
    static MaskPlacementCache cache;
    return cache;
}

std::optional<QImage> MaskPlacementCache::image(const QImage &mask, const LayerTransform &placement,
                                                const LayerTransform &layer, int width, int height,
                                                const std::function<std::optional<QImage>()> &build)
{
    {
        const std::lock_guard<std::mutex> guard(m_lock);
        m_clock += 1;
        for (Entry &entry : m_entries) {
            if (entry.mask.cacheKey() == mask.cacheKey() && entry.placement == placement && entry.layer == layer
                && entry.width == width && entry.height == height) {
                entry.lastUse = m_clock;
                return entry.image;
            }
        }
    }
    const std::optional<QImage> image = build();
    if (!image)
        return std::nullopt;
    constexpr qint64 pixelBudget = 64'000'000;
    if (qint64(width) * height > pixelBudget)
        return image;
    const std::lock_guard<std::mutex> guard(m_lock);
    m_entries.push_back({mask, placement, layer, width, height, *image, m_clock});
    const auto pixels = [&] {
        qint64 total = 0;
        for (const Entry &entry : m_entries)
            total += qint64(entry.width) * entry.height;
        return total;
    };
    while (m_entries.size() > 8 || pixels() > pixelBudget) {
        m_entries.erase(std::min_element(m_entries.begin(), m_entries.end(),
                                         [](const Entry &lhs, const Entry &rhs) { return lhs.lastUse < rhs.lastUse; }));
    }
    return image;
}

void FolderMaskClip::apply(QPointF center, const QPainter &context, QImage &coverage, double scale) const
{
    const double width = transform.size.width() * scale, height = transform.size.height() * scale;
    QImage veil(coverage.size(), QImage::Format_Alpha8);
    if (veil.isNull())
        throw ExportError(ExportError::Kind::render);
    veil.fill(0);
    {
        QTransform placement;
        placement.translate(center.x(), center.y());
        placement.rotateRadians(transform.radians());
        placement.scale(transform.flipX ? -1 : 1, transform.flipY ? -1 : 1);
        QPainter masking(&veil);
        masking.setRenderHint(QPainter::SmoothPixmapTransform, quality(transform.sampling) != InterpolationQuality::none);
        masking.setRenderHint(QPainter::Antialiasing, true);
        masking.setTransform(placement * context.deviceTransform());
        masking.drawImage(QRectF(-width / 2, -height / 2, width, height), BrushRaster::alphaView(image));
    }
    // A failed copy of shared coverage must not pass.
    QPainter multiplying(&coverage);
    if (!multiplying.isActive())
        throw ExportError(ExportError::Kind::render);
    multiplying.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    multiplying.drawImage(QRectF(coverage.rect()), veil);
}

void FolderMaskClip::draw(const std::vector<QUuid> &ids, const std::function<std::optional<QUuid>(QUuid)> &parent,
                          const std::function<std::optional<Applier>(QUuid)> &clip, const QPainter &context,
                          const std::function<void(QUuid, const QImage &clip)> &drawLayer)
{
    // Coverage through a folder and its ancestors; null for none.
    std::map<QUuid, QImage> covered;
    std::function<QImage(std::optional<QUuid>, int)> through = [&](std::optional<QUuid> folder, int depth) -> QImage {
        if (!folder || depth >= 64)
            return QImage();
        if (const auto found = covered.find(*folder); found != covered.end())
            return found->second;
        QImage coverage = through(parent(*folder), depth + 1);
        if (const std::optional<Applier> applier = clip(*folder)) {
            if (coverage.isNull()) {
                coverage = QImage(context.device()->width(), context.device()->height(), QImage::Format_Alpha8);
                if (coverage.isNull())
                    throw ExportError(ExportError::Kind::render);
                coverage.fill(255);
            }
            (*applier)(context, coverage);
        }
        covered.insert_or_assign(*folder, coverage);
        return coverage;
    };
    for (const QUuid &id : ids)
        drawLayer(id, through(parent(id), 0));
}

LayerTransform ImageLayer::maskTransform() const
{
    return mask && mask->placement ? *mask->placement : transform;
}

bool EditorSession::canEditMask() const
{
    return canEditLayers() && m_selectedLayerIDs.size() == 1 && activeLayer().has_value();
}

void EditorSession::selectLayerTarget(QUuid id, bool mask)
{
    dropEffectSelection();
    if (m_isProjectBusy || m_isImporting || m_brushStroke)
        return;
    resolveGradient();
    selectLayer(id);
    const std::optional<ImageLayer> active = activeLayer();
    setIsMaskSelected(mask && active && active->mask);
    notify();
}

void EditorSession::addMask(bool revealing)
{
    const std::optional<DocumentSelection> current = selection();
    if (!current) {
        addLayerMask(revealing);
        return;
    }
    const std::optional<ImageLayer> active = activeLayer();
    if (!canEditMask() || active->mask)
        return;
    const int index = indexOf(m_document->layers, active->id);
    // Mask pixels cover the layer's own pixel grid.
    const int width = active->asset ? active->asset->size().width() : int(std::round(active->size().width()));
    const int height = active->asset ? active->asset->size().height() : int(std::round(active->size().height()));
    try {
        if (width <= 0 || height <= 0 || qint64(width) * height > DocumentLimits::maxSurfacePixels)
            throw ProjectError(ProjectError::Kind::tooLarge);
        // The selection's coverage, soft where it is feathered.
        QImage image = PixelAdjust::coverage(current->clip(m_document->size()), width, height,
                                             BrushRaster::pixelToDocument(active->transform, width, height));
        // Revealing, white inside; hiding, black inside.
        if (!revealing)
            image.invertPixels();
        const LayerMask mask(LayerMask::assetFrom(image));
        finishOpacityEdit();
        beginEdit(revealing ? QStringLiteral("Reveal Selection") : QStringLiteral("Hide Selection"));
        m_document->layers[size_t(index)].mask = mask;
        m_document->selection = std::nullopt;
        setIsMaskSelected(true);
        endEdit();
    } catch (const std::runtime_error &error) {
        qCWarning(lcApp).noquote() << "cannot make a mask from the selection:" << error.what();
        setBrushError(QString::fromUtf8(error.what()));
    }
}

void EditorSession::addLayerMask(bool revealing)
{
    const std::optional<ImageLayer> active = activeLayer();
    if (!canEditMask() || active->mask)
        return;
    finishOpacityEdit();
    beginEdit(revealing ? QStringLiteral("Add Reveal-All Mask") : QStringLiteral("Add Hide-All Mask"));
    for (ImageLayer &layer : m_document->layers) {
        if (layer.id == active->id)
            layer.mask = LayerMask::solid(revealing);
    }
    setIsMaskSelected(true);
    endEdit();
}

void EditorSession::toggleLayerMask()
{
    const std::optional<ImageLayer> active = activeLayer();
    if (!canEditMask() || !active->mask)
        return;
    finishOpacityEdit();
    beginEdit(active->mask->isEnabled ? QStringLiteral("Disable Layer Mask") : QStringLiteral("Enable Layer Mask"));
    for (ImageLayer &layer : m_document->layers) {
        if (layer.id == active->id)
            layer.mask->isEnabled = !layer.mask->isEnabled;
    }
    endEdit();
}

void EditorSession::deleteLayerMask()
{
    const std::optional<ImageLayer> active = activeLayer();
    if (!canEditMask() || !active->mask)
        return;
    finishOpacityEdit();
    beginEdit(QStringLiteral("Delete Layer Mask"));
    for (ImageLayer &layer : m_document->layers) {
        if (layer.id == active->id)
            layer.mask = std::nullopt;
    }
    setIsMaskSelected(false);
    endEdit();
}

bool EditorSession::canCopyMask(QUuid source, QUuid target) const
{
    if (!canEditLayers() || source == target)
        return false;
    const auto holds = [&](QUuid id, bool asSource) {
        return std::any_of(m_document->layers.begin(), m_document->layers.end(), [&](const ImageLayer &layer) {
            return layer.id == id && (asSource ? layer.mask.has_value() : !layer.isGroup);
        });
    };
    return holds(source, true) && holds(target, false);
}

void EditorSession::copyMask(QUuid source, QUuid target)
{
    if (!canCopyMask(source, target))
        return;
    commitTransform();
    const auto find = [&](QUuid id) {
        return std::find_if(m_document->layers.begin(), m_document->layers.end(), [&](const ImageLayer &layer) { return layer.id == id; });
    };
    // The copy sits where the mask sits on the document.
    LayerMask mask = *find(source)->mask;
    mask.placement = find(source)->maskTransform();
    beginEdit(find(target)->mask ? QStringLiteral("Replace Layer Mask") : QStringLiteral("Copy Layer Mask"));
    find(target)->mask = mask;
    selectLayer(target);
    setIsMaskSelected(true);
    endEdit();
}

void EditorSession::toggleMaskLink(QUuid id)
{
    const auto holds = [&](const ImageLayer &layer) { return layer.id == id && layer.mask; };
    if (!canEditLayers() || std::none_of(m_document->layers.begin(), m_document->layers.end(), holds))
        return;
    commitTransform();
    const auto layer = std::find_if(m_document->layers.begin(), m_document->layers.end(), holds);
    beginEdit(layer->mask->isLinked ? QStringLiteral("Unlink Layer Mask") : QStringLiteral("Link Layer Mask"));
    layer->mask->isLinked = !layer->mask->isLinked;
    endEdit();
}

std::optional<LayerTransform> EditorSession::displayedMaskPlacement(const ImageLayer &layer) const
{
    if (!layer.mask)
        return std::nullopt;
    // Previewed on a grown layer, the mask keeps its bounds.
    if (m_filterEdit && m_filterEdit->preparedTransform && m_filterEdit->previewImage(layer.id))
        return layer.mask->placement.value_or(layer.transform);
    if (m_transformEdit && m_transformEdit->group) {
        const TransformGroup &group = *m_transformEdit->group;
        const auto original = group.originals.constFind(layer.id);
        if (original == group.originals.constEnd())
            return layer.mask->placement;
        if (m_transformEdit->corners)
            return layer.mask->isLinked && !layer.mask->placement ? std::nullopt : std::optional(layer.mask->placement.value_or(layer.transform));
        return layer.mask->placementMovingLayer(layer.transform, original->following(group.box, m_transformEdit->draft));
    }
    if (!m_transformEdit || m_transformEdit->layerID != layer.id)
        return layer.mask->placement;
    const LayerTransform &draft = m_transformEdit->draft;
    if (m_transformEdit->mask)
        return draft.samePlacement(layer.transform) ? std::nullopt : std::optional(draft);
    // A distortion carries a linked layer's own mask; others stay.
    if (m_transformEdit->corners)
        return layer.mask->isLinked && !layer.mask->placement ? std::nullopt : std::optional(layer.mask->placement.value_or(layer.transform));
    return layer.mask->placementMovingLayer(layer.transform, draft);
}

std::optional<QImage> EditorSession::maskDistortPreview(const ImageLayer &layer) const
{
    const std::optional<TransformEdit> &edit = m_transformEdit;
    if (!edit || !edit->mask || edit->layerID != layer.id || !edit->corners || !layer.mask || !layer.mask->isEnabled)
        return std::nullopt;
    const LayerMask &owned = *layer.mask;
    if (m_maskDistortPreviewCache && m_maskDistortPreviewCache->corners == *edit->corners && m_maskDistortPreviewCache->draft == edit->draft
        && m_maskDistortPreviewCache->mask == owned.asset.identity() && m_maskDistortPreviewCache->layer == layer.transform)
        return m_maskDistortPreviewCache->result;
    const int width = layer.asset ? layer.asset->size().width() : int(std::round(layer.size().width()));
    const int height = layer.asset ? layer.asset->size().height() : int(std::round(layer.size().height()));
    std::optional<QImage> result;
    try {
        const DistortWarp::Warped moved = DistortWarp::warpMask(owned.asset.image(), edit->draft, *edit->corners, LayerMask::background(owned.asset.thumbnail), 2048);
        result = LayerMask(ImportedImage(moved.image, owned.asset.thumbnail, owned.asset.name)).clipImage(moved.transform, layer.transform, width, height, 2048);
    } catch (const ProjectError &) {
    } catch (const ExportError &) {
    }
    m_maskDistortPreviewCache = MaskDistortPreviewCache{*edit->corners, edit->draft, owned.asset.identity(), layer.transform, result};
    return result;
}

void EditorSession::commitMaskTransform()
{
    const TransformEdit edit = *m_transformEdit;
    m_maskDistortPreviewCache.reset();
    const int index = indexOf(m_document->layers, edit.layerID);
    if (edit.corners && edit.draft.isValid() && index >= 0 && m_document->layers[index].mask) {
        // A distorted mask is resampled; its background fills outside.
        m_transformEdit = std::nullopt;
        const ImageLayer layer = m_document->layers[index];
        const LayerMask &mask = *layer.mask;
        try {
            const DistortWarp::Warped moved = DistortWarp::warpMask(mask.asset.image(), edit.draft, *edit.corners, LayerMask::background(mask.asset.thumbnail));
            // Everything that can throw comes before the transaction opens.
            const ImportedImage asset = moved.image.cacheKey() == mask.asset.image().cacheKey() ? mask.asset : LayerMask::assetFrom(moved.image);
            beginEdit(QStringLiteral("Distort Layer Mask"));
            m_document->layers[index].mask = LayerMask(asset, mask.isEnabled,
                                                       moved.transform.samePlacement(layer.transform) ? std::nullopt : std::optional(moved.transform), mask.isLinked);
            endEdit();
        } catch (const ProjectError &error) {
            setBrushError(QString::fromUtf8(error.what()));
        } catch (const ExportError &error) {
            setBrushError(QString::fromUtf8(error.what()));
        }
        return;
    }
    // The mask takes the placement; its pixels are untouched.
    beginEdit(QStringLiteral("Transform Layer Mask"));
    for (ImageLayer &layer : m_document->layers) {
        if (layer.id == edit.layerID && layer.mask && edit.draft.isValid())
            layer.mask->placement = edit.draft.samePlacement(layer.transform) ? std::nullopt : std::optional(edit.draft);
    }
    m_transformEdit = std::nullopt;
    endEdit();
}

// The stroke's mask, in the layer's grid, preview-sized.
std::optional<QImage> BrushStroke::placedMaskPreview(const LayerTransform &placement) const
{
    const int gridWidth = layer.asset ? layer.asset->size().width() : int(std::round(layer.size().width()));
    const int gridHeight = layer.asset ? layer.asset->size().height() : int(std::round(layer.size().height()));
    const double factor = std::min(1.0, 2048.0 / std::max({1, gridWidth, gridHeight}));
    const int w = std::max(1, int(std::ceil(gridWidth * factor))), h = std::max(1, int(std::ceil(gridHeight * factor)));
    const std::optional<ImportedImage> &old = layer.mask ? std::optional(layer.mask->asset) : std::nullopt;
    const double perMaskPixel = placement.size.width() / std::max(1, width) * w / std::max(1.0, layer.transform.size.width());
    try {
        return LayerMask::placed(w, h, layer.transform, placement, width, height, old ? LayerMask::background(old->thumbnail) : 1, [&](QPainter &context) {
            if (old)
                LayerMask::drawSmooth(DownsampleCache::shared().imageDrawnAt(old->image(), perMaskPixel), sourceRect, context);
            for (const BrushPatch &patch : patches())
                LayerMask::drawSmooth(patch.image, patch.rect, context);
        });
    } catch (const ExportError &error) {
        qCWarning(lcRendering) << "the placed mask's preview could not be made:" << error.what();
        return std::nullopt;
    }
}
