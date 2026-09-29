#include "Document/BrushStroke.h"
#include "Document/DocumentLimits.h"
#include "Document/EditorSession.h"
#include "Document/PixelAdjust.h"
#include "Document/PixelInvert.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include <QtConcurrent>
#include <cmath>

namespace {
// A uniform 1×1 mask given the layer's pixel grid.
QImage expandedUniformMask(const QImage &image, int width, int height)
{
    if (width <= 0 || height <= 0 || qint64(width) * height > DocumentLimits::maxSurfacePixels)
        throw ProjectError(ProjectError::Kind::tooLarge);
    QImage expanded = BrushRaster::context(width, height, true);
    QPainter painter(&expanded);
    BrushRaster::draw(image, QRectF(0, 0, width, height), painter);
    return expanded;
}
}

bool EditorSession::canEditPixels() const
{
    return canPaint();
}

// Fills the selection or the layer with one colour.
void EditorSession::fillSelection(FillSource source, std::function<void()> done)
{
    const std::optional<ImageLayer> layer = activeLayer();
    if (!canEditPixels() || !layer) {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
        return;
    }
    // A mask takes the palette's red as gray.
    const PaletteColor value = paletteColor(source == FillSource::background);
    // Live text takes the colour itself and stays text.
    if (!m_isMaskSelected && !selection() && recolorText(layer->id, value)) {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
        return;
    }
    const QColor color = m_isMaskSelected ? QColor::fromRgbF(float(value.red), float(value.red), float(value.red)) : value.color();
    applyPixelEdit(*layer, m_isMaskSelected ? QStringLiteral("Fill Mask") : QStringLiteral("Fill"), [color](BrushStroke &edit) { edit.fill(color); }, std::move(done));
}

// Delete with a selection: pixels clear; a mask takes background.
void EditorSession::clearSelectedPixels(std::function<void()> done)
{
    const std::optional<ImageLayer> layer = activeLayer();
    if (!selection() || !canEditPixels() || !layer) {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
        return;
    }
    if (m_isMaskSelected) {
        fillSelection(FillSource::background, std::move(done));
        return;
    }
    if (!layer->asset) {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
        return;
    }
    applyPixelEdit(*layer, QStringLiteral("Clear"), [](BrushStroke &edit) { edit.clearPixels(); }, std::move(done));
}

// The Delete key: clears the selection, else mask or layer.
void EditorSession::deleteKeyPressed()
{
    if (selectedEffect()) {
        removeSelectedEffect();
        return;
    }
    if (selection())
        clearSelectedPixels();
    else
        deleteLayerOrMask();
}

// A raster edit committed off the UI thread, one step.
void EditorSession::applyPixelEdit(const ImageLayer &layer, const QString &name, const std::function<void(BrushStroke &)> &paint, std::function<void()> done)
{
    finishOpacityEdit();
    try {
        // A fill on a mask covers the canvas too.
        std::unique_ptr<BrushStroke> edit = makeRasterEdit(layer, BrushSettings(), true);
        paint(*edit);
        if (edit->patches().empty()) {
            if (done)
                QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
            return;
        }
        commitRasterEdit(std::move(edit), name, {}, std::move(done));
    } catch (const ProjectError &error) {
        setBrushError(QString::fromUtf8(error.what()));
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
    } catch (const ExportError &error) {
        setBrushError(QString::fromUtf8(error.what()));
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
    }
}

void EditorSession::deleteLayerOrMask()
{
    if (selectedEffect()) {
        removeSelectedEffect();
        return;
    }
    const std::optional<ImageLayer> active = activeLayer();
    if (m_isMaskSelected && active && active->mask && m_selectedLayerIDs.size() <= 1)
        deleteLayerMask();
    else
        deleteSelectedLayers();
}

bool EditorSession::canInvert() const
{
    const std::optional<ImageLayer> layer = activeLayer();
    if (!m_document || !layer || m_isProjectBusy || m_isImporting || m_brushStroke || m_pixelMove || m_renamingLayerID || m_showsNewDocument || m_showsImporter
        || m_selectedLayerIDs.size() != 1 || (layer->isGroup && !m_isMaskSelected) || !m_document->effectiveVisibleIDs().contains(layer->id))
        return false;
    const std::optional<DocumentSelection> current = selection();
    if (current && current->isEmpty())
        return false;
    return m_isMaskSelected ? layer->mask && layer->mask->isEnabled : layer->asset.has_value();
}

void EditorSession::invertPixels(std::function<void()> done)
{
    // The caller resumes from the event loop, as after await.
    const auto finish = [this, done] {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
    };
    if (!canInvert()) {
        finish();
        return;
    }
    commitTransform();
    // A pending gradient lands first, as Swift awaits it.
    if (m_gradientEdit) {
        commitGradient([this, done] { invertPixels(done); });
        return;
    }
    if (!canInvert()) {
        finish();
        return;
    }
    const ImageLayer layer = activeLayer().value();
    const bool mask = m_isMaskSelected;
    finishOpacityEdit();
    setIsProjectBusy(true);
    PixelInvert::Job job{QImage(), mask, QTransform(), std::nullopt};
    try {
        // Flattening a painted asset may fail: caught like the rest.
        job.image = mask ? layer.mask.value().asset.image() : layer.asset.value().image();
        const std::optional<DocumentSelection> current = selection();
        if (current)
            job.selection = current->clip(m_document->size());
        // A uniform 1×1 mask cannot hold a partial selection.
        if (mask && job.selection && job.image.size() == QSize(1, 1)) {
            job.image = expandedUniformMask(job.image, layer.asset ? layer.asset->size().width() : int(std::round(layer.size().width())),
                                            layer.asset ? layer.asset->size().height() : int(std::round(layer.size().height())));
        }
        job.pixelToDocument = BrushRaster::pixelToDocument(mask ? layer.maskTransform() : layer.transform, job.image.width(), job.image.height());
    } catch (const std::runtime_error &error) {
        qCWarning(lcApp).noquote() << "cannot invert:" << error.what();
        setBrushError(QString::fromUtf8(error.what()));
        setIsProjectBusy(false);
        finish();
        return;
    }
    m_inverting = Inverting{layer.id, mask, layer.asset ? std::optional(layer.asset->identity()) : std::nullopt,
                            layer.mask ? std::optional(layer.mask->asset.identity()) : std::nullopt, std::move(done)};
    m_inverter.setFuture(QtConcurrent::run([job]() -> Inverted {
        try {
            return Inverted{PixelInvert::run(job), std::nullopt};
        } catch (const ExportError &error) {
            return Inverted{std::nullopt, QString::fromUtf8(error.what())};
        }
    }));
}

void EditorSession::finishInvert()
{
    const Inverted result = m_inverter.result();
    const Inverting pending = std::exchange(m_inverting, std::nullopt).value();
    // The document may have gone meanwhile: nothing to write.
    const int index = m_document ? indexOf(m_document->layers, pending.layerID) : -1;
    if (result.failure) {
        qCWarning(lcApp).noquote() << "cannot invert:" << *result.failure;
        setBrushError(result.failure);
    } else if (index >= 0) {
        // Only the layer the invert was computed from is written.
        const ImageLayer &current = m_document->layers[size_t(index)];
        const std::optional<ImageIdentity> image = current.asset ? std::optional(current.asset->identity()) : std::nullopt;
        const std::optional<ImageIdentity> maskImage = current.mask ? std::optional(current.mask->asset.identity()) : std::nullopt;
        if (image == pending.image && maskImage == pending.maskImage) {
            try {
                if (pending.mask) {
                    const ImportedImage asset = LayerMask::assetFrom(*result.image);
                    beginEdit(QStringLiteral("Invert Mask"));
                    m_document->layers[size_t(index)].mask = current.mask.value().replacing(asset);
                } else {
                    const ImportedImage asset(*result.image, PixelAdjust::thumbnail(*result.image), current.name);
                    beginEdit(QStringLiteral("Invert"));
                    m_document->layers[size_t(index)].asset = asset;
                    // Swift rebuilds the layer without shape, effects and text.
                    m_document->layers[size_t(index)].shape = std::nullopt;
                    m_document->layers[size_t(index)].effects = std::nullopt;
                    m_document->layers[size_t(index)].text = std::nullopt;
                }
                endEdit();
                ++m_brushRevision;
            } catch (const std::runtime_error &error) {
                qCWarning(lcApp).noquote() << "cannot invert:" << error.what();
                setBrushError(QString::fromUtf8(error.what()));
            }
        }
    }
    setIsProjectBusy(false);
    if (pending.done)
        QMetaObject::invokeMethod(this, pending.done, Qt::QueuedConnection);
}

PixelMove::PixelMove(std::shared_ptr<BrushStroke> raster, DocumentSelection origin, bool duplicate)
    : raster(std::move(raster)), origin(std::move(origin)), duplicate(duplicate)
{
}

DocumentSelection PixelMove::movedSelection() const
{
    return DocumentSelection{QTransform::fromTranslate(offset.width(), offset.height()).map(origin.path), origin.antialiased, origin.feather};
}

// Ctrl-drag: starts moving the selected pixels; false with none.
bool EditorSession::beginPixelMove(bool duplicate)
{
    const std::optional<DocumentSelection> current = selection();
    const std::optional<ImageLayer> layer = activeLayer();
    // `canPaint` refuses a pixel move and an empty selection.
    if (!current || !canPaint() || m_isMaskSelected || !layer || !layer->asset)
        return false;
    try {
        std::shared_ptr<BrushStroke> raster = makeRasterEdit(*layer);
        if (!raster->liftSelection())
            return false;
        finishOpacityEdit();
        m_pixelMove = std::make_unique<PixelMove>(std::move(raster), *current, duplicate);
        notify();
        return true;
    } catch (const ProjectError &error) {
        setBrushError(QString::fromUtf8(error.what()));
    } catch (const ExportError &error) {
        setBrushError(QString::fromUtf8(error.what()));
    }
    return false;
}

// The pixels previewed `offset` whole document pixels away.
void EditorSession::movePixels(QSizeF offset)
{
    if (!m_pixelMove)
        return;
    const QSizeF rounded(std::round(offset.width()), std::round(offset.height()));
    try {
        m_pixelMove->raster->moveLifted(rounded, m_pixelMove->duplicate);
    } catch (const ProjectError &error) {
        cancelPixelMove();
        setBrushError(QString::fromUtf8(error.what()));
        return;
    } catch (const ExportError &error) {
        cancelPixelMove();
        setBrushError(QString::fromUtf8(error.what()));
        return;
    }
    m_pixelMove->offset = rounded;
    ++m_brushRevision;
    notify();
}

// The outline to draw, following a pixel move or transform.
std::optional<DocumentSelection> EditorSession::displayedSelection() const
{
    if (m_pixelMove)
        return m_pixelMove->movedSelection();
    // Transforming selected pixels: the outline follows the handles.
    const std::optional<DocumentSelection> current = selection();
    if (m_transformEdit && current) {
        if (const std::optional<QTransform> matrix = floatingSelectionTransform(*m_transformEdit))
            return DocumentSelection{matrix->map(current->path), current->antialiased, current->feather};
    }
    return current;
}

// Pixels and moved outline commit together, one step.
void EditorSession::finishPixelMove(std::function<void()> done)
{
    const auto finish = [this, done] {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
    };
    if (!m_pixelMove || m_isProjectBusy) {
        finish();
        return;
    }
    const auto end = [this, finish] {
        m_pixelMove.reset();
        ++m_brushRevision;
        notify();
        finish();
    };
    if (m_pixelMove->offset.isNull()) {
        end();
        return;
    }
    // The outline stays at its new place while committing.
    const DocumentSelection moved = m_pixelMove->movedSelection();
    commitRasterEdit(m_pixelMove->raster, m_pixelMove->duplicate ? QStringLiteral("Duplicate Pixels") : QStringLiteral("Move Pixels"),
                     [this, moved] { m_document->selection = moved; }, end);
}

void EditorSession::cancelPixelMove()
{
    if (!m_pixelMove)
        return;
    m_pixelMove.reset();
    ++m_brushRevision;
    notify();
}

// Ctrl-arrow: the selected pixels a step, as one undo step.
void EditorSession::nudgePixels(double dx, double dy, std::function<void()> done)
{
    if (!beginPixelMove()) {
        qCWarning(lcApp) << "no selected pixels to nudge";
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
        return;
    }
    movePixels(QSizeF(dx, dy));
    finishPixelMove(std::move(done));
}
