#include "Document/FloatingSelection.h"
#include "Document/BrushStroke.h"
#include "Document/Distort.h"
#include "Document/EditorSession.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore.h"
#include "Logging.h"
#include "Rendering/LayerRenderer.h"
#include <cmath>

// Swift's FloatingSelection extension: one "Transform Selection" step.
namespace {
QRectF integral(const QRectF &rect)
{
    return QRectF(QPointF(std::floor(rect.left()), std::floor(rect.top())), QPointF(std::ceil(rect.right()), std::ceil(rect.bottom())));
}
}

bool EditorSession::canTransformSelection() const
{
    const std::optional<DocumentSelection> current = selection();
    const std::optional<ImageLayer> layer = activeLayer();
    // `canEditPixels` refuses an open edit and an empty selection.
    return canEditPixels() && !m_isMaskSelected && current && layer && layer->asset;
}

// Ctrl+T: the selected pixels with a selection, else the layer.
void EditorSession::transformCommand()
{
    if (canTransformSelection())
        beginSelectionTransform();
    else
        beginTransform();
}

void EditorSession::beginSelectionTransform(std::function<void()> done)
{
    const auto finish = [this, done] {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
    };
    if (!canTransformSelection()) {
        qCWarning(lcApp) << "no selected pixels to transform";
        finish();
        return;
    }
    const ImageLayer source = activeLayer().value();
    std::optional<CopiedPixels> lifted;
    try {
        lifted = renderSelectedPixels(source, false);
    } catch (const ExportError &error) {
        setBrushError(QString::fromUtf8(error.what()));
        finish();
        return;
    }
    if (!lifted) {
        qCWarning(lcApp) << "no selected pixels to lift";
        finish();
        return;
    }
    const CanvasDocument before = m_document.value();
    const std::optional<QUuid> beforeActive = m_activeLayerID;
    // The outer step: closed by the merge or the restore.
    beginEdit(QStringLiteral("Transform Selection"));
    clearSelectedPixels([this, source, lifted = *lifted, before, beforeActive, finish] {
        const int index = m_document ? indexOf(m_document->layers, source.id) : -1;
        std::optional<QImage> thumbnail;
        try {
            thumbnail = PixelAdjust::thumbnail(lifted.image);
        } catch (const ExportError &error) {
            qCWarning(lcApp) << "the floating selection could not be made:" << error.what();
        }
        if (index < 0 || !thumbnail) {
            m_document = before;
            endEdit();
            finish();
            return;
        }
        ImageLayer floating(ImportedImage(lifted.image, *thumbnail, QStringLiteral("Floating Selection")), lifted.region.topLeft());
        floating.name = QStringLiteral("Floating Selection");
        floating.parentID = source.parentID;
        floating.opacity = source.opacity;
        floating.blendMode = source.blendMode;
        m_document->layers.insert(m_document->layers.begin() + index + 1, floating);
        setActiveLayerID(floating.id);
        m_tool = NavigationTool::move;
        const auto state = std::make_shared<const FloatingTransform>(FloatingTransform{source.id, before, beforeActive, floating.transform, lifted.region.size()});
        m_transformEdit = TransformEdit{floating.id, floating.transform, std::nullopt, false, true, std::nullopt, state};
        notify();
        finish();
    });
}

// Maps the original selection to where the pixels float now.
std::optional<QTransform> EditorSession::floatingSelectionTransform(const TransformEdit &edit) const
{
    if (!edit.floating)
        return std::nullopt;
    const int width = int(edit.floating->pixelSize.width()), height = int(edit.floating->pixelSize.height());
    return BrushRaster::pixelToDocument(edit.floating->original, width, height).inverted() * BrushRaster::pixelToDocument(edit.draft, width, height);
}

// The pixels back into their layer, the selection moved along.
void EditorSession::mergeFloatingTransform(const TransformEdit &edit, const FloatingTransform &floating)
{
    try {
        // The open edit holds both layers; checked reads suffice.
        const QImage pixels = m_document->layers[indexOf(m_document->layers, edit.layerID)].asset.value().image();
        const ImageLayer source = m_document->layers[indexOf(m_document->layers, floating.sourceID)];
        // A distorted selection is warped into its shape first.
        QImage placedImage = pixels;
        LayerTransform placedTransform = edit.draft;
        if (edit.corners) {
            const DistortWarp::Trimmed warped = DistortWarp::warpTrimmed(pixels, edit.draft, *edit.corners);
            placedImage = warped.image;
            placedTransform = warped.transform;
        }
        const FloatingMerge::Merged merged = FloatingMerge::merge(placedImage, placedTransform, source);
        const std::optional<DocumentSelection> current = selection();
        std::optional<DocumentSelection> moved;
        if (current && edit.corners) {
            const QTransform placement = BrushRaster::pixelToDocument(floating.original, int(floating.pixelSize.width()), int(floating.pixelSize.height()));
            if (const std::optional<QPainterPath> path = DistortWarp::mapPath(current->path, placement, floating.pixelSize, edit.draft, *edit.corners))
                moved = DocumentSelection{*path, current->antialiased, current->feather};
        } else if (const std::optional<QTransform> shift = floatingSelectionTransform(edit); current && shift) {
            moved = DocumentSelection{shift->map(current->path), current->antialiased, current->feather};
        }
        std::erase_if(m_document->layers, [&](const ImageLayer &layer) { return layer.id == edit.layerID; });
        ImageLayer &layer = m_document->layers[indexOf(m_document->layers, source.id)];
        layer.asset = merged.asset;
        layer.transform = merged.transform;
        layer.isGroup = false;
        layer.mask = merged.mask;
        // Swift rebuilds the layer without shape, effects and text.
        layer.shape = std::nullopt;
        layer.effects = std::nullopt;
        layer.text = std::nullopt;
        m_document->selection = moved;
        setActiveLayerID(source.id);
    } catch (const ProjectError &error) {
        m_document = floating.before;
        setActiveLayerID(floating.beforeActive);
        setBrushError(QString::fromUtf8(error.what()));
    } catch (const ExportError &error) {
        m_document = floating.before;
        setActiveLayerID(floating.beforeActive);
        setBrushError(QString::fromUtf8(error.what()));
    }
    endEdit();
}

void EditorSession::cancelFloatingTransform(const FloatingTransform &floating)
{
    m_document = floating.before;
    setActiveLayerID(floating.beforeActive);
    endEdit();
}

FloatingMerge::Merged FloatingMerge::merge(const QImage &pixels, const LayerTransform &transform, const ImageLayer &source)
{
    if (!source.asset)
        throw ProjectError(ProjectError::Kind::invalid);
    const QImage sourceImage = source.asset->image();
    const int width = sourceImage.width(), height = sourceImage.height();
    const QTransform toDocument = BrushRaster::pixelToDocument(source.transform, width, height);
    const QTransform toPixels = toDocument.inverted();
    const QRectF floatingBounds = toPixels.mapRect(BrushRaster::pixelToDocument(transform, pixels.width(), pixels.height()).mapRect(QRectF(pixels.rect())));
    const QRectF original(0, 0, width, height);
    const QRectF extent = integral(original.united(floatingBounds));
    if (extent.width() > 30'000 || extent.height() > 30'000 || extent.width() * extent.height() > 100'000'000)
        throw ProjectError(ProjectError::Kind::tooLarge);
    QImage context = BrushRaster::context(int(extent.width()), int(extent.height()), false);
    const QRectF placed = original.translated(-extent.topLeft());
    QPainter painter(&context);
    BrushRaster::draw(sourceImage, placed, painter);
    painter.setTransform(toPixels * QTransform::fromTranslate(-extent.left(), -extent.top()));
    LayerRenderer::draw(pixels, transform, transform.center(), painter, {});
    painter.end();
    const ImportedImage asset(context, PixelAdjust::thumbnail(context), source.name);
    LayerTransform merged = source.transform;
    merged.size = {extent.width() * source.size().width() / width, extent.height() * source.size().height() / height};
    const QPointF center = toDocument.map(extent.center());
    merged.origin = {center.x() - merged.size.width() / 2, center.y() - merged.size.height() / 2};
    std::optional<LayerMask> mask = source.mask;
    if (source.mask && !source.mask->placement && extent != original) {
        // The mask grows with the layer, revealing the new area.
        QImage grown = BrushRaster::context(int(extent.width()), int(extent.height()), true);
        grown.fill(Qt::white);
        QPainter maskPainter(&grown);
        BrushRaster::draw(source.mask->asset.image(), placed, maskPainter);
        maskPainter.end();
        mask = source.mask->replacing(LayerMask::assetFrom(grown));
    }
    return {asset, merged, mask};
}
