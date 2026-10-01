#include "Document/BrushStroke.h"
#include "Document/DocumentLimits.h"
#include "Document/EditorSession.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore.h"
#include "Logging.h"
#include "Rendering/RasterSnapshot.h"
#include <QtConcurrent>
#include <chrono>
#include <cmath>

// Swift's EditorSession+Brush: the stroke from press to commit.
namespace {
bool sameAsset(const std::optional<ImportedImage> &a, const std::optional<ImportedImage> &b)
{
    return a.has_value() == b.has_value() && (!a || a->identity() == b->identity());
}

bool sameMask(const std::optional<LayerMask> &a, const std::optional<LayerMask> &b)
{
    return a.has_value() == b.has_value() && (!a || a->asset.identity() == b->asset.identity());
}

double uptime()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

bool EditorSession::canPaint() const
{
    const std::optional<ImageLayer> layer = activeLayer();
    const std::optional<DocumentSelection> current = selection();
    // A folder has no pixels: only its mask paints.
    return canEditLayers() && m_selectedLayerIDs.size() == 1 && layer && (!layer->isGroup || m_isMaskSelected) && !(current && current->isEmpty())
        && m_document->effectiveVisibleIDs().contains(layer->id) && (!m_isMaskSelected || (layer->mask && layer->mask->isEnabled))
        && (m_isMaskSelected || !layer->adjustment);
}

std::optional<QString> EditorSession::paintRefusal() const
{
    const std::optional<ImageLayer> layer = activeLayer();
    if (!canEditLayers() || !layer || canPaint())
        return std::nullopt;
    if (m_selectedLayerIDs.size() > 1)
        return QStringLiteral("Several layers are selected. Select just one to paint on it.");
    if (layer->isGroup && !m_isMaskSelected)
        return QStringLiteral("“%1” is a folder, which has no pixels of its own. Paint on a layer inside it, or on the folder’s mask.").arg(layer->name);
    if (!m_document->effectiveVisibleIDs().contains(layer->id))
        return QStringLiteral("“%1” is hidden, or inside a hidden folder. Show it to paint on it.").arg(layer->name);
    if (m_isMaskSelected && !(layer->mask && layer->mask->isEnabled))
        return QStringLiteral("The layer mask is turned off. Shift-click its thumbnail to turn it on, then paint.");
    if (!m_isMaskSelected && layer->adjustment)
        return QStringLiteral("“%1” is an adjustment layer, with no pixels to paint. Paint on its mask instead.").arg(layer->name);
    const std::optional<DocumentSelection> current = selection();
    if (current && current->isEmpty())
        return QStringLiteral("Nothing is selected, so there’s nowhere to paint. Choose Select › Deselect (Ctrl+D) to paint anywhere.");
    return std::nullopt;
}

// A tiled raster edit of pixels or mask, within budget.
std::unique_ptr<BrushStroke> EditorSession::makeRasterEdit(const ImageLayer &layer, const BrushSettings &settings, bool growsMask) const
{
    if (!m_document)
        throw ProjectError(ProjectError::Kind::tooLarge);
    auto stroke = std::make_unique<BrushStroke>(layer, m_isMaskSelected, settings, m_document->size(), growsMask);
    qint64 used = 0;
    for (const ImageLayer &other : m_document->layers) {
        if (other.id == layer.id)
            continue;
        const std::optional<ImportedImage> &image = m_isMaskSelected ? (other.mask ? std::optional(other.mask->asset) : std::nullopt) : other.asset;
        if (image)
            used += qint64(image->size().width()) * image->size().height();
    }
    stroke->pixelLimit = DocumentLimits::documentPixelBudget() - used;
    const std::optional<DocumentSelection> current = selection();
    if (current)
        stroke->selectionClip = current->clip(m_document->size());
    if (!m_isMaskSelected && layer.mask) {
        qint64 maskPixels = 0;
        for (const ImageLayer &other : m_document->layers) {
            if (other.id != layer.id && other.mask)
                maskPixels += qint64(other.mask->asset.size().width()) * other.mask->asset.size().height();
        }
        stroke->pixelLimit = std::min<qint64>(stroke->pixelLimit, DocumentLimits::documentPixelBudget() - maskPixels);
    }
    return stroke;
}

void EditorSession::beginBrush(QPointF point)
{
    if (m_tool == NavigationTool::blur && m_blurMode != BlurToolMode::blur) {
        beginWarp(point);
        return;
    }
    // Spot Healing and Clone Stamp rework pixels, never a mask.
    const bool paintsMasks = m_tool == NavigationTool::brush || m_tool == NavigationTool::blur;
    if (!paintsMasks && (!isBrushTool(m_tool) || m_isMaskSelected))
        return;
    if (!canPaint()) {
        setBrushError(paintRefusal());
        return;
    }
    const ImageLayer layer = activeLayer().value();
    std::optional<QSizeF> sourceOffset;
    if (m_tool == NavigationTool::cloneStamp) {
        sourceOffset = cloneStrokeOffset(point);
        if (!sourceOffset) {
            setBrushError(QStringLiteral("Alt-click where Clone Stamp should copy from first."));
            return;
        }
    }
    finishOpacityEdit();
    try {
        BrushSettings settings = m_brushSettings;
        settings.healing = m_tool == NavigationTool::spotHealing;
        settings.erasing = m_tool == NavigationTool::brush && m_brushMode == BrushToolMode::erase && !m_isMaskSelected;
        settings.healingMode = m_spotHealingMode;
        if (m_isMaskSelected) {
            settings.red = m_maskPaintWhite ? 1 : 0;
            settings.green = settings.red;
            settings.blue = settings.red;
        }
        std::unique_ptr<BrushStroke> stroke = makeRasterEdit(layer, settings, m_tool == NavigationTool::brush);
        if (sourceOffset) {
            stroke->setClone(cloneSample(m_document.value(), *stroke, *sourceOffset));
            if (!stroke->clone())
                return;
            m_cloneOffset = sourceOffset;
        }
        // Blur paints a softened copy of the layer, in place.
        if (m_tool == NavigationTool::blur) {
            stroke->setClone(blurSample(*stroke));
            if (!stroke->clone())
                return;
        }
        stroke->isBlur = m_tool == NavigationTool::blur;
        m_brushStroke = std::move(stroke);
        m_brushStroke->append(point);
        m_brushAnchor = point;
        m_brushPointer = point;
        m_lastBrushPoint = LastBrushPoint{point, layer.id, m_isMaskSelected};
        ++m_brushRevision;
        resumeFileRequests();
        notify();
    } catch (const ProjectError &error) {
        cancelBrush();
        setBrushError(QString::fromUtf8(error.what()));
    } catch (const ExportError &error) {
        cancelBrush();
        setBrushError(QString::fromUtf8(error.what()));
    }
}

void EditorSession::continueBrush(QPointF point)
{
    if (m_warpStroke) {
        m_warpStroke->append(point);
        m_lastBrushPoint->point = point;
        ++m_brushRevision;
        notify();
        return;
    }
    if (!m_brushStroke)
        return;
    m_brushPointer = point;
    const std::optional<QPointF> painted = smoothed(point);
    if (!painted)
        return;
    try {
        m_brushStroke->append(*painted);
        m_lastBrushPoint->point = *painted;
        ++m_brushRevision;
        notify();
    } catch (const ProjectError &error) {
        cancelBrush();
        setBrushError(QString::fromUtf8(error.what()));
    } catch (const ExportError &error) {
        cancelBrush();
        setBrushError(QString::fromUtf8(error.what()));
    }
}

// Photoshop's string: the brush moves once pulled taut.
std::optional<QPointF> EditorSession::smoothed(QPointF point)
{
    if (m_tool != NavigationTool::brush || !(m_brushSettings.smoothing > 0) || !m_brushAnchor)
        return point;
    const double radius = m_brushSettings.smoothing / std::max(0.01, viewport.zoom());
    const QPointF delta = point - *m_brushAnchor;
    const double distance = std::hypot(delta.x(), delta.y());
    if (!(distance > radius))
        return std::nullopt;
    m_brushAnchor = *m_brushAnchor + delta * ((distance - radius) / distance);
    return m_brushAnchor;
}

// Where a Shift-click's line starts, on the same target.
std::optional<QPointF> EditorSession::shiftLineStart() const
{
    if (!m_lastBrushPoint || m_lastBrushPoint->layerID != m_activeLayerID || m_lastBrushPoint->mask != m_isMaskSelected)
        return std::nullopt;
    return m_lastBrushPoint->point;
}

void EditorSession::cancelBrush()
{
    m_warpStroke.reset();
    m_brushStroke.reset();
    m_brushAnchor.reset();
    m_brushPointer.reset();
    ++m_brushRevision;
    resumeFileRequests();
    notify();
}

// Mouse-up's own call, before the next event arrives.
bool EditorSession::finishBrushImmediately()
{
    if (m_warpStroke) {
        if (m_isProjectBusy)
            return false;
        finishWarp();
        return true;
    }
    if (!m_brushStroke)
        return true;
    if (m_isProjectBusy)
        return false;
    try {
        // Smoothing leaves the brush short; the stroke ends there.
        if (m_brushPointer && m_brushAnchor && *m_brushPointer != *m_brushAnchor && m_tool == NavigationTool::brush && m_brushSettings.smoothing > 0)
            m_brushStroke->append(*m_brushPointer);
        m_brushStroke->flush();
        if (m_brushStroke->settings.healing)
            m_brushStroke->heal();
        if (!m_brushStroke->patches().empty())
            commitPaintSnapshot(*m_brushStroke);
    } catch (const ProjectError &error) {
        setBrushError(QString::fromUtf8(error.what()));
    } catch (const ExportError &error) {
        setBrushError(QString::fromUtf8(error.what()));
    }
    cancelBrush();
    return true;
}

void EditorSession::finishBrush()
{
    finishBrushImmediately();
}

// Grown past its layer, or placed already: keeps its place.
LayerMask EditorSession::placedMask(const LayerMask &mask, const ImportedImage &asset, const QRectF &bounds, const LayerTransform &transform,
                                   const BrushStroke &stroke)
{
    LayerMask painted = mask.replacing(asset);
    if (mask.placement || bounds != stroke.sourceRect)
        painted.placement = transform;
    return painted;
}

// Immutable tiles land at once, undo entry included.
void EditorSession::commitPaintSnapshot(const BrushStroke &stroke)
{
    const PaintSnapshot result = stroke.paintSnapshot();
    const int index = m_document ? indexOf(m_document->layers, stroke.layer.id) : -1;
    if (!result.transform.isValid() || index < 0)
        return;
    const ImageLayer current = m_document->layers[index];
    if (!sameAsset(current.asset, stroke.layer.asset) || current.transform != stroke.layer.transform)
        return;
    std::optional<LayerMask> mask = current.mask;
    if (!stroke.isMask && mask && !mask->placement && result.bounds != stroke.sourceRect) {
        // The mask grows with the layer, revealing past it.
        const std::shared_ptr<const RasterSnapshot> raster = RasterSnapshot::replacing(mask->asset, stroke.sourceRect, {}, result.bounds, true);
        mask = mask->replacing(ImportedImage(raster, raster->thumbnail(), mask->asset.name));
    }
    beginEdit(stroke.editName.value_or(stroke.isMask               ? QStringLiteral("Paint Mask")
                                       : stroke.settings.erasing ? QStringLiteral("Erase")
                                       : stroke.isBlur           ? QStringLiteral("Blur")
                                       : stroke.clone()          ? QStringLiteral("Clone Stamp")
                                       : stroke.settings.healing ? QStringLiteral("Spot Healing")
                                                                 : QStringLiteral("Brush Stroke")));
    ImageLayer &layer = m_document->layers[index];
    if (stroke.isMask) {
        layer.mask = current.mask ? placedMask(*current.mask, result.asset, result.bounds, result.transform, stroke) : LayerMask(result.asset);
    } else {
        layer.asset = result.asset;
        layer.transform = result.transform;
        layer.isGroup = false;
        layer.mask = mask;
        // Swift rebuilds the layer; effects stay, shape and text go.
        layer.shape = std::nullopt;
        layer.text = std::nullopt;
    }
    endEdit();
}

// A raster edit assembled off the UI thread; one step.
void EditorSession::commitRasterEdit(std::shared_ptr<const BrushStroke> stroke, const QString &name, std::function<void()> alsoApply, std::function<void()> done)
{
    // The caller resumes from the event loop, like await.
    const auto finish = [this, done] {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
    };
    if (!stroke->committedTransform().isValid()) {
        setBrushError(QString::fromUtf8(ProjectError(ProjectError::Kind::tooLarge).what()));
        finish();
        return;
    }
    setIsProjectBusy(true);
    const BrushCommit::Input input = stroke->commitInput();
    m_committingRaster = CommittingRaster{std::move(stroke), name, std::move(alsoApply), std::move(done)};
    m_rasterCommit.setFuture(QtConcurrent::run([input]() -> RasterMade {
        try {
            return RasterMade{BrushCommit::render(input), std::nullopt};
        } catch (const ExportError &error) {
            return RasterMade{std::nullopt, QString::fromUtf8(error.what())};
        }
    }));
}

void EditorSession::finishRasterCommit()
{
    const RasterMade made = m_rasterCommit.result();
    const CommittingRaster pending = std::move(m_committingRaster.value());
    m_committingRaster = std::nullopt;
    const auto finish = [&] {
        setIsProjectBusy(false);
        ++m_brushRevision;
        notify();
        if (pending.done)
            QMetaObject::invokeMethod(this, pending.done, Qt::QueuedConnection);
    };
    if (made.failure) {
        setBrushError(made.failure);
        finish();
        return;
    }
    const BrushStroke &stroke = *pending.stroke;
    const BrushCommit::Output &result = *made.output;
    try {
        const LayerTransform transform = stroke.transform(result.pixelBounds.translated(stroke.committedBounds().topLeft()));
        if (!transform.isValid())
            throw ProjectError(ProjectError::Kind::tooLarge);
        std::optional<LayerMask> mask = stroke.layer.mask;
        if (!stroke.isMask && mask && !mask->placement)
            mask = mask->replacing(BrushCommit::expandMask(mask->asset, stroke.commitInput(), result.pixelBounds));
        // Never over content that changed underneath it.
        const int index = m_document ? indexOf(m_document->layers, stroke.layer.id) : -1;
        if (index < 0) {
            finish();
            return;
        }
        const ImageLayer current = m_document->layers[index];
        if (!sameAsset(current.asset, stroke.layer.asset) || current.transform != stroke.layer.transform || !sameMask(current.mask, stroke.layer.mask)) {
            finish();
            return;
        }
        beginEdit(pending.name);
        ImageLayer &layer = m_document->layers[index];
        if (stroke.isMask) {
            const QRectF bounds = result.pixelBounds.translated(stroke.committedBounds().topLeft());
            layer.mask = current.mask ? placedMask(*current.mask, result.asset, bounds, transform, stroke) : LayerMask(result.asset);
        } else {
            layer.asset = result.asset;
            layer.transform = transform;
            layer.isGroup = false;
            // Swift rebuilds the layer; effects stay, shape and text go.
            layer.shape = std::nullopt;
            layer.text = std::nullopt;
            if (mask && current.mask)
                mask->isEnabled = current.mask->isEnabled;
            layer.mask = mask;
        }
        if (pending.alsoApply)
            pending.alsoApply();
        endEdit();
    } catch (const ProjectError &error) {
        setBrushError(QString::fromUtf8(error.what()));
    } catch (const ExportError &error) {
        setBrushError(QString::fromUtf8(error.what()));
    }
    finish();
}

bool EditorSession::usesOpacityKeys() const
{
    return isBrushTool(m_tool) || m_tool == NavigationTool::gradient || m_tool == NavigationTool::move;
}

void EditorSession::typeOpacityDigit(int digit, std::optional<double> time)
{
    if (!usesOpacityKeys() || m_brushStroke || m_isProjectBusy || digit < 0 || digit > 9)
        return;
    const double now = time.value_or(uptime());
    int percent = digit == 0 ? 100 : digit * 10;
    if (m_pendingOpacityDigit && now - m_pendingOpacityDigit->time < 0.6) {
        percent = std::max(1, m_pendingOpacityDigit->digit * 10 + digit);
        m_pendingOpacityDigit = std::nullopt;
    } else {
        m_pendingOpacityDigit = PendingOpacityDigit{digit, now};
    }
    const double value = percent / 100.0;
    if (isBrushTool(m_tool)) {
        m_brushSettings.opacity = value;
        notify();
    } else if (m_tool == NavigationTool::gradient) {
        setGradientSettings(GradientSettings{m_gradientSettings.shape, m_gradientSettings.style, m_gradientSettings.reversed, value});
    } else {
        setSelectedLayersOpacity(value);
    }
}

// Shift-[ and Shift-]: hardness in Photoshop's quarters.
void EditorSession::changeBrushHardness(bool increase)
{
    if (m_brushStroke)
        return;
    const double quarter = m_brushSettings.hardness * 4;
    const double step = increase ? std::floor(quarter + 0.001) + 1 : std::ceil(quarter - 0.001) - 1;
    m_brushSettings.hardness = std::min(4.0, std::max(0.0, step)) / 4;
    notify();
}

void EditorSession::changeBrushSize(bool increase)
{
    if (m_brushStroke)
        return;
    // A fifth, at least a pixel: 2 would stay 2.
    const double current = m_brushSettings.diameter;
    const double stepped = increase ? std::max(current + 1, std::round(current * 1.2)) : std::min(current - 1, std::round(current / 1.2));
    m_brushSettings.diameter = std::min(2000.0, std::max(1.0, stepped));
    notify();
}

void EditorSession::setBrushSettings(const BrushSettings &settings)
{
    m_brushSettings = settings;
    refreshGradient();
    notify();
}

void EditorSession::setBrushMode(BrushToolMode mode)
{
    m_brushMode = mode;
    notify();
}

void EditorSession::setBlurMode(BlurToolMode mode)
{
    m_blurMode = mode;
    notify();
}

void EditorSession::setSpotHealingMode(SpotHealingMode mode)
{
    m_spotHealingMode = mode;
    notify();
}

void EditorSession::setMaskPaintWhite(bool white)
{
    m_maskPaintWhite = white;
    refreshGradient();
    notify();
}
