#include "Document/EditorSession.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include <QtConcurrent>

namespace {
// Swift's adjustedPixels: the render, or why it failed.
SessionJobs::Adjusted adjusted(const HueSaturationJob &job)
{
    try {
        return {HueSaturationFilter::run(job), std::nullopt};
    } catch (const ExportError &error) {
        return {std::nullopt, QString::fromUtf8(error.what())};
    }
}
}

// Swift's session extension in HueSaturation.swift.
bool EditorSession::canAdjustColors() const
{
    return canAdjust(false);
}

// Vignette also paints an empty layer, without pixels yet.
bool EditorSession::canVignette() const
{
    const std::optional<ImageLayer> layer = activeLayer();
    return canAdjust(!(layer && layer->adjustment));
}

bool EditorSession::canAdjust(bool allowingEmpty) const
{
    const std::optional<ImageLayer> layer = activeLayer();
    const std::optional<DocumentSelection> current = selection();
    return !m_levels && !m_filterEdit && m_document && layer && !m_isProjectBusy && !m_isImporting && !m_brushStroke && !m_pixelMove && !m_renamingLayerID && !m_showsNewDocument
        && !m_showsImporter && m_selectedLayerIDs.size() == 1 && !layer->isGroup && !m_isMaskSelected && (layer->asset || allowingEmpty)
        && m_document->effectiveVisibleIDs().contains(layer->id) && !(current && current->isEmpty());
}

void EditorSession::beginHueSaturation()
{
    // Swift beeps.
    if (m_hueSaturation || !canAdjustColors()) {
        qCWarning(lcApp) << "Hue/Saturation cannot open here";
        return;
    }
    // A pending gradient lands first, as beginLevels waits.
    if (m_gradientEdit) {
        commitGradient([this] { beginHueSaturation(); });
        return;
    }
    commitTransform();
    const ImageLayer layer = activeLayer().value();
    const std::optional<DocumentSelection> current = selection();
    try {
        m_hueSaturation.emplace(layer.id, layer.asset.value(), current ? std::optional(current->clip(m_document->size())) : std::nullopt,
                                layer.transform);
    } catch (const ExportError &error) {
        setBrushError(QString::fromUtf8(error.what()));
        return;
    }
    notify();
}

void EditorSession::updateHueSaturation(const HueSaturationSettings &settings, bool preview)
{
    if (!m_hueSaturation)
        return;
    HueSaturationEdit &edit = *m_hueSaturation;
    edit.settings = settings;
    edit.preview = preview;
    // An adjustment layer takes the settings; its pixels stay.
    if (previewAdjustmentEditing(preview)) {
        notify();
        return;
    }
    if (!preview || settings.isIdentity()) {
        // Swift cancels the running preview: its result drops.
        m_hueSaturationPreviewFor = QUuid();
        m_hueSaturationPending.reset();
        edit.setPreview(std::nullopt);
        ++m_brushRevision;
        notify();
        return;
    }
    m_hueSaturationPending = HueSaturationJob{edit.previewSource, settings, edit.selection, edit.previewPixelToDocument, false};
    renderPendingHuePreview();
    notify();
}

// Renders the newest job; one that arrives mid-render waits.
void EditorSession::renderPendingHuePreview()
{
    if (!m_hueSaturation || !m_hueSaturationPending || m_hueSaturationRendering)
        return;
    const HueSaturationJob job = *std::exchange(m_hueSaturationPending, std::nullopt);
    m_hueSaturationPreviewFor = m_hueSaturation->id;
    m_hueSaturationRendering = true;
    m_hueSaturationTask.setFuture(QtConcurrent::run([job] { return adjusted(job); }));
}

void EditorSession::finishHueSaturationPreview()
{
    m_hueSaturationRendering = false;
    const Adjusted made = m_hueSaturationTask.result();
    // Swift's adjustedPixels shows a failure whichever edit asked.
    if (made.failure)
        setBrushError(made.failure);
    if (m_hueSaturation && m_hueSaturation->id == std::exchange(m_hueSaturationPreviewFor, QUuid()) && made.pixels) {
        m_hueSaturation->setPreview(made.pixels->image);
        ++m_brushRevision;
        notify();
    }
    renderPendingHuePreview();
}

void EditorSession::commitHueSaturation(std::function<void()> done)
{
    // The caller resumes from the event loop, as after await.
    const auto finish = [this, done] {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
    };
    if (finishAdjustmentEditing(true) || !m_hueSaturation || m_committingHueSaturation) {
        finish();
        return;
    }
    m_hueSampleMode.reset();
    m_hueTargeting = false;
    m_hueTargetDrag.reset();
    m_hueSaturationPending.reset();
    m_hueSaturationPreviewFor = QUuid();
    const HueSaturationEdit &edit = *m_hueSaturation;
    if (edit.settings.isIdentity()) {
        m_hueSaturation.reset();
        ++m_brushRevision;
        notify();
        finish();
        return;
    }
    // The preview stays on screen until the pixels land.
    m_committingHueSaturation.emplace(CommittingHue{edit, std::move(done)});
    setIsProjectBusy(true);
    // A painted layer flattens there, where a failure is caught.
    m_hueSaturationCommit.setFuture(QtConcurrent::run([original = edit.original, settings = edit.settings, selection = edit.selection,
                                                       mapping = edit.pixelToDocument]() -> Adjusted {
        try {
            return adjusted(HueSaturationJob{original.image(), settings, selection, mapping, true});
        } catch (const ExportError &error) {
            return {std::nullopt, QString::fromUtf8(error.what())};
        }
    }));
}

void EditorSession::finishHueSaturationCommit()
{
    const Adjusted made = m_hueSaturationCommit.result();
    const CommittingHue committing = std::exchange(m_committingHueSaturation, std::nullopt).value();
    const HueSaturationEdit &edit = committing.edit;
    const int index = m_document ? indexOf(m_document->layers, edit.layerID) : -1;
    if (made.failure)
        setBrushError(made.failure);
    else if (index >= 0 && m_document->layers[index].asset && m_document->layers[index].asset->identity() == edit.original.identity()) {
        beginEdit(QStringLiteral("Hue/Saturation"));
        ImageLayer &layer = m_document->layers[index];
        layer.asset = ImportedImage(made.pixels->image, made.pixels->thumbnail.value_or(made.pixels->image), layer.name);
        // Swift rebuilds the layer without shape, effects and text.
        layer.shape = std::nullopt;
        layer.effects = std::nullopt;
        layer.text = std::nullopt;
        endEdit();
    }
    // Swift's defers: the project frees, then the edit goes.
    setIsProjectBusy(false);
    m_hueSaturation.reset();
    ++m_brushRevision;
    notify();
    if (committing.done)
        QMetaObject::invokeMethod(this, committing.done, Qt::QueuedConnection);
}

std::optional<double> EditorSession::sampledHue(QPointF point) const
{
    const std::optional<PaletteColor> color = sampleCompositeColor(point);
    if (!color)
        return std::nullopt;
    const PickerHSB hsb(*color);
    return hsb.saturation > 0.02 ? std::optional(hsb.hue) : std::nullopt;
}

void EditorSession::sampleHueRange(QPointF point)
{
    if (!m_hueSaturation || !m_hueSampleMode)
        return;
    HueSaturationSettings settings = m_hueSaturation->settings;
    const std::optional<double> hue = settings.range != ColorRange::master && !settings.colorize ? sampledHue(point) : std::nullopt;
    // Swift beeps: Master, Colorize, or a gray pixel.
    if (!hue) {
        qCWarning(lcApp) << "no hue to sample here";
        return;
    }
    HueBand band = settings.band();
    if (*m_hueSampleMode == HueSampleMode::replace)
        band = band.centered(*hue);
    else if (*m_hueSampleMode == HueSampleMode::add)
        band.include(*hue);
    else
        band.exclude(*hue);
    settings.setBand(band);
    updateHueSaturation(settings, m_hueSaturation->preview);
}

bool EditorSession::beginHueTargeting(QPointF point)
{
    const std::optional<double> hue = m_hueSaturation && m_hueTargeting && !m_hueSaturation->settings.colorize ? sampledHue(point) : std::nullopt;
    // Swift beeps.
    if (!hue) {
        qCWarning(lcApp) << "no colour to target here";
        return false;
    }
    HueSaturationSettings settings = m_hueSaturation->settings;
    // Swift's max(by:): the first of the strongest claims.
    ColorRange range = colorRanges.front();
    for (const ColorRange each : colorRanges) {
        if (settings.weight(each, *hue) > settings.weight(range, *hue))
            range = each;
    }
    settings.range = range;
    const RangeAdjustment adjustment = settings.adjustments.contains(range) ? settings.adjustments.at(range) : RangeAdjustment();
    m_hueTargetDrag = HueTargetDrag{range, adjustment.hue, adjustment.saturation};
    updateHueSaturation(settings, m_hueSaturation->preview);
    return true;
}

void EditorSession::dragHueTargeting(double viewDelta, bool adjustsHue)
{
    if (!m_hueSaturation || !m_hueTargetDrag)
        return;
    HueSaturationSettings settings = m_hueSaturation->settings;
    const HueTargetDrag drag = *m_hueTargetDrag;
    RangeAdjustment &adjustment = settings.adjustments[drag.range];
    if (adjustsHue)
        adjustment.hue = std::min(180.0, std::max(-180.0, drag.hue + viewDelta / 2));
    else
        adjustment.saturation = std::min(100.0, std::max(-100.0, drag.saturation + viewDelta / 2));
    updateHueSaturation(settings, m_hueSaturation->preview);
}

void EditorSession::endHueTargeting()
{
    m_hueTargetDrag.reset();
}

void EditorSession::cancelHueSaturation()
{
    if (finishAdjustmentEditing(false))
        return;
    const bool armed = m_hueSampleMode || m_hueTargeting;
    m_hueSampleMode.reset();
    m_hueTargeting = false;
    m_hueTargetDrag.reset();
    if (!m_hueSaturation) {
        if (armed)
            notify();
        return;
    }
    // A running preview drops: its edit is gone.
    m_hueSaturationPending.reset();
    m_hueSaturation.reset();
    ++m_brushRevision;
    notify();
}

void EditorSession::setHueSampleMode(std::optional<HueSampleMode> mode)
{
    if (m_hueSampleMode == mode)
        return;
    m_hueSampleMode = mode;
    notify();
}

void EditorSession::setHueTargeting(bool targeting)
{
    if (m_hueTargeting == targeting)
        return;
    m_hueTargeting = targeting;
    notify();
}
