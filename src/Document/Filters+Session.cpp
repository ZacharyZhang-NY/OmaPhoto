#include "Document/BrushStroke.h"
#include "Document/ContentFill.h"
#include "Document/EditorSession.h"
#include "Document/PixelAdjust.h"
#include "Document/SubjectRemoval.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore.h"
#include "Logging.h"
#include <QtConcurrent>

// Swift's session extension in Filters.swift.

// `canAdjustColors` already refuses an empty selection.
bool EditorSession::canContentAwareFill() const
{
    return canAdjustColors() && selection().has_value() && !m_hueSaturation;
}

void EditorSession::beginFilter(FilterKind kind)
{
    if (kind == FilterKind::contentAwareFill && !canContentAwareFill())
        return;
    // Swift beeps; `canAdjustColors` reads the open filter.
    if (m_hueSaturation || !canAdjustColors()) {
        qCWarning(lcApp) << "a filter cannot open here";
        return;
    }
    // A pending gradient lands first, then the filter begins.
    if (m_gradientEdit) {
        commitGradient([this, kind] { beginFilter(kind); });
        return;
    }
    commitTransform();
    cancelCrop();
    cancelLasso();
    const ImageLayer layer = activeLayer().value();
    const QSizeF size = m_document->size();
    const std::optional<DocumentSelection> current = selection();
    FilterSettings settings = m_filterSettings;
    // Gradient Map starts from the palette, as Photoshop's.
    if (kind == FilterKind::gradientMap)
        settings.gradientMap = GradientMapSettings{AdjustmentColor(foregroundColor()), AdjustmentColor(m_backgroundColor)};
    try {
        // Content-Aware Fill extends the layer over the selection's reach.
        std::optional<QRectF> area;
        if (kind == FilterKind::contentAwareFill && current) {
            const QRectF box = current->path.boundingRect().intersected(QRectF(QPointF(0, 0), size));
            if (!box.isEmpty())
                area = box;
        }
        m_filterEdit.emplace(kind, layer, current ? std::optional(current->clip(size)) : std::nullopt, settings, area);
        updateFilter(m_filterEdit->settings, true);
    } catch (const ProjectError &error) {
        setBrushError(QString::fromUtf8(error.what()));
    } catch (const ExportError &error) {
        setBrushError(QString::fromUtf8(error.what()));
    }
}

void EditorSession::updateFilter(const FilterSettings &settings, bool preview)
{
    if (!m_filterEdit || m_filterEdit->committing)
        return;
    FilterEdit &edit = *m_filterEdit;
    edit.settings = settings.normalized();
    edit.preview = preview;
    // A bigger blur needs more room round the layer.
    if (FilterEdit::blurMargin(edit.kind, edit.settings) > edit.grownMargin) {
        try {
            edit.growForBlur();
            edit.preparedPreview.reset();
        } catch (const ProjectError &error) {
            setBrushError(QString::fromUtf8(error.what()));
        } catch (const ExportError &error) {
            setBrushError(QString::fromUtf8(error.what()));
        }
    }
    // An adjustment layer takes the settings; its pixels stay.
    if (previewAdjustmentEditing(preview)) {
        notify();
        return;
    }
    // Automatic kinds render once.
    if (isAutomatic(edit.kind) && edit.preparedPreview && edit.preparedSettings == edit.settings) {
        ++m_brushRevision;
        notify();
        return;
    }
    if (!preview) {
        // A job behind another edit's run is Swift's running task.
        if (m_filterRendering && m_filterPreviewFor == edit.id)
            edit.pending.reset();
        edit.preparedPreview.reset();
        ++m_brushRevision;
        notify();
        return;
    }
    edit.pending = edit.previewJob();
    renderFilterPreview();
}

// Renders the newest job; one that arrives mid-render waits.
void EditorSession::renderFilterPreview()
{
    if (!m_filterEdit || !m_filterEdit->pending)
        return;
    FilterEdit &edit = *m_filterEdit;
    edit.preparing = true;
    edit.previewError = std::nullopt;
    // One worker at a time: a job waits its turn.
    if (!m_filterRendering) {
        const FilterJob job = *std::exchange(edit.pending, std::nullopt);
        m_filterPreviewFor = edit.id;
        m_filterRendering = true;
        m_filterPreview.setFuture(QtConcurrent::run([job]() -> Filtered {
            try {
                return Filtered{PixelFilter::run(job), std::nullopt, job.settings};
            } catch (const ContentFillError &error) {
                return Filtered{std::nullopt, QString::fromUtf8(error.what()), job.settings};
            } catch (const SubjectRemovalError &error) {
                return Filtered{std::nullopt, QString::fromUtf8(error.what()), job.settings};
            } catch (const ExportError &error) {
                return Filtered{std::nullopt, QString::fromUtf8(error.what()), job.settings};
            } catch (const ProjectError &error) {
                // Settings past their bounds: Black & White, Color Balance.
                return Filtered{std::nullopt, QString::fromUtf8(error.what()), job.settings};
            }
        }));
    }
    notify();
}

void EditorSession::finishFilterPreview()
{
    m_filterRendering = false;
    const Filtered result = m_filterPreview.result();
    // An edit cancelled meanwhile takes nothing; a new one waited.
    if (m_filterEdit && m_filterEdit->id == m_filterPreviewFor) {
        FilterEdit &edit = *m_filterEdit;
        edit.preparing = false;
        edit.previewError = result.failure;
        if (edit.preview || isAutomatic(edit.kind)) {
            edit.preparedPreview = result.image;
            edit.preparedSettings = result.settings;
            ++m_brushRevision;
        }
        notify();
    }
    renderFilterPreview();
    // Each waiting commit looks again; a gone edit's just returns.
    for (const Committing &waiter : std::exchange(m_filterCommitWaiting, {})) {
        if (m_filterEdit && m_filterEdit->id == waiter.editID)
            commitFilter(waiter.done);
        else
            QMetaObject::invokeMethod(this, waiter.done, Qt::QueuedConnection);
    }
}

void EditorSession::cancelFilter()
{
    // A Gradient Map colour being picked goes with the panel.
    if (m_colorPicker && m_colorPicker->target.kind == ColorPickerTarget::Kind::gradientMap)
        closeColorPicker(false);
    if (finishAdjustmentEditing(false) || !m_filterEdit || m_filterEdit->committing)
        return;
    m_filterEdit.reset();
    ++m_brushRevision;
    notify();
}

void EditorSession::commitFilter(std::function<void()> done)
{
    // The caller resumes from the event loop, as after await.
    const auto finish = [this, done] {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
    };
    if (m_colorPicker && m_colorPicker->target.kind == ColorPickerTarget::Kind::gradientMap)
        closeColorPicker(true);
    if (finishAdjustmentEditing(true) || !m_filterEdit || m_filterEdit->committing) {
        finish();
        return;
    }
    FilterEdit &edit = *m_filterEdit;
    // An automatic filter commits what its preview made, once delivered.
    if (isAutomatic(edit.kind) && edit.preparing) {
        m_filterCommitWaiting.push_back(Committing{edit.id, done ? done : [] {}});
        return;
    }
    if (isAutomatic(edit.kind) && (!edit.preparedPreview || edit.previewError)) {
        finish();
        return;
    }
    // Nothing to change closes as Cancel does, with no step.
    if ((edit.kind == FilterKind::lensCorrection && edit.settings.distortion == 0)
        || (edit.kind == FilterKind::exposure && edit.settings.exposure == ExposureSettings())
        || (edit.kind == FilterKind::grain && edit.settings.grain.amount == 0)) {
        cancelFilter();
        finish();
        return;
    }
    edit.committing = true;
    // Swift cancels the running preview: its result and queue drop.
    edit.pending.reset();
    m_filterPreviewFor = QUuid();
    m_filterSettings = edit.settings;
    setIsProjectBusy(true);
    m_committingFilter = Committing{edit.id, std::move(done)};
    // A mask hides the background, which can come back.
    if (edit.kind == FilterKind::removeBackground) {
        commitBackgroundMask(edit);
        return;
    }
    const std::optional<QImage> cached = isAutomatic(edit.kind) && edit.preparedSettings == edit.settings ? edit.preparedPreview : std::nullopt;
    const bool spreads = edit.kind == FilterKind::gaussianBlur || edit.kind == FilterKind::motionBlur;
    // A painted layer flattens in the worker, which catches failures.
    m_filterCommit.setFuture(QtConcurrent::run([kind = edit.kind, original = edit.original, grownImage = edit.grownImage, settings = edit.settings,
                                                selection = edit.selection, mapping = edit.mapping, seed = edit.seed, cached,
                                                grown = edit.grownTransform, spreads]() -> FilterMade {
        try {
            QImage image = cached ? *cached : PixelFilter::run(FilterJob{kind, grownImage ? *grownImage : original.image(), settings, 1, selection, mapping, seed});
            std::optional<LayerTransform> placed = grown;
            // A blur is cut back to the pixels it left.
            if (spreads && grown) {
                const PixelFilter::Trimmed trimmed = PixelFilter::trimmed(image, *grown);
                image = trimmed.image;
                placed = trimmed.transform;
            }
            return FilterMade{ImportedImage(image, PixelAdjust::thumbnail(image), rawValue(kind)), placed, std::nullopt};
        } catch (const ContentFillError &error) {
            return FilterMade{std::nullopt, std::nullopt, QString::fromUtf8(error.what())};
        } catch (const ExportError &error) {
            return FilterMade{std::nullopt, std::nullopt, QString::fromUtf8(error.what())};
        } catch (const ProjectError &error) {
            return FilterMade{std::nullopt, std::nullopt, QString::fromUtf8(error.what())};
        }
    }));
}

void EditorSession::commitBackgroundMask(const FilterEdit &edit)
{
    // A mask in the layer's own grid is multiplied in.
    const int index = indexOf(m_document->layers, edit.layerID);
    std::optional<ImportedImage> existing;
    if (index >= 0 && m_document->layers[index].mask) {
        const LayerMask &owned = *m_document->layers[index].mask;
        if (!owned.placement && owned.asset.size() == edit.original.size())
            existing = owned.asset;
    }
    // Painted pixels and masks flatten in the worker.
    m_filterCommit.setFuture(QtConcurrent::run([original = edit.original, existing, selection = edit.selection, mapping = edit.mapping,
                                                settings = edit.settings]() -> FilterMade {
        try {
            const QImage source = original.image();
            const std::optional<QImage> under = existing ? std::optional(existing->image()) : std::nullopt;
            QImage mask = SubjectRemoval::subjectMask(source, under, settings);
            // With a selection, only its part of the mask changes.
            if (selection) {
                QImage base = under ? *under : BrushRaster::context(source.width(), source.height(), true);
                if (!under)
                    base.fill(255);
                mask = PixelAdjust::blend(mask, base, *selection, mapping, true);
            }
            return FilterMade{LayerMask::assetFrom(mask), std::nullopt, std::nullopt};
        } catch (const SubjectRemovalError &error) {
            return FilterMade{std::nullopt, std::nullopt, QString::fromUtf8(error.what())};
        } catch (const ExportError &error) {
            return FilterMade{std::nullopt, std::nullopt, QString::fromUtf8(error.what())};
        }
    }));
}

void EditorSession::finishFilterCommit()
{
    const FilterMade made = m_filterCommit.result();
    const FilterEdit edit = std::exchange(m_filterEdit, std::nullopt).value();
    const Committing pending = std::exchange(m_committingFilter, std::nullopt).value();
    // The preview stayed up till now: the canvas never flashed.
    const auto finish = [&] {
        ++m_brushRevision;
        setIsProjectBusy(false);
        if (pending.done)
            QMetaObject::invokeMethod(this, pending.done, Qt::QueuedConnection);
    };
    if (made.failure) {
        setBrushError(made.failure);
        finish();
        return;
    }
    const int index = m_document ? indexOf(m_document->layers, edit.layerID) : -1;
    if (index < 0) {
        finish();
        return;
    }
    const ImageLayer current = m_document->layers[index];
    if (!current.asset || current.asset->identity() != edit.original.identity() || current.transform != edit.transform) {
        finish();
        return;
    }
    if (edit.kind == FilterKind::removeBackground) {
        beginEdit(rawValue(edit.kind));
        // Swift keeps the mask's switches; a placement would misplace it.
        LayerMask mask = current.mask ? current.mask->replacing(*made.asset) : LayerMask(*made.asset);
        mask.isEnabled = true;
        mask.placement = std::nullopt;
        m_document->layers[index].mask = mask;
        m_isMaskSelected = true;
        endEdit();
        finish();
        return;
    }
    try {
        // The mask moves onto the new box, edge tone beyond.
        std::optional<LayerMask> mask = current.mask;
        if (edit.grownTransform && current.mask && !current.mask->placement
            && (current.mask->asset.size().width() > 1 || current.mask->asset.size().height() > 1)) {
            LayerMask enabled = *current.mask;
            enabled.isEnabled = true;
            const QSize size = made.asset->size();
            const std::optional<QImage> carried = enabled.clipImage(current.transform, made.transform.value(), size.width(), size.height());
            if (!carried)
                throw ExportError(ExportError::Kind::render);
            mask = current.mask->replacing(LayerMask::assetFrom(*carried));
        }
        beginEdit(rawValue(edit.kind));
        ImageLayer layer = current;
        layer.asset = made.asset;
        layer.transform = made.transform.value_or(current.transform);
        layer.mask = mask;
        // Swift rebuilds the layer without shape, effects and text.
        layer.shape = std::nullopt;
        layer.effects = std::nullopt;
        layer.text = std::nullopt;
        m_document->layers[index] = layer;
        endEdit();
    } catch (const ExportError &error) {
        setBrushError(QString::fromUtf8(error.what()));
    }
    finish();
}
