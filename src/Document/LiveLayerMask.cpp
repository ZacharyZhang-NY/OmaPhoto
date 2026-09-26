#include "Document/LiveLayerMask.h"
#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "Document/LayerEffects+Renderer.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include "Rendering/AdjustmentSurface.h"
#include "Rendering/LayerRenderer.h"
#include "Rendering/LiveMaskRenderer.h"
#include <QHash>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QtConcurrent>
#include <map>
#include <stdexcept>

void EditorSession::drawLiveComposite(const CanvasDocument &document, QPainter &context, bool onSurface) const
{
    // Adjustments read the pixels beneath: a surface of their own.
    if (!onSurface && std::any_of(document.layers.begin(), document.layers.end(), [](const ImageLayer &layer) { return layer.adjustment.has_value(); })) {
        AdjustmentSurface::draw(context, [&](QPainter &surface) { drawLiveComposite(document, surface, true); });
        return;
    }
    std::map<QUuid, ImageLayer> records;
    for (const ImageLayer &layer : document.layers) {
        if (!records.emplace(layer.id, layer).second)
            throw std::logic_error("a document repeats a layer id");
    }
    const auto parent = [&](QUuid id) { return records.contains(id) ? records.at(id).parentID : std::nullopt; };
    LiveMaskRenderer live([&](QUuid id) { return records.contains(id) ? records.at(id).maskSourceID : std::nullopt; },
                          [&](QUuid id, QPainter &target, const QImage &clip) {
                              if (!records.contains(id) || !records.at(id).asset)
                                  return;
                              const ImageLayer &layer = records.at(id);
                              const QImage image = layer.asset->image();
                              const LayerTransform transform = displayedTransform(layer);
                              const std::optional<QImage> mask =
                                  layer.mask ? layer.mask->clipImage(displayedMaskPlacement(layer), transform, image.width(), image.height()) : std::nullopt;
                              // With effects the mask is in their image already.
                              if (const auto effects = LayerEffectsRenderer::cached(image, mask, layer.effects)) {
                                  const LayerTransform grown = LayerEffectsRenderer::placed(transform, effects->image, effects->inset);
                                  LayerRenderer::draw(effects->image, grown, grown.center(), target,
                                                      {.opacity = layer.effectiveOpacity(records), .blendMode = displayedBlendMode(layer), .clip = clip});
                                  return;
                              }
                              LayerRenderer::draw(image, transform, transform.center(), target,
                                                  {.opacity = layer.effectiveOpacity(records), .blendMode = displayedBlendMode(layer), .mask = mask.value_or(QImage()), .clip = clip});
                          });
    live.adjustment = [&](QUuid id) { return records.contains(id) ? records.at(id).adjustment : std::nullopt; };
    live.adjustmentOpacity = [&](QUuid id) { return records.at(id).effectiveOpacity(records); };
    live.adjustmentClip = [&](QUuid id, const QPainter &painter, QImage &coverage) {
        const ImageLayer &layer = records.at(id);
        if (const std::optional<QImage> image = layer.mask ? layer.mask->enabledImage() : std::nullopt)
            FolderMaskClip{*image, layer.transform}.apply(layer.transform.center(), painter, coverage);
    };
    std::vector<QUuid> ids;
    for (const ImageLayer &layer : document.renderLayers())
        ids.push_back(layer.id);
    live.prepareStacks(ids, parent, [&](QUuid id) { return records.contains(id) ? displayedBlendMode(records.at(id)) : LayerBlendMode::normal; });
    FolderMaskClip::draw(ids, parent, [&](QUuid id) -> std::optional<FolderMaskClip::Applier> {
        if (!records.contains(id) || !records.at(id).mask)
            return std::nullopt;
        const ImageLayer &folder = records.at(id);
        const std::optional<QImage> image = folder.mask->enabledImage();
        if (!image)
            return std::nullopt;
        const FolderMaskClip clip{*image, displayedTransform(folder)};
        return [clip](const QPainter &painter, QImage &coverage) { clip.apply(clip.transform.center(), painter, coverage); };
    }, context, [&](QUuid id, const QImage &clip) { live.drawComposite(id, context, clip); });
}

void LiveMaskGraph::validate(const std::vector<ProjectLayerRecord> &layers)
{
    QHash<QUuid, const ProjectLayerRecord *> records;
    for (const ProjectLayerRecord &layer : layers) {
        if (records.contains(layer.id))
            throw ProjectError(ProjectError::Kind::invalid);
        records.insert(layer.id, &layer);
    }
    for (const ProjectLayerRecord &layer : layers) {
        QSet<QUuid> path;
        std::optional<QUuid> current = layer.id;
        while (current) {
            const ProjectLayerRecord *record = records.value(*current);
            if (path.size() >= 256 || path.contains(*current) || !record)
                throw ProjectError(ProjectError::Kind::invalid);
            path.insert(*current);
            if (record->maskSourceID) {
                const ProjectLayerRecord *source = records.value(*record->maskSourceID);
                // Folders never clip; nothing clips to an adjustment.
                if (record->isGroup == true || !source || source->isGroup == true || source->adjustment)
                    throw ProjectError(ProjectError::Kind::invalid);
            }
            current = record->maskSourceID;
        }
    }
}

std::optional<ImportedImage> LiveMaskBaker::bake(const ProjectSnapshot &snapshot, QUuid target)
{
    const std::vector<ProjectLayerRecord> &layers = snapshot.manifest.layers;
    const size_t found = std::find_if(layers.begin(), layers.end(), [&](const ProjectLayerRecord &layer) { return layer.id == target; }) - layers.begin();
    if (found == layers.size() || !snapshot.images.contains(target))
        return std::nullopt;
    // Checked reads: a lost guard aborts or throws.
    const ImportedImage &original = snapshot.images.at(target);
    const int width = original.size().width(), height = original.size().height();
    QImage surface = BrushRaster::context(width, height, false);
    const QTransform inverse = BrushRaster::pixelToDocument(layers[found].transform, width, height).inverted();
    // Swift's dictionary traps on an id that repeats.
    QHash<QUuid, const ProjectLayerRecord *> records;
    for (const ProjectLayerRecord &layer : layers) {
        if (records.contains(layer.id))
            throw std::logic_error("a snapshot to bake repeats a layer id");
        records.insert(layer.id, &layer);
    }
    // A source dims with its folders, as exported.
    const auto folded = [&records](const ProjectLayerRecord &layer) {
        return LayerOpacity::effective(layer.opacity.value_or(1), layer.parentID, [&records](QUuid id) -> std::optional<std::pair<double, std::optional<QUuid>>> {
            if (!records.contains(id))
                return std::nullopt;
            return std::pair(records.value(id)->opacity.value_or(1), records.value(id)->parentID);
        });
    };
    LiveMaskRenderer live([&](QUuid id) { return records.contains(id) ? records.value(id)->maskSourceID : std::nullopt; },
                          [&](QUuid id, QPainter &context, const QImage &clip) {
                              // `at` is checked: end() would read wild memory.
                              if (!records.contains(id) || !snapshot.images.contains(id))
                                  return;
                              const QImage pixels = snapshot.images.at(id).image();
                              // Only the live dependency bakes: mask and appearance stay.
                              if (id == target) {
                                  LayerRenderer::draw(pixels, {.origin = {0, 0}, .size = QSizeF(width, height)}, QPointF(width / 2.0, height / 2.0),
                                                      context, {.clip = clip});
                                  return;
                              }
                              const ProjectLayerRecord &layer = *records.value(id);
                              const std::optional<LayerMask> mask = snapshot.mask(layer);
                              const std::optional<QImage> shown =
                                  mask ? mask->clipImage(mask->placement, layer.transform, pixels.width(), pixels.height()) : std::nullopt;
                              context.save();
                              context.setTransform(inverse, true);
                              LayerRenderer::draw(pixels, layer.transform, layer.transform.center(), context,
                                                  {.opacity = folded(layer), .mask = shown.value_or(QImage()), .clip = clip});
                              context.restore();
                          });
    {
        QPainter painter(&surface);
        live.draw(target, painter);
    }
    return ImportedImage(surface, PixelAdjust::thumbnail(surface), original.name);
}

bool EditorSession::canLinkMask(QUuid source, QUuid target) const
{
    if (!canEditLayers())
        return false;
    bool found = false;
    std::vector<ProjectLayerRecord> records;
    for (const ImageLayer &layer : m_document->layers) {
        records.push_back(layer.hierarchyRecord());
        if (layer.id == target) {
            records.back().maskSourceID = source;
            found = true;
        }
    }
    if (!found)
        return false;
    // The graph refuses itself, folders, strangers and rings.
    try {
        LiveMaskGraph::validate(records);
    } catch (const ProjectError &) {
        return false;
    }
    return true;
}

bool EditorSession::linkMask(QUuid source, QUuid target)
{
    if (!canLinkMask(source, target))
        return false;
    ImageLayer &layer = m_document->layers[indexOf(m_document->layers, target)];
    if (layer.maskSourceID == source)
        return true;
    beginEdit(QStringLiteral("Create Clipping Mask"));
    layer.maskSourceID = source;
    endEdit();
    return true;
}

void EditorSession::adoptClipping(QUuid id, std::vector<ImageLayer> &layers)
{
    const int moved = indexOf(layers, id);
    if (moved < 0 || layers[moved].isGroup)
        return;
    std::vector<const ImageLayer *> siblings;
    for (const ImageLayer &layer : layers) {
        if (layer.parentID == layers[moved].parentID)
            siblings.push_back(&layer);
    }
    const size_t own = std::find_if(siblings.begin(), siblings.end(), [&](const ImageLayer *layer) { return layer->id == id; }) - siblings.begin();
    // Between a base or its stack and a clipped layer.
    if (own == 0 || own + 1 == siblings.size())
        return;
    const std::optional<QUuid> source = siblings[own + 1]->maskSourceID;
    const ImageLayer &below = *siblings[own - 1];
    if (source && *source != id && (below.id == *source || below.maskSourceID == source))
        layers[moved].maskSourceID = source;
}

void EditorSession::removeLiveMask(QUuid target)
{
    if (!canEditLayers())
        return;
    const int clipped = indexOf(m_document->layers, target);
    if (clipped < 0 || !m_document->layers[clipped].maskSourceID)
        return;
    // Those above it on the same base let go too.
    const std::optional<QUuid> source = m_document->layers[clipped].maskSourceID, parent = m_document->layers[clipped].parentID;
    beginEdit(QStringLiteral("Release Clipping Mask"));
    bool reached = false;
    for (ImageLayer &layer : m_document->layers) {
        if (layer.parentID != parent)
            continue;
        reached = reached || layer.id == target;
        if (reached && layer.id != target && layer.maskSourceID != source)
            break;
        if (reached)
            layer.maskSourceID = std::nullopt;
    }
    endEdit();
}

bool EditorSession::canToggleClippingMask(QUuid id) const
{
    if (!canEditLayers())
        return false;
    const std::vector<ImageLayer> &layers = m_document->layers;
    const int layer = indexOf(layers, id);
    if (layer < 0)
        return false;
    if (layers[layer].maskSourceID)
        return true;
    const std::optional<QUuid> base = clippingBase(layers[layer]);
    return base && canLinkMask(*base, id);
}

std::optional<QUuid> EditorSession::clippingBase(const ImageLayer &layer) const
{
    // The next lower sibling, or the base that one shares.
    const ImageLayer *below = nullptr;
    for (const ImageLayer &each : m_document->layers) {
        if (each.id == layer.id)
            break;
        if (each.parentID == layer.parentID)
            below = &each;
    }
    return below ? std::optional(below->maskSourceID.value_or(below->id)) : std::nullopt;
}

void EditorSession::toggleClippingMask(QUuid id)
{
    if (!canEditLayers())
        return;
    const std::vector<ImageLayer> &layers = m_document->layers;
    const int layer = indexOf(layers, id);
    if (layer < 0)
        return;
    if (layers[layer].maskSourceID) {
        removeLiveMask(id);
        return;
    }
    if (const std::optional<QUuid> base = clippingBase(layers[layer]))
        linkMask(*base, id);
}

void EditorSession::releaseDetachedClipping(std::vector<ImageLayer> &layers)
{
    // A clipped layer sits in one run above its base.
    std::map<std::optional<QUuid>, std::optional<QUuid>> bases;
    for (ImageLayer &layer : layers) {
        std::optional<QUuid> &base = bases[layer.parentID];
        if (!layer.maskSourceID) {
            base = layer.id;
        } else if (layer.maskSourceID != base) {
            layer.maskSourceID = std::nullopt;
            base = layer.id;
        }
    }
}

bool EditorSession::deleteWithLiveMaskChoice(const std::vector<QUuid> &ids)
{
    if (!m_document)
        return false;
    QSet<QUuid> removed;
    for (const QUuid id : ids) {
        removed.unite(descendantIDs(id));
        removed.insert(id);
    }
    std::vector<QUuid> targets;
    for (const ImageLayer &layer : m_document->layers) {
        if (!removed.contains(layer.id) && layer.maskSourceID && removed.contains(*layer.maskSourceID))
            targets.push_back(layer.id);
    }
    if (targets.empty())
        return false;
    QMessageBox alert;
    alert.setIcon(QMessageBox::Warning);
    alert.setText(ids.size() == 1 ? QStringLiteral("This layer supplies a live mask") : QStringLiteral("These layers supply live masks"));
    alert.setInformativeText(QStringLiteral("Bake keeps the current masked appearance in the dependent layers’ pixels. "
                                            "Remove Links reveals their pixels. You can undo either choice."));
    const QPushButton *bake = alert.addButton(QStringLiteral("Bake and Delete"), QMessageBox::AcceptRole);
    alert.addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
    const QPushButton *unlink = alert.addButton(QStringLiteral("Remove Links and Delete"), QMessageBox::DestructiveRole);
    alert.exec();
    if (alert.clickedButton() == unlink) {
        finishDeletingLayers(ids, {});
        return true;
    }
    const std::optional<ProjectSnapshot> snapshot = projectSnapshot();
    if (alert.clickedButton() != bake || !snapshot)
        return true;
    qCInfo(lcApp) << "baking" << int(targets.size()) << "live masks before a deletion";
    setIsProjectBusy(true);
    m_bakingIDs = ids;
    m_baker.setFuture(QtConcurrent::run([snapshot = *snapshot, targets]() -> Baked {
        Baked result;
        try {
            for (const QUuid target : targets) {
                if (const std::optional<ImportedImage> image = LiveMaskBaker::bake(snapshot, target))
                    result.images.insert({target, *image});
            }
        } catch (const ExportError &error) {
            result.failure = QString::fromUtf8(error.what());
        }
        return result;
    }));
    return true;
}

void EditorSession::finishBake()
{
    const Baked baked = m_baker.result();
    if (baked.failure) {
        qCWarning(lcApp).noquote() << "cannot bake the live masks:" << *baked.failure;
        setBrushError(baked.failure);
    } else {
        finishDeletingLayers(m_bakingIDs, baked.images);
    }
    setIsProjectBusy(false);
}

void EditorSession::finishDeletingLayer(QUuid id, const BakedImages &baked)
{
    if (!m_document)
        return;
    std::vector<ImageLayer> &layers = m_document->layers;
    const int index = indexOf(layers, id);
    if (index < 0)
        return;
    QSet<QUuid> removed = descendantIDs(id);
    removed.insert(id);
    beginEdit(QStringLiteral("Delete Layer"));
    std::erase_if(layers, [&](const ImageLayer &layer) { return removed.contains(layer.id); });
    // Who was clipped to them is freed, baked where asked.
    for (ImageLayer &layer : layers) {
        if (!layer.maskSourceID || !removed.contains(*layer.maskSourceID))
            continue;
        layer.maskSourceID = std::nullopt;
        if (const auto image = baked.find(layer.id); image != baked.end())
            layer.asset = image->second;
    }
    if (m_activeLayerID && removed.contains(*m_activeLayerID))
        setActiveLayerID(layers.empty() ? std::nullopt : std::optional(layers[std::min(index, int(layers.size()) - 1)].id));
    endEdit();
}

void EditorSession::finishDeletingLayers(const std::vector<QUuid> &ids, const BakedImages &baked)
{
    if (ids.size() <= 1) {
        if (!ids.empty())
            finishDeletingLayer(ids.front(), baked);
        return;
    }
    beginEdit(QStringLiteral("Delete Layers"));
    for (const QUuid id : ids)
        finishDeletingLayer(id, baked);
    endEdit();
}
