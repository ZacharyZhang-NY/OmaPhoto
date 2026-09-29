#pragma once
#include "Document/Crop.h"
#include "Document/EditorSession.h"
#include "Document/LayerMask.h"
#include "Rendering/BrushCursorOverlay.h"
#include "Rendering/InlineTextEditor.h"
#include "Rendering/LayerEffectsSurface.h"
#include "Rendering/SelectionIcons.h"
#include "Rendering/LayerRenderer.h"
#include "Rendering/SampleRingOverlay.h"
#include "Rendering/TransformOverlay.h"
#include <QElapsedTimer>
#include <QPainterPath>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <optional>
#include <vector>

// The document on screen: layers over a checkerboard.
class CanvasView : public QWidget {
    Q_OBJECT
public:
    explicit CanvasView(EditorSession &session, QWidget *parent = nullptr);

    // From 200% document pixels show hard-edged, as Photoshop does.
    static constexpr double crispZoom = 2;
    // The pixel grid appears from 800%.
    static constexpr double pixelGridZoom = 8;
    // Crop edges snap within this many view points.
    static constexpr double cropSnapDistance = 8;

    // Repaints what the session changed; true when something did.
    bool synchronizeDisplay();
    // Each request the session counts up takes focus once.
    void consumeFocusRequest(int request);
    // Photoshop's duplicate pointer: a black arrow over a white one.
    static QCursor duplicateCursor(double ratio);
    // The Move pointer and the rotation grip's, drawn as paths.
    static QCursor moveCursor(double ratio);
    static QCursor rotationCursor(double ratio);
    // A white arrow: the handle under it distorts.
    static QCursor distortCursor(double ratio);
    // A badged pointing hand: Ctrl-click loads a selection.
    static QCursor loadSelectionCursor(double ratio);
    // An arrow with a dashed box: it drags the outline.
    static QCursor moveSelectionCursor(double ratio);
    // The arrow with scissors: a Ctrl-drag cuts and moves pixels.
    static QCursor movePixelsCursor(double ratio);
    // A crosshair with the tool's icon, badged + or −.
    static QCursor selectionCursor(SelectionIcon icon, SelectionMode mode, double ratio);
    // The wand, its sparkle at the hot spot, badged alike.
    static QCursor wandCursor(SelectionMode mode, double ratio);
    // Swift's eyedropper: haloed white, its tip the hot spot.
    static QCursor eyedropperCursor(double ratio);
    // The brush circle as last drawn, for tests.
    const BrushCursorOverlay &brushCursor() const { return m_brushCursor; }
    const SampleRingOverlay &sampleRing() const { return m_sampleRing; }
    // The text typed on the canvas, while one is open.
    InlineTextEditor *inlineTextEditor() const { return m_inlineTextEditor.get(); }
    // Released on the rulers, just outside the canvas.
    bool isOverRuler(QPointF point) const;
    std::optional<double> documentPosition(CanvasGuide::Axis axis, QPointF point) const;

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

private:
    struct DisplayState {
        struct Layer {
            QUuid id;
            LayerTransform transform;
            std::optional<ImageIdentity> imageID;
            std::optional<ImageIdentity> maskID;
            std::optional<QUuid> maskSourceID;
            // Entering or leaving a masked folder changes the clip.
            std::optional<QUuid> parentID;
            bool visible;
            double opacity;
            LayerBlendMode blendMode;
            // Where the mask shows when placed apart from the layer.
            std::optional<LayerTransform> maskPlacement;
            std::optional<LayerAdjustment> adjustment;
            std::optional<LayerEffects> effects;
            friend bool operator==(const Layer &, const Layer &) = default;
        };
        // Folders have no pixels, so their masks are tracked apart.
        struct FolderMask {
            QUuid id;
            std::optional<ImageIdentity> maskID;
            LayerTransform transform;
            friend bool operator==(const FolderMask &, const FolderMask &) = default;
        };
        // Every change to a stroke, so a press repaints.
        int brushRevision;
        bool pixelGrid;
        // A distortion's corners: the warp follows them.
        std::optional<Corners> corners;
        std::optional<QUuid> documentID;
        std::optional<QSizeF> size;
        std::optional<QRectF> renderBounds;
        CanvasViewport viewport;
        std::vector<Layer> layers;
        std::vector<FolderMask> folderMasks;
        friend bool operator==(const DisplayState &, const DisplayState &) = default;
    };
    // A Zoom press: drags zoom about it, clicks step.
    struct ZoomDrag {
        QPointF start;
        double zoom;
        bool moved;
    };
    using Center = std::function<QPointF(QPointF)>;
    // What a press off the handles drags, picked or not.
    struct PressTarget {
        QUuid id;
        bool picked;
    };

    DisplayState displayState() const;
    std::optional<QRectF> strokeDirtyRect() const;
    FolderMaskClip::Applier liveFolderMaskClip(const BrushStroke &edit, double scale, const Center &center) const;
    std::optional<QRectF> renderBounds() const;
    void syncGeometry();
    void draw(QPainter &context, const QRectF &dirty);
    void drawStroke(const BrushStroke &stroke, const ImageLayer &layer, const LayerTransform &transform, const std::optional<QImage> &mask,
                    const LayerRenderer::Options &options, double scale, const Center &center, QPainter &target);
    void drawLayers(const CanvasDocument &document, double scale, const Center &center, QPainter &context);
    // Swift's effects surface on the canvas (EditorCanvas+Effects.cpp).
    LayerEffectsSurface *strokeSurface(const ImageLayer &layer, const BrushStroke &stroke, const std::optional<QImage> &mask);
    // The stroke is over: its surface stands in meanwhile.
    void handOnStrokeSurface();
    // The dragged shape, in the colour it will have.
    void drawShapeDraft(double scale, const Center &center, QPainter &target, const QImage &clip) const;
    // What the draft draws with, so a change repaints it.
    struct ShapeShown {
        ShapeDraft draft;
        PaletteColor color;
        double lineWidth;
        // The layer the draft sits above.
        std::optional<QUuid> target;
        friend bool operator==(const ShapeShown &, const ShapeShown &) = default;
    };
    std::optional<ShapeShown> shownShape() const;
    // Swift's synchronizeInlineText: the editor follows the draft.
    void synchronizeInlineText();
    // The Type tool's press: the editor's, else Swift's gesture.
    void pressTextTool(QPointF point, Qt::KeyboardModifiers modifiers, int clicks);
    void beginTextGesture(QPointF point, Qt::KeyboardModifiers modifiers, int clicks);
    void dragTextGesture(QPointF point);
    void finishTextGesture();
    void endTextGesture();
    QRectF textBoxDraftRect() const;
    void drawTextBoxDraft(QPainter &painter) const;
    void drawDocumentPixels(const QRectF &view, const QRectF &pixels, const CanvasDocument &document, QPainter &context);
    void drawPixelGrid(const QRectF &view, const CanvasDocument &document, QPainter &context) const;
    void updateCursor();
    static QPainterPath arrowPath();
    Qt::KeyboardModifiers heldModifiers() const;
    void readModifiers(Qt::KeyboardModifiers modifiers);
    void modifiersChanged();
    // Swift's zoom keys on key-down; true when it zoomed.
    bool handleKeyboardZoom(const QKeyEvent &key);
    std::optional<PressTarget> transformPressLayer(QPointF pixel, Qt::KeyboardModifiers modifiers) const;
    bool pressMovesLayer(QPointF point, Qt::KeyboardModifiers modifiers) const;
    QCursor transformCursor(QPointF point, Qt::KeyboardModifiers modifiers) const;
    void beginTransformDrag(QPointF point, Qt::KeyboardModifiers modifiers);
    void dragTransform(QPointF point, Qt::KeyboardModifiers modifiers);
    void endTransformDrag();
    void cancelTransformDrag();
    // Swift's lasso gestures (EditorCanvas+Selection.cpp).
    QCursor lassoCursor(Qt::KeyboardModifiers modifiers, std::optional<QPointF> location) const;
    static QCursor pixelDragCursor(bool duplicate, double ratio);
    void lassoMouseDown(QPointF point, Qt::KeyboardModifiers modifiers);
    bool moveSelectionTool(QPointF point, Qt::KeyboardModifiers modifiers);
    void releaseSelectionTool();
    bool selectionKey(const QKeyEvent &key);
    // The keys as the canvas reads them, after remapping.
    void pressKey(QKeyEvent *event, int physical);
    void endSelectionGestures();
    void dragSelection(QPointF point, Qt::KeyboardModifiers modifiers);
    void dragMarqueeDraft(QPointF pixel, Qt::KeyboardModifiers modifiers);
    QSizeF marqueeAutoscrollDelta(QPointF point) const;
    void updateMarqueeAutoscroll(QPointF point);
    void stepMarqueeAutoscroll();
    void stopMarqueeAutoscroll();
    void updateAntsTimer();
    void stepAnts();
    // Swift's brush gestures (EditorCanvas+Brush.cpp).
    void brushMouseDown(QPointF point, Qt::KeyboardModifiers modifiers);
    bool brushMouseMove(QPointF point, Qt::KeyboardModifiers modifiers, bool dragging);
    void brushMouseUp(QPointF point);
    void beginBrushTipDrag(QPointF point, Qt::KeyboardModifiers modifiers);
    void dragBrushTip(QPointF point, Qt::KeyboardModifiers modifiers);
    void endBrushTipDrag(QPointF point);
    void updateBrushCursor();
    void endMiddlePan();
    // Clone Stamp's preview: one click's coverage, and the source there.
    QImage cloneTip(double diameter, double hardness);
    QImage clonePreview(QPointF center, double diameter, const CanvasDocument &document);
    bool brushKey(const QKeyEvent &key);
    // Swift's gradient gestures (EditorCanvas+Gradient.cpp).
    void beginGradientDrag(QPointF point);
    bool dragGradient(QPointF point, Qt::KeyboardModifiers modifiers);
    // Shift keeps a line to 45° steps, as Photoshop.
    static QPointF snapped(QPointF point, QPointF anchor);
    bool brushBracket(const QString &text);
    // Swift's colour sampling (EditorCanvas+Picking.cpp).
    bool palettePicking() const;
    bool picking() const;
    void syncPicking();
    void beginSampling(QPointF point);
    void sampleColor(QPointF point);
    void endSampling();
    bool pickingMove(QPointF point, Qt::MouseButtons buttons);
    void pickingRelease();
    // A sampling or targeting press, with Space up.
    bool pickingPress(QPointF point);
    // Swift's crop gestures (EditorCanvas+Crop.cpp).
    std::optional<int> cropRegion(QPointF point) const;
    QCursor cropCursor(QPointF point) const;
    void beginCropDrag(QPointF point);
    bool cropMove(QPointF point, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers);
    void dragCrop(QPointF point, Qt::KeyboardModifiers modifiers);
    void endCropDrag();
    bool cropKey(const QKeyEvent &key);
    // The Move tool's guide drags (EditorCanvas+Guides.cpp).
    bool beginGuideDrag(QPointF point);
    // The Move tool's double click: the topmost live text opens.
    bool beginLiveTextEdit(QPointF point);
    void press(QMouseEvent *event, int clicks);
    bool guideMove(QPointF point, Qt::MouseButtons buttons);
    void endGuideDrag(QPointF point);
    // Camera Raw's tools on the canvas (EditorCanvas+CameraRaw.cpp).
    bool cameraRawPress(QPointF point);
    bool cameraRawMove(QPointF point, Qt::MouseButtons buttons);
    void cameraRawRelease();
    void clearCameraRawReadout();
    // Swift's targeted drag: right raises; Ctrl drags the hue.
    bool targetingMove(QPointF point, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers);
    void targetingRelease();

    EditorSession &m_session;
    TransformOverlay m_overlay;
    std::optional<TransformOverlayGeometry> m_displayedGeometry;
    SnapGuides m_displayedGuides;
    // Where the overlay last drew, repainted with the next drawing.
    QRect m_overlayRect;
    std::optional<TransformDrag> m_transformDrag;
    // The drag's cursor, kept until the release.
    std::optional<QCursor> m_dragCursor;
    bool m_duplicatesTransformOnDrag = false;
    // The last pointer position over the canvas.
    std::optional<QPointF> m_hover;
    bool m_controlHeld = false;
    // Busy or importing shows the arrow; tracked for refreshes.
    bool m_displayedBlocked = false;
    std::optional<DisplayState> m_displayedState;
    std::optional<NavigationTool> m_displayedTool;
    int m_lastFocusRequest = 0;
    bool m_spaceHeld = false;
    // The key that began the pan, when Space is remapped.
    std::optional<int> m_panKey;
    bool m_optionHeld = false;
    std::optional<QPointF> m_lastDragPoint;
    // Where a middle-button pan last was, its own drag point.
    std::optional<QPointF> m_middlePanPoint;
    std::optional<ZoomDrag> m_zoomDrag;
    bool m_shiftHeld = false;
    // Document point where a drag of the outline began.
    std::optional<QPointF> m_selectionDragStart;
    // Where a Ctrl-drag of selected pixels began, in document pixels.
    std::optional<QPointF> m_pixelDragStart;
    // Shift at the press chose Add; pressed afresh it squares.
    bool m_marqueeConstrainArmed = true;
    std::optional<QPointF> m_marqueeDragPixel;
    // Against the canvas edge the view pans toward the pointer.
    QTimer m_marqueeAutoscroll;
    std::optional<QPointF> m_marqueeAutoscrollPoint;
    QTimer m_antsTimer;
    // A repaint still pending skips a tick: slow outlines stutter.
    bool m_antsRepaintPending = false;
    std::optional<DocumentSelection> m_displayedOutline;
    std::optional<LassoDraft> m_displayedDraft;
    std::optional<ShapeShown> m_displayedShape;
    // What the lasso cursor last showed: kinds and mode.
    struct SelectionCursorState {
        LassoKind marquee;
        LassoKind lasso;
        SelectionMode mode;
        friend bool operator==(const SelectionCursorState &, const SelectionCursorState &) = default;
    };
    std::optional<SelectionCursorState> m_displayedSelectionCursor;
    BrushCursorOverlay m_brushCursor;
    std::unique_ptr<InlineTextEditor> m_inlineTextEditor;
    // A text box being drawn, in document pixels.
    std::optional<QPointF> m_textBoxAnchor;
    std::optional<QRectF> m_textBoxRect;
    // Qt lacks triple clicks: a press soon after a double.
    QElapsedTimer m_textDoubleClick;
    QPointF m_textDoubleClickPoint;
    // The circle's centre in view points; none off the canvas.
    std::optional<QPointF> m_brushPointer;
    // Shift's line keeps to the axis its first pixels chose.
    std::optional<QPointF> m_brushAxisAnchor;
    std::optional<bool> m_brushAxisHorizontal;
    std::optional<QPointF> m_brushLastPixel;
    // A right-drag: size or hardness from the press's values.
    struct BrushTipDrag {
        QPointF start;
        double diameter;
        double hardness;
        bool hardnessShown;
    };
    std::optional<BrushTipDrag> m_brushTipDrag;
    // Swift's cloneTipCache: rebuilt when size or hardness change.
    struct CloneTip {
        double diameter;
        double hardness;
        QImage image;
    };
    std::optional<CloneTip> m_cloneTip;
    // Which end of the pending gradient a drag moves.
    enum class GradientHandle { start, end };
    std::optional<GradientHandle> m_gradientDrag;
    // Swift's ClonePreviewKey: what the preview depends on.
    struct ClonePreviewKey {
        QPointF center;
        double diameter;
        double scale;
        int revision;
        int undoCount;
        bool allLayers;
        std::optional<QUuid> layerID;
        friend bool operator==(const ClonePreviewKey &, const ClonePreviewKey &) = default;
    };
    struct ClonePreview {
        ClonePreviewKey key;
        QImage image;
    };
    std::optional<ClonePreview> m_clonePreview;
    SampleRingOverlay m_sampleRing;
    // The painted layer's effects, made as its stroke starts.
    std::unique_ptr<LayerEffectsSurface> m_strokeSurface;
    // The colour a sampling press began from, for the ring.
    PaletteColor m_samplingOriginal = PaletteColor::black();
    bool m_samplingColor = false;
    bool m_displayedPicking = false;
    bool m_displayedTargeting = false;
    // Where a targeted drag began, in view points.
    std::optional<QPointF> m_hueTargetStart;
    std::optional<CropDrag> m_cropDrag;
    // Built at the press: the zoom holds still mid-drag.
    std::optional<CropSnap> m_cropSnap;
    // The crop frame as last shown, in view points.
    std::optional<QRectF> m_displayedCrop;
    bool m_guideDragging = false;
    // The grid and guides as drawn: a change redraws.
    struct GuidesShown {
        bool grid;
        bool guides;
        std::vector<CanvasGuide> lines;
        friend bool operator==(const GuidesShown &, const GuidesShown &) = default;
    };
    std::optional<GuidesShown> m_displayedGuideLines;
};
