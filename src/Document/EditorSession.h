#pragma once
#include "Document/BrushStroke.h"
#include "Document/CloneStamp.h"
#include "Document/ColorPalette.h"
#include "Document/Gradient.h"
#include "Document/Distort.h"
#include "Document/Filters.h"
#include "Document/LevelsAutomatic.h"
#include "Document/FloatingSelection.h"
#include "Document/MagicWand.h"
#include "Document/ObjectSelection.h"
#include "Document/SelectionClipboard.h"
#include "Document/SelectionEdits.h"
#include "Document/SmudgeLiquify.h"
#include "Document/ToolDefaults.h"
#include "Document/DocumentHistory.h"
#include "Document/EditorSession+Jobs.h"
#include "Document/EditorSession+Model.h"
#include "IO/ProjectStore.h"
#include "IO/PSD/PSDDocumentBuilder.h"
#include "IO/RawImporter.h"
#include "UI/PSDConversionSheet.h"
#include "Rendering/CanvasViewport.h"
#include "Rendering/EffectsPreviewCache.h"
#include <QFutureWatcher>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <deque>
#include <map>
#include <functional>
#include <set>

class QPainter;

// The one editor state; `changed` stands in for Swift's observation.
class EditorSession : public QObject, private SessionJobs {
    Q_OBJECT
public:
    explicit EditorSession(QObject *parent = nullptr);

    // The canvas edits this in place, then calls notify().
    CanvasViewport viewport;
    DocumentHistory history;
    // Swift's @ObservationIgnored tuple: the canvas draws it unannounced.
    SnapGuides snapGuides;
    // The canvas's effects previews, rendered off the UI thread.
    EffectsPreviewCache effectsPreviews;

    const std::optional<CanvasDocument> &document() const { return m_document; }
    NavigationTool tool() const { return m_tool; }
    const QSet<QUuid> &collapsedGroupIDs() const { return m_collapsedGroupIDs; }
    const QSet<QUuid> &selectedLayerIDs() const { return m_selectedLayerIDs; }
    std::optional<QUuid> activeLayerID() const { return m_activeLayerID; }
    void setActiveLayerID(std::optional<QUuid> id);
    std::optional<ImageLayer> activeLayer() const;

    // Blocks overlapping edits at once.
    bool isProjectBusy() const { return m_isProjectBusy; }
    void setIsProjectBusy(bool busy);
    // Busy this long, in milliseconds, before the controls dim.
    static constexpr int busyIndicatorDelay = 250;
    bool showsBusy() const { return m_showsBusy; }
    bool showsNewDocument() const { return m_showsNewDocument; }
    void setShowsNewDocument(bool shows);
    bool showsImporter() const { return m_showsImporter; }
    void setShowsImporter(bool shows);
    bool isImporting() const { return m_isImporting; }
    void setIsImporting(bool importing);
    std::optional<QString> importError() const { return m_importError; }
    void setImportError(std::optional<QString> error);
    bool showsConversionSheet() const { return m_showsConversionSheet; }
    const std::optional<PSDConversionRequest> &conversionRequest() const { return m_conversionRequest; }
    // The RAW file the develop sheet shows, while it shows.
    const std::optional<RawDevelopRequest> &rawDevelop() const { return m_rawDevelop; }
    void finishRawDevelop(std::optional<RawDevelopSettings> settings);
    std::optional<QUuid> renamingLayerID() const { return m_renamingLayerID; }
    void setRenamingLayerID(std::optional<QUuid> id);

    // The first tab's first canvas sheet skips the clipboard.
    bool skipsInitialClipboardCanvasSize = false;
    // Where the project lives on disk, once opened or saved.
    const std::optional<QString> &projectPath() const { return m_projectPath; }
    void setProjectPath(std::optional<QString> path);

    bool canStartProjectOperation() const;
    // `ready` runs at once, or when the session allows.
    void waitForFileRequest(std::function<void()> ready);
    void waitForProjectAccess(std::function<void()> ready);

    void selectLayer(std::optional<QUuid> id);
    void selectTool(NavigationTool value);
    // Tab: the current tool's next mode.
    void cycleToolMode();

    // Whether the mask thumbnail, not the layer's, is the target.
    bool isMaskSelected() const { return m_isMaskSelected; }

    // A blend mode tried on by hovering; nothing is saved.
    struct BlendPreview {
        QUuid layerID;
        LayerBlendMode mode;
        friend bool operator==(const BlendPreview &, const BlendPreview &) = default;
    };
    const std::optional<BlendPreview> &blendPreview() const { return m_blendPreview; }

    // A pending transform; layers change when it is committed.
    const std::optional<TransformEdit> &transformEdit() const { return m_transformEdit; }
    bool canTransform() const;
    // Several layers, or a folder's contents, move in one box.
    bool transformsAsGroup() const;
    std::vector<ImageLayer> groupTransformMembers() const;
    std::optional<LayerTransform> groupTransformBox() const;
    void beginTransform(bool persistent = true);
    // Alt-drag: the selection's copies move, one step.
    void beginDuplicateTransform();
    // The Move tool's settings (TransformInspector, Ctrl+H).
    bool transformAutoSelect() const { return m_transformAutoSelect; }
    void setTransformAutoSelect(bool picks);
    bool showsTransformControls() const { return m_showsTransformControls; }
    void setShowsTransformControls(bool shows);
    bool locksTransformRatio() const { return m_locksTransformRatio; }
    void setLocksTransformRatio(bool locks);
    void previewTransform(const LayerTransform &value);
    void commitTransform();
    void cancelTransform();
    std::optional<QSizeF> transformPixelSize() const;
    LayerTransform displayedTransform(const ImageLayer &layer) const;
    LayerTransform editedTransform(const ImageLayer &layer) const;
    std::optional<LayerTransform> pendingTransform(const ImageLayer &layer) const;
    // An unlinked mask, selected, transforms without its layer.
    bool transformTargetsMask() const;
    void nudgeLayer(double dx, double dy);

    const std::optional<LassoDraft> &lassoDraft() const { return m_lassoDraft; }
    LassoKind lassoKind() const { return m_lassoKind; }
    void setLassoKind(LassoKind kind);
    LassoKind marqueeKind() const { return m_marqueeKind; }
    void setMarqueeKind(LassoKind kind);
    // The options bar's mode; Shift adds, Alt subtracts.
    SelectionMode selectionModeChoice() const { return m_selectionModeChoice; }
    void setSelectionModeChoice(SelectionMode mode);
    std::optional<SelectionMode> heldSelectionMode() const { return m_heldSelectionMode; }
    bool selectionAntialiased() const { return m_selectionAntialiased; }
    void setSelectionAntialiased(bool antialiased);
    // Pixels Expand and Contract grow or shrink by.
    int selectionExpandAmount() const { return m_selectionExpandAmount; }
    void setSelectionExpandAmount(int amount);
    int selectionContractAmount() const { return m_selectionContractAmount; }
    void setSelectionContractAmount(int amount);
    int selectionFeatherAmount() const { return m_selectionFeatherAmount; }
    void setSelectionFeatherAmount(int amount);
    // The amount sheet's question, open while set.
    std::optional<SelectionAmountOperation> selectionAmountOperation() const { return m_selectionAmountOperation; }
    void setSelectionAmountOperation(std::optional<SelectionAmountOperation> operation);
    const std::optional<DocumentSelection> &selectionMoveOrigin() const { return m_selectionMoveOrigin; }
    const WandSettings &wandSettings() const { return m_wandSettings; }
    void setWandSettings(const WandSettings &settings);
    const BrushStroke *brushStroke() const { return m_brushStroke.get(); }
    const BrushSettings &brushSettings() const { return m_brushSettings; }
    void setBrushSettings(const BrushSettings &settings);
    BrushToolMode brushMode() const { return m_brushMode; }
    void setBrushMode(BrushToolMode mode);
    BlurToolMode blurMode() const { return m_blurMode; }
    void setBlurMode(BlurToolMode mode);
    // Smudge or Liquify under way; the canvas shows its copy.
    const WarpStroke *warpStroke() const { return m_warpStroke.get(); }
    SpotHealingMode spotHealingMode() const { return m_spotHealingMode; }
    void setSpotHealingMode(SpotHealingMode mode);
    bool maskPaintWhite() const { return m_maskPaintWhite; }
    void setMaskPaintWhite(bool white);
    const PaletteColor &backgroundColor() const { return m_backgroundColor; }
    void setBackgroundColor(const PaletteColor &color);
    // The open colour picker; the session notifies its changes.
    const std::optional<ColorPickerState> &colorPicker() const { return m_colorPicker; }
    const GradientSettings &gradientSettings() const { return m_gradientSettings; }
    void setGradientSettings(const GradientSettings &settings);
    // A gradient drawn and not yet applied.
    const std::optional<GradientEdit> &gradientEdit() const { return m_gradientEdit; }
    // Clone Stamp's Alt-clicked source, in document pixels, and options.
    const std::optional<QPointF> &cloneSource() const { return m_cloneSource; }
    const CloneSettings &cloneSettings() const { return m_cloneSettings; }
    void setCloneSettings(const CloneSettings &settings);
    // Counts every change to a stroke, so the canvas redraws.
    int brushRevision() const { return m_brushRevision; }
    // Selected pixels on the move, from a Ctrl-drag or Ctrl-arrow.
    const PixelMove *pixelMove() const { return m_pixelMove.get(); }

    // The one filter open; its sheet shows while it lasts.
    const std::optional<FilterEdit> &filterEdit() const { return m_filterEdit; }
    // The last committed filter's settings, where the next one starts.
    const FilterSettings &filterSettings() const { return m_filterSettings; }
    const std::optional<LevelsEdit> &levels() const { return m_levels; }
    const std::optional<HueSaturationEdit> &hueSaturation() const { return m_hueSaturation; }
    // The newest preview, waiting for the running one.
    const std::optional<HueSaturationJob> &hueSaturationPending() const { return m_hueSaturationPending; }
    std::optional<HueSampleMode> hueSampleMode() const { return m_hueSampleMode; }
    bool hueTargeting() const { return m_hueTargeting; }
    // The adjustment layer the colour editors write to.
    std::optional<QUuid> adjustmentEditingID() const { return m_adjustmentEditingID; }
    void setAdjustmentEditingID(std::optional<QUuid> id);
    // Its settings as its editor opened, for Cancel.
    const std::optional<LayerAdjustment> &adjustmentOriginal() const { return m_adjustmentOriginal; }
    // The effect whose panel is open, bound to its layer.
    const std::optional<LayerEffectSelection> &effectsEditing() const { return m_effectsEditing; }
    void setEffectsEditing(std::optional<LayerEffectSelection> selection);
    // Its layer's effects as the panel opened, for Cancel.
    const std::optional<LayerEffects> &effectsEditingOriginal() const { return m_effectsEditingOriginal; }
    void setEffectsEditingOriginal(std::optional<LayerEffects> original);
    // The effect chosen in the list; see selectedEffect.
    const std::optional<LayerEffectSelection> &effectSelection() const { return m_effectSelection; }

    bool isModified() const { return history.isModified(); }
    bool canUseHistory() const;
    // Undo's first press discards a pending gradient.
    bool canUndo() const { return canUseHistory() && (history.canUndo() || m_gradientEdit); }
    bool canRedo() const { return canUseHistory() && history.canRedo(); }
    void undo();
    void redo();
    // A transaction that nests; a whole gesture is one entry.
    void beginEdit(const QString &name);
    void endEdit();

    bool canEditLayers() const;
    void addBlankLayer();
    void deleteLayer(QUuid id);
    void deleteActiveLayer();
    // Every selected layer, folders with their contents: one step.
    void deleteSelectedLayers();
    void renameLayer(QUuid id, const QString &name);
    void toggleLayerVisibility(QUuid id);
    // Pressing an eye, then dragging over others: one undo step.
    std::optional<bool> beginVisibilitySwipe(QUuid id);
    void setVisibilityInSwipe(QUuid id, bool visible);
    void endVisibilitySwipe();
    // The list runs top down; the document stores bottom up.
    void reorderLayers(const std::set<int> &offsets, int destination);
    bool canMoveActiveLayer(int offset) const;
    void moveActiveLayer(int offset);

    // Files decode off the UI thread; `done` follows this request.
    void importImages(const QList<QUrl> &urls, std::optional<QPointF> point = std::nullopt, std::function<void()> done = {});
    void insert(const ImportedImage &asset, std::optional<QPointF> centeredAt = std::nullopt);
    // The sheet shows while a Photoshop file is read.
    void beginPSDReading(const QString &title, const QString &confirmTitle);
    void finishPSDReading(const std::vector<PSDConversion> &conversions, std::function<void(bool)> answer);
    void endPSDReading();
    void finishConversion(bool confirmed);
    void decodeRaw(const QString &path, qint64 remaining);
    void developRaw(const QString &path, std::function<void(std::optional<RawDevelopSettings>)> answer);
    void insertPhotoshop(const PSDImport &imported, const QString &named, std::optional<QPointF> centeredAt = std::nullopt);
    void createDocument(int width, int height, bool emptyLayer = false);
    void fit();
    void zoom(double value, std::optional<QPointF> anchor = std::nullopt);
    // The View menu's grid, shown from 800%.
    bool showsPixelGrid() const { return m_showsPixelGrid; }
    void setShowsPixelGrid(bool shows);
    // View > Snap: moves and crops line up with edges.
    bool snappingEnabled() const { return m_snappingEnabled; }
    void setSnappingEnabled(bool enabled);
    // The Eyedropper's ring: the sampled colour over the original.
    bool showsSampleRing() const { return m_showsSampleRing; }
    void setShowsSampleRing(bool shows);
    // The Crop tool's frame in document pixels, once drawn.
    const std::optional<QRectF> &cropRect() const { return m_cropRect; }
    void setCropRect(std::optional<QRectF> rect);
    // Swift's ratio names: Free, Original, 1:1, 4:3 and 16:9.
    const QString &cropRatioChoice() const { return m_cropRatioChoice; }
    void setCropRatioChoice(const QString &choice);
    const std::optional<QString> &cropError() const { return m_cropError; }
    void setCropError(std::optional<QString> error);
    // Counted requests; the canvas takes focus on each new one.
    int canvasFocusRequest() const { return m_canvasFocusRequest; }
    void requestCanvasFocus();

    // What Paste puts back in place, until another copy.
    const std::optional<PixelClipboard> &pixelClipboard() const { return m_pixelClipboard; }
    // What went wrong in a tool; the window shows it.
    const std::optional<QString> &brushError() const { return m_brushError; }
    void setBrushError(std::optional<QString> error);

    void notify() { emit changed(); }

    // Swift's extensions, each declared in its twin's header.
#include "Document/EditorSession+Projects.h"
#include "IO/ImageResizer+Session.h"
#include "Document/LayerMask+Session.h"
#include "Document/LayerAppearance+Session.h"
#include "Document/Crop+Session.h"
#include "Document/Distort+Session.h"
#include "Document/LayerMerge+Session.h"
#include "Document/LayerFlip+Session.h"
#include "Document/Selection+Session.h"
#include "Document/MagicWand+Session.h"
#include "Document/SubjectRemoval+Session.h"
#include "Document/EditorSession+Brush.h"
#include "Document/CloneStamp+Session.h"
#include "Document/ColorPalette+Session.h"
#include "Document/Gradient+Session.h"
#include "Document/ShapeTool+Session.h"
#include "Document/TypeTool+Session.h"
#include "Document/SmudgeLiquify+Session.h"
#include "Document/BlurTool+Session.h"
#include "Document/SelectionEdits+Session.h"
#include "Document/SelectionClipboard+Session.h"
#include "Document/FloatingSelection+Session.h"
#include "Document/Filters+Session.h"
#include "Document/Levels+Session.h"
#include "Document/LevelsAutomatic+Session.h"
#include "Document/HueSaturation+Session.h"
#include "Document/MaskTracing+Session.h"
#include "Document/LayerGroups+Session.h"
#include "Document/LiveLayerMask+Session.h"
#include "Document/LayerAdjustment+Session.h"
#include "Document/AdjustmentEditing+Session.h"
#include "Document/LayerEffects+Session.h"
#include "Document/Guides+Session.h"
#include "Document/ObjectSelection+Session.h"
signals:
    void changed();

private:
    // Copies layers into this document in place, as Swift does.
    friend class ProjectWorkspace;
    void restore(const DocumentHistory::Snapshot &snapshot);
    void resumeFileRequests();
    void updateBusyIndicator();
    void drainImports();
    void decodeNext();
    void finishDecode();
    void finishPhotoshopRead();
    void endDuplicateTransform();
    // Clone Stamp is family 1, the Smear 2, others 0.
    static int tipFamily(NavigationTool tool);

    std::optional<CanvasDocument> m_document;
    NavigationTool m_tool = NavigationTool::move;
    QSet<QUuid> m_collapsedGroupIDs;
    QSet<QUuid> m_selectedLayerIDs;
    // The canvas reads previews from a const session: mutable caches.
    mutable std::map<QUuid, DistortPreviewCache> m_distortPreviewCache;
    mutable std::map<QUuid, DistortEffectsCache> m_distortEffectsCache;
    mutable std::optional<MaskDistortPreviewCache> m_maskDistortPreviewCache;

    std::optional<TransformEdit> m_transformEdit;
    std::optional<TransformDuplicate> m_transformDuplicate;
    bool m_transformAutoSelect = ToolDefaults::boolean(QStringLiteral("autoSelect"), false);
    bool m_showsTransformControls = ToolDefaults::boolean(QStringLiteral("transformControls"), true);
    bool m_locksTransformRatio = true;
    std::optional<BlendPreview> m_blendPreview;
    std::optional<QUuid> m_opacityEditLayerID;
    bool m_isMaskSelected = false;
    std::optional<QUuid> m_activeLayerID;
    bool m_isProjectBusy = false;
    bool m_showsNewDocument = false;
    bool m_showsImporter = false;
    bool m_isImporting = false;
    std::optional<QString> m_importError;
    std::optional<QUuid> m_renamingLayerID;
    std::optional<QString> m_projectPath;
    bool m_showsPixelGrid = ToolDefaults::boolean(QStringLiteral("pixelGrid"), true);
    bool m_snappingEnabled = true;
    // The layout grid starts off; guides show; rulers hide.
    bool m_showsGrid = ToolDefaults::boolean(QStringLiteral("grid"), false);
    bool m_showsGuides = ToolDefaults::boolean(QStringLiteral("guides"), true);
    bool m_showsRulers = ToolDefaults::boolean(QStringLiteral("rulers"), false);
    bool m_snapEnabled = ToolDefaults::boolean(QStringLiteral("snap"), true);
    bool m_snapToGuides = ToolDefaults::boolean(QStringLiteral("snapGuides"), true);
    bool m_snapToGrid = ToolDefaults::boolean(QStringLiteral("snapGrid"), false);
    bool m_snapToLayers = ToolDefaults::boolean(QStringLiteral("snapLayers"), true);
    bool m_snapToDocumentBounds = ToolDefaults::boolean(QStringLiteral("snapBounds"), true);
    bool m_locksGuides = ToolDefaults::boolean(QStringLiteral("lockGuides"), false);
    std::optional<GuideDrag> m_guideDrag;
    bool m_showsSampleRing = true;
    std::optional<QRectF> m_cropRect;
    QString m_cropRatioChoice = QStringLiteral("Free");
    std::optional<QString> m_cropError;
    QFutureWatcher<Cropped> m_cropCommit;
    std::function<void()> m_committingCrop;
    int m_canvasFocusRequest = 0;
    bool m_showsBusy = false;
    QTimer m_busyTimer;
    std::vector<std::function<void()>> m_projectWaiters;
    std::vector<std::function<void()>> m_fileRequestWaiters;
    std::deque<ImportRequest> m_pendingImports;
    // Where the request now decoding stands.
    qsizetype m_importIndex = 0;
    std::optional<QPointF> m_importPoint;
    QStringList m_importFailures;
    QFutureWatcher<Decoded> m_decoder;
    QFutureWatcher<PhotoshopRead> m_photoshopReader;
    bool m_showsConversionSheet = false;
    std::optional<PSDConversionRequest> m_conversionRequest;
    std::function<void(bool)> m_conversionAnswer;
    std::optional<RawDevelopRequest> m_rawDevelop;
    std::function<void(std::optional<RawDevelopSettings>)> m_rawAnswer;
    // Cancel pressed while the file was still read.
    bool m_conversionCancelled = false;
    std::optional<QString> m_brushError;
    // The layers a bake under way will delete.
    std::vector<QUuid> m_bakingIDs;
    QFutureWatcher<Baked> m_baker;
    std::optional<Inverting> m_inverting;
    QFutureWatcher<Inverted> m_inverter;
    WandSettings m_wandSettings;
    WandMode m_wandMode = WandMode::wand;
    ObjectSelectionSettings m_objectSelectionSettings;
    std::optional<Wanding> m_wanding;
    QFutureWatcher<Wanded> m_wand;
    std::optional<Subjecting> m_subjecting;
    QFutureWatcher<Subjected> m_subject;
    std::unique_ptr<BrushStroke> m_brushStroke;
    std::unique_ptr<PixelMove> m_pixelMove;
    BrushSettings m_brushSettings;
    BrushToolMode m_brushMode = BrushToolMode::paint;
    BlurToolMode m_blurMode = BlurToolMode::liquify;
    std::unique_ptr<WarpStroke> m_warpStroke;
    SpotHealingMode m_spotHealingMode = SpotHealingMode::contentAware;
    bool m_maskPaintWhite = false;
    PaletteColor m_backgroundColor = PaletteColor::white();
    std::optional<ColorPickerState> m_colorPicker;
    GradientSettings m_gradientSettings;
    std::optional<GradientEdit> m_gradientEdit;
    ShapeKind m_shapeKind = ShapeKind::rectangle;
    double m_shapeCornerRadius = 0;
    double m_shapeLineWidth = 4;
    std::optional<ShapeDraft> m_shapeDraft;
    mutable std::map<QUuid, ShapePreview> m_shapeTransformPreviewCache;
    std::optional<TextDraft> m_textDraft;
    LayerTextStyle m_textDefaults;
    std::optional<QPointF> m_cloneSource;
    CloneSettings m_cloneSettings;
    // The offset aligned strokes keep, fixed as a stroke begins.
    std::optional<QSizeF> m_cloneOffset;
    // Clone Stamp and the Smear keep their own soft tips.
    std::map<int, ParkedTip> m_parkedBrushTips{{1, {40, 0, 1}}, {2, {40, 0, 1}}};
    std::optional<LastBrushPoint> m_lastBrushPoint;
    // Smoothing's brush, and the pointer it trails.
    std::optional<QPointF> m_brushAnchor;
    std::optional<QPointF> m_brushPointer;
    int m_brushRevision = 0;
    std::optional<PendingOpacityDigit> m_pendingOpacityDigit;
    std::optional<CommittingRaster> m_committingRaster;
    QFutureWatcher<RasterMade> m_rasterCommit;
    std::optional<FilterEdit> m_filterEdit;
    FilterSettings m_filterSettings;
    QFutureWatcher<Filtered> m_filterPreview;
    // Out until its finish runs: setFuture drops a pending signal.
    bool m_filterRendering = false;
    // Which edit's preview is being made; a cancelled edit's drops.
    QUuid m_filterPreviewFor;
    std::vector<Committing> m_filterCommitWaiting;
    std::optional<Committing> m_committingFilter;
    QFutureWatcher<FilterMade> m_filterCommit;
    std::optional<LevelsEdit> m_levels;
    // Which edit a running preview serves; others drop.
    QUuid m_levelsPreviewFor;
    QFutureWatcher<std::optional<QImage>> m_levelsPreview;
    bool m_levelsRendering = false;
    QFutureWatcher<std::optional<LevelsHistogram>> m_levelsHistogram;
    std::optional<Committing> m_committingLevels;
    QFutureWatcher<Leveled> m_levelsCommit;
    std::optional<HueSaturationEdit> m_hueSaturation;
    std::optional<HueSaturationJob> m_hueSaturationPending;
    // Which edit a running preview serves; others drop.
    QUuid m_hueSaturationPreviewFor;
    QFutureWatcher<Adjusted> m_hueSaturationTask;
    bool m_hueSaturationRendering = false;
    std::optional<CommittingHue> m_committingHueSaturation;
    QFutureWatcher<Adjusted> m_hueSaturationCommit;
    std::optional<HueSampleMode> m_hueSampleMode;
    bool m_hueTargeting = false;
    std::optional<HueTargetDrag> m_hueTargetDrag;
    std::optional<QUuid> m_adjustmentEditingID;
    std::optional<LayerAdjustment> m_adjustmentOriginal;
    std::optional<LayerEffectSelection> m_effectsEditing;
    std::optional<LayerEffects> m_effectsEditingOriginal;
    std::optional<LayerEffectSelection> m_effectSelection;
    std::optional<PixelClipboard> m_pixelClipboard;
    // Swift's pasteboard change count, counted from the first copy.
    int m_clipboardChanges = 0;
    bool m_watchingClipboard = false;
    std::optional<LassoDraft> m_lassoDraft;
    LassoKind m_lassoKind = LassoKind::freehand;
    LassoKind m_marqueeKind = LassoKind::rectangle;
    SelectionMode m_selectionModeChoice = SelectionMode::replace;
    std::optional<SelectionMode> m_heldSelectionMode;
    // The selection as a drag began; one step per drag.
    std::optional<DocumentSelection> m_selectionMoveOrigin;
    bool m_selectionAntialiased = true;
    int m_selectionExpandAmount = 1;
    int m_selectionContractAmount = 1;
    int m_selectionFeatherAmount = 2;
    std::optional<SelectionAmountOperation> m_selectionAmountOperation;
};
