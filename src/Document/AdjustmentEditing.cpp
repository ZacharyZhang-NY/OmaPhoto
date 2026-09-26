#include "Document/EditorSession.h"
#include "Document/LayerGroups.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include <QtConcurrent>

// Swift's AdjustmentEditing.swift: pixels beneath feed histogram and sampling.

void EditorSession::beginAdjustmentEditing(QUuid id, std::function<void()> done)
{
    const int index = m_document ? indexOf(m_document->layers, id) : -1;
    if (m_adjustmentEditingID != id || m_adjustmentOriginal || m_levels || m_hueSaturation || m_filterEdit || index < 0
        || !m_document->layers[size_t(index)].adjustment) {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
        return;
    }
    const LayerAdjustment original = *m_document->layers[size_t(index)].adjustment;
    ProjectSnapshot source = projectSnapshot().value();
    // Records stay for live masks and folders; beneath alone shows.
    QSet<QUuid> underneath;
    for (const LayerHierarchy::Entry &entry : LayerHierarchy::entries(source.manifest.layers)) {
        if (entry.layer.id == id)
            break;
        underneath.insert(entry.layer.id);
    }
    for (ProjectLayerRecord &layer : source.manifest.layers) {
        if (!underneath.contains(layer.id))
            layer.isVisible = false;
    }
    // A watcher per call, as Swift's calls are tasks.
    auto *watcher = new QFutureWatcher<AdjustmentInput>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, id, original, done = std::move(done)] {
        watcher->deleteLater();
        openAdjustmentEditor(id, original, watcher->result());
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
    });
    // A painted layer flattens there, where a failure is caught.
    watcher->setFuture(QtConcurrent::run([source]() -> AdjustmentInput {
        try {
            const QImage image = ImageExporter::render(source).image;
            return {ImportedImage(image, PixelAdjust::thumbnail(image), QStringLiteral("Adjustment input")), std::nullopt};
        } catch (const ExportError &error) {
            return {std::nullopt, QString::fromUtf8(error.what())};
        }
    }));
}

void EditorSession::openAdjustmentEditor(QUuid id, const LayerAdjustment &original, const AdjustmentInput &made)
{
    // Swift's guard after the await; an open editor wins.
    if (m_adjustmentEditingID != id || m_adjustmentOriginal)
        return;
    std::optional<QString> failure = made.failure;
    // Beyond Swift: an edit begun meanwhile keeps its editor.
    if (!failure && (m_levels || m_hueSaturation || m_filterEdit)) {
        qCWarning(lcApp) << "an adjustment's editor gave way to an edit begun meanwhile";
        m_adjustmentEditingID.reset();
        resumeFileRequests();
        notify();
        return;
    }
    if (!failure) {
        try {
            const ImageLayer layer(made.asset.value(), QPointF(0, 0));
            switch (original.kind) {
            case AdjustmentKind::levels:
                m_levels.emplace(layer, std::nullopt);
                m_levels->settings = original.levels;
                countLevelsHistogram();
                break;
            case AdjustmentKind::hsv:
                m_hueSaturation.emplace(layer.id, made.asset.value(), std::nullopt, layer.transform);
                m_hueSaturation->settings = original.resolvedHSV();
                break;
            case AdjustmentKind::curves:
            case AdjustmentKind::exposure:
            case AdjustmentKind::gradientMap:
            case AdjustmentKind::grain: {
                FilterSettings settings;
                settings.curves = original.curves;
                settings.exposure = original.exposure();
                settings.gradientMap = original.gradientMap();
                settings.grain = original.grain();
                m_filterEdit.emplace(filterKind(original.kind).value(), layer, std::nullopt, settings, std::nullopt);
                break;
            }
            }
        } catch (const ExportError &error) {
            failure = QString::fromUtf8(error.what());
        }
    }
    if (failure) {
        m_adjustmentEditingID.reset();
        resumeFileRequests();
        setBrushError(failure);
        return;
    }
    m_adjustmentOriginal = original;
    beginEdit(QStringLiteral("Edit %1 Adjustment").arg(rawValue(original.kind)));
}

// The kind's edit lives as long as the original does.
LayerAdjustment EditorSession::editedAdjustment() const
{
    LayerAdjustment value = m_adjustmentOriginal.value();
    switch (value.kind) {
    case AdjustmentKind::levels: value.levels = m_levels.value().settings; break;
    case AdjustmentKind::hsv: value.hsvSettings = m_hueSaturation.value().settings; break;
    case AdjustmentKind::curves: value.curves = m_filterEdit.value().settings.curves; break;
    case AdjustmentKind::exposure: value.setExposure(m_filterEdit.value().settings.exposure); break;
    case AdjustmentKind::gradientMap: value.setGradientMap(m_filterEdit.value().settings.gradientMap); break;
    case AdjustmentKind::grain: value.setGrain(m_filterEdit.value().settings.grain); break;
    }
    return value;
}

bool EditorSession::previewAdjustmentEditing(bool preview)
{
    if (!m_adjustmentEditingID || !m_adjustmentOriginal)
        return false;
    updateAdjustment(*m_adjustmentEditingID, preview ? editedAdjustment() : *m_adjustmentOriginal);
    return true;
}

bool EditorSession::finishAdjustmentEditing(bool commit)
{
    if (!m_adjustmentEditingID || !m_adjustmentOriginal)
        return false;
    updateAdjustment(*m_adjustmentEditingID, commit ? editedAdjustment() : *m_adjustmentOriginal);
    m_hueSampleMode.reset();
    m_hueTargeting = false;
    m_hueTargetDrag.reset();
    m_levels.reset();
    m_hueSaturation.reset();
    m_filterEdit.reset();
    endEdit();
    m_adjustmentOriginal.reset();
    m_adjustmentEditingID.reset();
    resumeFileRequests();
    ++m_canvasFocusRequest;
    ++m_brushRevision;
    notify();
    return true;
}
