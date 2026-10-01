#include "IO/PSD/PSDDocumentBuilder.h"
#include "Document/PixelAdjust.h"
#include "IO/ProjectStore.h"
#include "Document/BrushStroke.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include <QPainter>

namespace {
ImportedImage imported(const QImage &image, const QString &name)
{
    return ImportedImage(image, PixelAdjust::thumbnail(image), name);
}

// Swift's `try?`: text that cannot be drawn stays pixels.
std::optional<PSDText::Rendered> rendered(const PSDRecord &record)
{
    if (!record.text)
        return std::nullopt;
    try {
        return PSDText::render(*record.text);
    } catch (const std::runtime_error &) {
        return std::nullopt;
    }
}

std::vector<QString> notes(const PSDRecord &record, bool typed)
{
    std::vector<QString> result;
    if (record.kind == PSDLayerKind::text) {
        if (typed) {
            result.insert(result.end(), record.text->notes.begin(), record.text->notes.end());
            if (const std::optional<QString> missing = PSDText::missingFontNote(record.text->style.fontName))
                result.push_back(*missing);
        } else {
            result.push_back(PSDText::rasterizedNote);
        }
    }
    if (record.kind == PSDLayerKind::smartObject)
        result.push_back(QStringLiteral("The smart object was rasterized. Linked contents can’t be edited."));
    if (record.kind == PSDLayerKind::effects)
        result.push_back(QStringLiteral("Layer effects were discarded, so the appearance may differ."));
    if (record.kind == PSDLayerKind::vector) {
        if (record.shape)
            result.insert(result.end(), record.shapeNotes.begin(), record.shapeNotes.end());
        else
            result.push_back(QStringLiteral("Vector shape was rasterized to pixels."));
    }
    if (record.isGroup) {
        if (record.blendKey != QLatin1String("pass") && record.blendKey != QLatin1String("norm"))
            result.push_back(QStringLiteral("Folder blend mode “%1” isn’t supported. The folder will be pass-through.").arg(record.blendKey));
    } else if (!record.blendMode() && record.blendKey != QLatin1String("pass")) {
        // Swift trims `.whitespaces`: spaces and tabs.
        QString key = record.blendKey;
        while (!key.isEmpty() && (key.front() == QLatin1Char(' ') || key.front() == QLatin1Char('\t')))
            key.remove(0, 1);
        while (!key.isEmpty() && (key.back() == QLatin1Char(' ') || key.back() == QLatin1Char('\t')))
            key.chop(1);
        result.push_back(QStringLiteral("Blend mode “%1” isn’t supported and will be applied as Normal.").arg(key));
    }
    if (record.kind == PSDLayerKind::adjustment)
        result.push_back(record.adjustment ? QStringLiteral("Adjustment parameters may not match Photoshop exactly.")
                                           : QStringLiteral("This adjustment type isn’t supported and was skipped."));
    return result;
}

ImageLayer layerFor(const PSDRecord &record, QSizeF canvas, const std::map<QUuid, ImportedImage> &assets, const std::optional<PSDText::Rendered> &typed)
{
    ImageLayer layer(record.name, canvas);
    if (typed && !record.adjustment) {
        layer = ImageLayer(imported(typed->image, record.name), QPointF());
        layer.transform = typed->transform;
        layer.text = LayerText{record.text->style, layer.asset->identity()};
    } else if (record.image && !record.isGroup && !record.adjustment) {
        const ImportedImage asset = assets.contains(record.id) ? assets.at(record.id) : imported(*record.image, record.name);
        const QSizeF size = record.bounds.width() > 0 && record.bounds.height() > 0 ? record.bounds.size() : QSizeF(record.image->size());
        layer = ImageLayer(asset, record.bounds.topLeft());
        layer.transform.size = size;
        if (record.shape)
            layer.shape = LayerShape{*record.shape, asset.identity()};
    }
    layer.id = record.id;
    layer.isVisible = record.isVisible;
    layer.parentID = record.parentID;
    layer.isGroup = record.isGroup;
    layer.opacity = std::clamp(record.opacity, 0.0, 1.0);
    if (!record.isGroup) {
        layer.blendMode = record.blendMode().value_or(LayerBlendMode::normal);
        layer.adjustment = record.adjustment;
    }
    return layer;
}
}

std::map<QUuid, ImportedImage> PSDDocumentBuilder::assets(const PSDDocument &document)
{
    std::map<QUuid, ImportedImage> result;
    for (const PSDRecord &record : document.layers)
        if (record.image)
            result.emplace(record.id, imported(*record.image, record.name));
    return result;
}

PSDImport PSDDocumentBuilder::makeImport(const PSDDocument &document, const std::map<QUuid, ImportedImage> &assets)
{
    std::vector<PSDConversion> conversions;
    std::vector<ImageLayer> layers;
    const QSizeF canvas(document.width, document.height);
    for (const PSDRecord &record : document.layers) {
        if (record.croppedToCanvas)
            conversions.push_back(PSDConversion{.layerName = record.name,
                                                .message = QStringLiteral("Cropped to the canvas so the file fits in memory. Pixels outside the canvas weren't imported.")});
        const std::optional<PSDText::Rendered> typed = rendered(record);
        for (const QString &note : notes(record, typed.has_value()))
            conversions.push_back(PSDConversion{.layerName = record.name, .message = note});
        if (record.kind == PSDLayerKind::adjustment && !record.adjustment)
            continue;
        ImageLayer layer = layerFor(record, canvas, assets, typed);
        if (record.mask) {
            try {
                const std::optional<QImage> placed = maskOnLayerGrid(*record.mask, record, layer, canvas);
                if (!placed)
                    throw ExportError(ExportError::Kind::render);
                layer.mask = LayerMask(LayerMask::assetFrom(*placed), record.maskEnabled, std::nullopt, record.maskLinked);
            } catch (const std::runtime_error &) {
                // Swift's `try?`: a mask that fails is reported, not fatal.
                conversions.push_back(PSDConversion{.layerName = record.name,
                                                    .message = QStringLiteral("The layer mask couldn’t be converted to 8-bit grayscale and was skipped.")});
            }
        }
        layers.push_back(std::move(layer));
    }
    // A clipped layer takes the nearest unclipped pixels below.
    std::map<std::optional<QUuid>, QUuid> baseForParent;
    for (const PSDRecord &record : document.layers) {
        const int index = indexOf(layers, record.id);
        if (index < 0)
            continue;
        ImageLayer &layer = layers[size_t(index)];
        if (record.clipping) {
            // A clipped folder breaks the live mask graph: never linked.
            const int source = !layer.isGroup && baseForParent.contains(record.parentID) ? indexOf(layers, baseForParent.at(record.parentID)) : -1;
            if (source >= 0) {
                layer.maskSourceID = layers[size_t(source)].id;
            } else {
                conversions.push_back(PSDConversion{.layerName = record.name,
                                                    .message = QStringLiteral("This clipping mask’s base isn’t supported, so clipping was skipped.")});
            }
        } else if (!layer.isGroup && !layer.adjustment) {
            baseForParent[record.parentID] = record.id;
        } else {
            baseForParent.erase(record.parentID);
        }
    }
    return PSDImport{document.width, document.height, document.resolution, std::move(layers), std::move(conversions)};
}

std::optional<QImage> PSDDocumentBuilder::maskOnLayerGrid(const QImage &patch, const PSDRecord &record, const ImageLayer &layer, QSizeF canvas)
{
    const QSizeF grid = layer.asset ? QSizeF(layer.asset->size()) : canvas;
    const QRectF placed(layer.transform.origin, layer.transform.size);
    if (grid.width() < 1 || grid.height() < 1 || !(placed.width() > 0) || !(placed.height() > 0) || !(record.maskBounds.width() > 0)
        || !(record.maskBounds.height() > 0))
        return patch;
    const double scaleX = grid.width() / placed.width(), scaleY = grid.height() / placed.height();
    const QRectF rect((record.maskBounds.left() - placed.left()) * scaleX, (record.maskBounds.top() - placed.top()) * scaleY,
                      record.maskBounds.width() * scaleX, record.maskBounds.height() * scaleY);
    const int width = int(grid.width()), height = int(grid.height());
    // Already the layer's grid: nothing to place.
    if (rect.toAlignedRect() == QRect(0, 0, width, height) && patch.size() == QSize(width, height))
        return patch;
    QImage context(width, height, QImage::Format_Grayscale8);
    if (context.isNull()) {
        qCWarning(lcIO) << "a PSD layer mask could not be placed on its layer:" << QSize(width, height);
        return std::nullopt;
    }
    context.fill(uint(record.maskDefault));
    QPainter painter(&context);
    BrushRaster::draw(patch, rect, painter);
    return context;
}
