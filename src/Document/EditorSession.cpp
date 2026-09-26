#include "Document/EditorSession.h"
#include "Logging.h"
#include <cmath>

EditorSession::EditorSession(QObject *parent) : QObject(parent), m_busyTimer(this)
{
    // A coarse timer may fire a twentieth early.
    m_busyTimer.setTimerType(Qt::PreciseTimer);
    m_busyTimer.setSingleShot(true);
    m_busyTimer.setInterval(busyIndicatorDelay);
    connect(&m_busyTimer, &QTimer::timeout, this, [this] {
        m_showsBusy = true;
        notify();
    });
    connect(&m_decoder, &QFutureWatcher<Decoded>::finished, this, &EditorSession::finishDecode);
    connect(&m_baker, &QFutureWatcher<Baked>::finished, this, &EditorSession::finishBake);
    connect(&m_inverter, &QFutureWatcher<Inverted>::finished, this, &EditorSession::finishInvert);
    connect(&m_wand, &QFutureWatcher<Wanded>::finished, this, &EditorSession::finishWand);
    connect(&m_subject, &QFutureWatcher<Subjected>::finished, this, &EditorSession::finishSubject);
    connect(&m_filterPreview, &QFutureWatcher<Filtered>::finished, this, &EditorSession::finishFilterPreview);
    connect(&m_filterCommit, &QFutureWatcher<FilterMade>::finished, this, &EditorSession::finishFilterCommit);
    connect(&m_cropCommit, &QFutureWatcher<Cropped>::finished, this, &EditorSession::finishCropCommit);
    connect(&m_levelsHistogram, &QFutureWatcher<std::optional<LevelsHistogram>>::finished, this, &EditorSession::finishLevelsHistogram);
    connect(&m_levelsPreview, &QFutureWatcher<std::optional<QImage>>::finished, this, &EditorSession::finishLevelsPreview);
    connect(&m_levelsCommit, &QFutureWatcher<Leveled>::finished, this, &EditorSession::finishLevelsCommit);
    connect(&m_hueSaturationTask, &QFutureWatcher<Adjusted>::finished, this, &EditorSession::finishHueSaturationPreview);
    connect(&m_hueSaturationCommit, &QFutureWatcher<Adjusted>::finished, this, &EditorSession::finishHueSaturationCommit);
    connect(&m_rasterCommit, &QFutureWatcher<RasterMade>::finished, this, &EditorSession::finishRasterCommit);
}

void EditorSession::setActiveLayerID(std::optional<QUuid> id)
{
    // Another layer: its pixels are the target again.
    if (id != m_activeLayerID)
        m_isMaskSelected = false;
    m_activeLayerID = id;
    m_selectedLayerIDs = id ? QSet<QUuid>{*id} : QSet<QUuid>();
    notify();
}

std::optional<ImageLayer> EditorSession::activeLayer() const
{
    if (!m_document || !m_activeLayerID)
        return std::nullopt;
    const auto found = std::find_if(m_document->layers.begin(), m_document->layers.end(),
                                    [&](const ImageLayer &layer) { return layer.id == *m_activeLayerID; });
    return found == m_document->layers.end() ? std::nullopt : std::optional(*found);
}

void EditorSession::setIsProjectBusy(bool busy)
{
    m_isProjectBusy = busy;
    // Freed waiters each look again when their turn comes.
    if (!busy) {
        for (const std::function<void()> &waiter : std::exchange(m_projectWaiters, {}))
            QMetaObject::invokeMethod(this, [this, waiter] { waitForProjectAccess(waiter); }, Qt::QueuedConnection);
    }
    resumeFileRequests();
    updateBusyIndicator();
    notify();
}

void EditorSession::setShowsNewDocument(bool shows)
{
    m_showsNewDocument = shows;
    resumeFileRequests();
    notify();
}

void EditorSession::setShowsImporter(bool shows)
{
    m_showsImporter = shows;
    resumeFileRequests();
    notify();
}

void EditorSession::setIsImporting(bool importing)
{
    m_isImporting = importing;
    resumeFileRequests();
    notify();
}

void EditorSession::setImportError(std::optional<QString> error)
{
    m_importError = std::move(error);
    resumeFileRequests();
    notify();
}

void EditorSession::setBrushError(std::optional<QString> error)
{
    m_brushError = std::move(error);
    notify();
}

void EditorSession::setRenamingLayerID(std::optional<QUuid> id)
{
    m_renamingLayerID = id;
    resumeFileRequests();
    notify();
}

void EditorSession::setAdjustmentEditingID(std::optional<QUuid> id)
{
    m_adjustmentEditingID = id;
    resumeFileRequests();
    notify();
}

void EditorSession::selectLayer(std::optional<QUuid> id)
{
    dropEffectSelection();
    if (id != m_activeLayerID && !finishText())
        return;
    if (m_brushStroke || m_warpStroke || m_levels)
        return;
    if (id != m_activeLayerID) {
        commitTransform();
        resolveGradient();
    }
    setActiveLayerID(id);
}

void EditorSession::selectTool(NavigationTool value)
{
    if (m_tool != value && !finishText())
        return;
    if (m_isProjectBusy || m_brushStroke || m_warpStroke || m_levels)
        return;
    if (m_tool != value) {
        commitTransform();
        cancelCrop();
        resolveGradient();
        cancelLasso();
        cancelShape();
    }
    // Each family's tip parks on leaving; sessions begin in 0.
    const int from = tipFamily(m_tool), to = tipFamily(value);
    if (from != to) {
        const ParkedTip parked = m_parkedBrushTips.at(to);
        m_parkedBrushTips[from] = ParkedTip{m_brushSettings.diameter, m_brushSettings.hardness, m_brushSettings.opacity};
        m_brushSettings.diameter = parked.diameter;
        m_brushSettings.hardness = parked.hardness;
        m_brushSettings.opacity = parked.opacity;
    }
    m_tool = value;
    // The Crop tool opens on the whole canvas, freely.
    if (value == NavigationTool::crop && !m_cropRect && m_document) {
        m_cropRatioChoice = QStringLiteral("Free");
        m_cropRect = QRectF(QPointF(0, 0), m_document->size());
    }
    notify();
}

int EditorSession::tipFamily(NavigationTool tool)
{
    return tool == NavigationTool::cloneStamp ? 1 : tool == NavigationTool::blur ? 2 : 0;
}

// Tab: the tool's next mode; others arrive with their tools.
void EditorSession::cycleToolMode()
{
    if (m_isProjectBusy || m_brushStroke || m_warpStroke)
        return;
    if (m_tool == NavigationTool::marquee)
        toggleMarqueeKind();
    else if (m_tool == NavigationTool::lasso)
        toggleLassoKind();
    else if (m_tool == NavigationTool::shape)
        toggleShapeKind();
    else if (m_tool == NavigationTool::brush)
        setBrushMode(m_brushMode == BrushToolMode::paint ? BrushToolMode::erase : BrushToolMode::paint);
    else if (m_tool == NavigationTool::gradient)
        setGradientSettings(GradientSettings{allGradientShapes[(size_t(m_gradientSettings.shape) + 1) % allGradientShapes.size()], m_gradientSettings.style,
                                             m_gradientSettings.reversed, m_gradientSettings.opacity});
    else if (m_tool == NavigationTool::blur)
        setBlurMode(allBlurToolModes[(size_t(m_blurMode) + 1) % allBlurToolModes.size()]);
    else if (m_tool == NavigationTool::spotHealing)
        setSpotHealingMode(allSpotHealingModes[(size_t(m_spotHealingMode) + 1) % allSpotHealingModes.size()]);
    else if (m_tool == NavigationTool::cloneStamp)
        setCloneSettings(CloneSettings{m_cloneSettings.aligned, !m_cloneSettings.sampleAllLayers});
}

bool EditorSession::canUseHistory() const
{
    return !m_selectionAmountOperation && !m_textDraft && !m_isProjectBusy && !m_isImporting && !m_brushStroke && !m_warpStroke && !m_showsNewDocument
        && !m_showsImporter && !m_renamingLayerID && !m_importError && !m_transformEdit && !m_levels;
}

void EditorSession::undo()
{
    // As Photoshop, the first Undo discards a pending gradient.
    if (m_gradientEdit) {
        cancelGradient();
        return;
    }
    if (!canUndo())
        return;
    if (const std::optional<DocumentHistory::Snapshot> snapshot = history.undo())
        restore(*snapshot);
}

void EditorSession::redo()
{
    if (!canRedo())
        return;
    if (const std::optional<DocumentHistory::Snapshot> snapshot = history.redo())
        restore(*snapshot);
}

void EditorSession::restore(const DocumentHistory::Snapshot &snapshot)
{
    cancelCrop();
    cancelGradient();
    const auto canvas = [](const std::optional<CanvasDocument> &document) { return document ? std::optional(document->id) : std::nullopt; };
    const bool changedCanvas = canvas(m_document) != canvas(snapshot.document);
    // The mask stays the target while layer and mask remain.
    const bool keepsMaskTarget = m_isMaskSelected && m_activeLayerID == snapshot.activeLayerID;
    m_document = snapshot.document;
    setActiveLayerID(snapshot.activeLayerID);
    const std::optional<ImageLayer> active = activeLayer();
    m_isMaskSelected = keepsMaskTarget && active && active->mask;
    if (changedCanvas && m_document)
        viewport.fit(m_document->size());
    qCInfo(lcApp) << "restored a history snapshot of" << (m_document ? int(m_document->layers.size()) : 0) << "layers";
    notify();
}

void EditorSession::beginEdit(const QString &name)
{
    history.begin(name, m_document, m_activeLayerID);
    // An open transaction closes undo and redo: observers must know.
    notify();
}

void EditorSession::endEdit()
{
    history.end(m_document, m_activeLayerID);
    notify();
}

bool EditorSession::canEditLayers() const
{
    return !m_selectionAmountOperation && !m_textDraft && m_document && !m_brushStroke && !m_warpStroke && !m_isProjectBusy && !m_isImporting
        && !m_showsNewDocument && !m_showsImporter
        && !m_renamingLayerID && !m_transformEdit && !m_cropRect && !m_gradientEdit && !m_filterEdit && !m_pixelMove && !m_levels
        && !m_hueSaturation && !m_adjustmentEditingID;
}

void EditorSession::insert(const ImportedImage &asset, std::optional<QPointF> centeredAt)
{
    beginEdit(QStringLiteral("Import Image"));
    if (!m_document) {
        m_document = CanvasDocument(asset.size().width(), asset.size().height());
        viewport.fit(m_document->size());
    }
    const QPointF center = centeredAt.value_or(QPointF(m_document->width / 2.0, m_document->height / 2.0));
    ImageLayer layer(asset, QPointF(std::floor(center.x() - asset.size().width() / 2.0), std::floor(center.y() - asset.size().height() / 2.0)));
    // Beside the active layer, or inside the active folder.
    const std::optional<ImageLayer> active = activeLayer();
    layer.parentID = active && active->isGroup ? m_activeLayerID : active ? active->parentID : std::nullopt;
    if (layer.parentID)
        m_collapsedGroupIDs.remove(*layer.parentID);
    m_document->layers.push_back(layer);
    setActiveLayerID(layer.id);
    endEdit();
}

void EditorSession::createDocument(int width, int height, bool emptyLayer)
{
    if (m_isProjectBusy || m_isImporting || width < 1 || width > 30'000 || height < 1 || height > 30'000)
        return;
    commitTransform();
    beginEdit(QStringLiteral("New Canvas"));
    CanvasDocument document(width, height);
    // File > New starts with a selected blank "Layer 1".
    std::optional<QUuid> selected;
    if (emptyLayer) {
        document.layers.push_back(ImageLayer(QStringLiteral("Layer 1"), document.size()));
        selected = document.layers.back().id;
    }
    m_document = document;
    setActiveLayerID(selected);
    m_renamingLayerID = std::nullopt;
    viewport.fit(m_document->size());
    m_showsNewDocument = false;
    // A closed sheet or an ended rename frees file requests.
    resumeFileRequests();
    qCInfo(lcApp) << "new canvas" << width << "x" << height;
    endEdit();
}

void EditorSession::fit()
{
    if (!m_document)
        return;
    const CanvasViewport before = viewport;
    viewport.fit(m_document->size());
    if (!(viewport == before))
        notify();
}

void EditorSession::zoom(double value, std::optional<QPointF> anchor)
{
    if (!m_document)
        return;
    // The viewport refuses a zoom that is no number.
    const CanvasViewport before = viewport;
    viewport.setZoom(value, anchor.value_or(viewport.center()), m_document->size());
    if (!(viewport == before))
        notify();
}

void EditorSession::setShowsPixelGrid(bool shows)
{
    m_showsPixelGrid = shows;
    notify();
}

void EditorSession::setCropRect(std::optional<QRectF> rect)
{
    if (std::exchange(m_cropRect, rect) != rect)
        notify();
}

void EditorSession::setCropRatioChoice(const QString &choice)
{
    if (std::exchange(m_cropRatioChoice, choice) != choice)
        notify();
}

void EditorSession::setCropError(std::optional<QString> error)
{
    if (std::exchange(m_cropError, error) != error)
        notify();
}

void EditorSession::setShowsSampleRing(bool shows)
{
    m_showsSampleRing = shows;
    notify();
}

void EditorSession::requestCanvasFocus()
{
    ++m_canvasFocusRequest;
    notify();
}
