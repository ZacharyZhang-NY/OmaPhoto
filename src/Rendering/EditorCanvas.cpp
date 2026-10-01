#include "Rendering/EditorCanvas.h"
#include "Document/BrushStroke.h"
#include "Document/LayerEffects+Renderer.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include "Rendering/AdjustmentSurface.h"
#include "Rendering/DownsampleCache.h"
#include "Rendering/LayerRenderer.h"
#include "Rendering/LiveMaskRenderer.h"
#include "Rendering/RasterSnapshot.h"
#include "Rendering/TiledLayerRenderer.h"
#include <QPaintEvent>
#include <QPointer>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace {
// Swift's shadow: 35% black, blurred over fourteen points.
void drawShadow(const QRectF &rect, QPainter &context)
{
    constexpr int blur = 14;
    const QRectF shadow = rect.translated(0, 3);
    // Fourteen rings stack to 35% at the edge, fading out.
    const double ring = 1 - std::pow(0.65, 1.0 / blur);
    context.save();
    context.setPen(Qt::NoPen);
    context.setBrush(QColor::fromRgbF(0, 0, 0, ring));
    for (int step = blur; step >= 1; --step)
        context.drawRect(shadow.adjusted(-step, -step, step, step));
    context.restore();
}

// Swift's halo: the widest reach of a visible adjustment.
double samplingMargin(const CanvasDocument &document)
{
    const QSet<QUuid> visible = document.effectiveVisibleIDs();
    double margin = 0;
    for (const ImageLayer &layer : document.layers) {
        if (layer.adjustment && visible.contains(layer.id))
            margin = std::max(margin, layer.adjustment->samplingMargin());
    }
    return margin;
}

std::optional<ImageIdentity> enabledMaskID(const std::optional<LayerMask> &mask)
{
    return mask && mask->isEnabled ? std::optional(mask->asset.identity()) : std::nullopt;
}
}

CanvasView::CanvasView(EditorSession &session, QWidget *parent)
    : QWidget(parent), m_session(session), m_overlay(session), m_marqueeAutoscroll(this), m_antsTimer(this)
{
    setObjectName(QStringLiteral("editorCanvas"));
    setAccessibleName(QStringLiteral("Canvas"));
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    // The Move tool's cursor follows the pointer between presses.
    setMouseTracking(true);
    // Sixty frames a second, as Swift's; named for tests.
    m_marqueeAutoscroll.setObjectName(QStringLiteral("marqueeAutoscroll"));
    m_marqueeAutoscroll.setInterval(16);
    connect(&m_marqueeAutoscroll, &QTimer::timeout, this, &CanvasView::stepMarqueeAutoscroll);
    m_antsTimer.setObjectName(QStringLiteral("antsTimer"));
    m_antsTimer.setInterval(120);
    connect(&m_antsTimer, &QTimer::timeout, this, &CanvasView::stepAnts);
    m_overlay.repaintAnts = [this] { update(m_overlay.selectionRect(rect())); };
}

CanvasView::DisplayState CanvasView::displayState() const
{
    const std::optional<CanvasDocument> &document = m_session.document();
    const std::optional<TransformEdit> &edit = m_session.transformEdit();
    DisplayState state{.brushRevision = m_session.brushRevision(),
                       .pixelGrid = m_session.showsPixelGrid(),
                       .corners = edit ? edit->corners : std::nullopt,
                       .documentID = document ? std::optional(document->id) : std::nullopt,
                       .size = document ? std::optional(document->size()) : std::nullopt,
                       .renderBounds = renderBounds(),
                       .viewport = m_session.viewport,
                       .layers = {},
                       .folderMasks = {},
                       .textStyle = m_session.textDraft() ? std::optional(m_session.textDraft()->style) : std::nullopt,
                       .textTransform = m_session.textDraft() && m_inlineTextEditor ? std::optional(m_inlineTextEditor->shownTransform()) : std::nullopt};
    if (!document)
        return state;
    // A hidden source still clips: live masks count every layer.
    const bool liveMasks = std::any_of(document->layers.begin(), document->layers.end(),
                                       [](const ImageLayer &layer) { return layer.maskSourceID.has_value(); });
    const std::vector<ImageLayer> rendered = liveMasks ? std::vector<ImageLayer>() : document->renderLayers();
    const QSet<QUuid> visible = document->effectiveVisibleIDs();
    // A folder's opacity reaches the canvas through its layers.
    const QHash<QUuid, double> opacities = document->effectiveOpacities();
    for (const ImageLayer &layer : liveMasks ? document->layers : rendered) {
        // A blank layer counts while a draft sits on it.
        if (!layer.asset && !layer.adjustment && !(m_session.shapeDraft() && layer.id == m_session.activeLayerID()))
            continue;
        state.layers.push_back({.id = layer.id, .transform = m_session.displayedTransform(layer),
                                .imageID = layer.asset ? std::optional(layer.asset->identity()) : std::nullopt,
                                .maskID = enabledMaskID(layer.mask), .maskSourceID = layer.maskSourceID, .parentID = layer.parentID,
                                .visible = visible.contains(layer.id), .opacity = opacities.value(layer.id),
                                .blendMode = m_session.displayedBlendMode(layer), .maskPlacement = m_session.displayedMaskPlacement(layer),
                                .adjustment = layer.adjustment, .effects = layer.effects});
    }
    for (const ImageLayer &layer : document->layers) {
        if (layer.isGroup && layer.mask)
            state.folderMasks.push_back({.id = layer.id, .maskID = enabledMaskID(layer.mask), .transform = m_session.displayedTransform(layer)});
    }
    return state;
}

bool CanvasView::synchronizeDisplay()
{
    handOnStrokeSurface();
    synchronizeInlineText();
    // A text box being drawn goes with its tool.
    if (m_session.tool() != NavigationTool::type && m_textBoxRect)
        endTextGesture();
    const DisplayState state = displayState();
    bool changed = false;
    // Where the overlay draws now; a pan moves it.
    const QRect drawn = m_overlay.drawnRect(rect());
    if (m_displayedState != state) {
        // A stroke's step alone repaints where the stroke went.
        DisplayState still = state;
        still.brushRevision = m_displayedState ? m_displayedState->brushRevision : state.brushRevision;
        const std::optional<QRectF> dirty = strokeDirtyRect();
        if (m_displayedState && still == *m_displayedState && dirty)
            update(dirty->toAlignedRect());
        else
            update();
        m_displayedState = state;
        m_overlayRect = drawn;
        changed = true;
    }
    // The handles and guides repaint only where they draw.
    const std::optional<TransformOverlayGeometry> geometry = m_overlay.geometry();
    const bool blocked = m_session.isProjectBusy() || m_session.isImporting();
    const std::optional<DocumentSelection> outline = m_session.displayedSelection();
    const SelectionCursorState cursorState{m_session.marqueeKind(), m_session.lassoKind(), m_session.displayedSelectionMode()};
    const std::optional<QRectF> crop = m_overlay.cropViewRect();
    if (m_displayedGeometry != geometry || m_displayedGuides != m_session.snapGuides || m_displayedOutline != outline
        || m_displayedDraft != m_session.lassoDraft() || m_displayedCrop != crop) {
        // Crop dimming spans the canvas: it comes and goes whole.
        if (m_displayedCrop.has_value() != crop.has_value())
            update();
        else
            update(QRegion(m_overlayRect) | QRegion(drawn));
        m_displayedCrop = crop;
        m_displayedGeometry = geometry;
        m_displayedGuides = m_session.snapGuides;
        m_displayedOutline = outline;
        m_displayedDraft = m_session.lassoDraft();
        m_overlayRect = drawn;
        updateCursor();
    } else if (m_displayedTool != m_session.tool() || m_displayedBlocked != blocked || m_displayedSelectionCursor != cursorState) {
        updateCursor();
    }
    // The grid and guides redraw the view, as Swift's observation.
    if (const GuidesShown lines{m_session.showsGrid(), m_session.layoutGrid(), m_session.gridAppearance(), m_session.showsGuides(), m_session.displayedGuides()}; m_displayedGuideLines != lines) {
        update();
        m_displayedGuideLines = lines;
    }
    // A shape draft redraws the view, as Swift's observation does.
    if (const std::optional<ShapeShown> shape = shownShape(); m_displayedShape != shape) {
        update();
        m_displayedShape = shape;
    }
    syncPicking();
    m_displayedTool = m_session.tool();
    m_displayedBlocked = blocked;
    m_displayedSelectionCursor = cursorState;
    updateBrushCursor();
    updateAntsTimer();
    return changed;
}

// Crop shows what a frame past the canvas takes in.
std::optional<QRectF> CanvasView::renderBounds() const
{
    const std::optional<CanvasDocument> &document = m_session.document();
    if (!document)
        return std::nullopt;
    const QRectF original(QPointF(0, 0), document->size());
    // Swift's tool test is dead: frames live under Crop alone.
    return m_session.cropRect() ? original.united(*m_session.cropRect()) : original;
}

void CanvasView::paintEvent(QPaintEvent *event)
{
    m_antsRepaintPending = false;
    // The dirty rect, image-backed: hand blends and coverage read pixels.
    QPainter painter(this);
    painter.setClipRect(event->rect());
    AdjustmentSurface::draw(painter, [&](QPainter &surface) { draw(surface, QRectF(event->rect())); });
    // Swift's overlay view: handles and guides over the pixels.
    m_overlay.draw(painter, palette());
    // Swift's stack: the ring over the overlays, the editor above.
    m_sampleRing.draw(painter);
    if (m_inlineTextEditor)
        m_inlineTextEditor->draw(painter);
    m_brushCursor.draw(painter);
    drawTextBoxDraft(painter);
}

void CanvasView::draw(QPainter &context, const QRectF &dirty)
{
    // The surround is the palette's Base: Swift's 0.105 when dark.
    context.fillRect(dirty, palette().color(QPalette::Base));
    const std::optional<CanvasDocument> &document = m_session.document();
    if (!document)
        return;
    const CanvasViewport &viewport = m_session.viewport;
    const QRectF pixels = renderBounds().value();
    const double points = viewport.pointsPerPixel();
    const QRectF rect(viewport.viewPoint(pixels.topLeft(), document->size()), QSizeF(pixels.width() * points, pixels.height() * points));
    const QRectF bounds(QPointF(0, 0), QSizeF(size()));
    if (!rect.intersects(bounds) || !rect.intersects(dirty))
        return;
    // Inside the document its opaque fill covers the shadow.
    if (!rect.adjusted(1, 1, -1, -1).contains(dirty))
        drawShadow(rect, context);
    context.save();
    context.setClipRect(rect.intersected(dirty));
    context.fillRect(rect, QColor::fromRgbF(0.30, 0.30, 0.30));
    // Work scales with the visible viewport, not the document.
    constexpr double tile = 10;
    const QRectF visible = rect.intersected(bounds).intersected(dirty);
    if (!visible.isEmpty()) {
        const int minX = int(std::floor((visible.left() - rect.left()) / tile)), maxX = int(std::ceil((visible.right() - rect.left()) / tile));
        const int minY = int(std::floor((visible.top() - rect.top()) / tile)), maxY = int(std::ceil((visible.bottom() - rect.top()) / tile));
        for (int row = minY; row < maxY; ++row) {
            for (int column = minX; column < maxX; ++column) {
                if ((row + column) % 2 == 0)
                    context.fillRect(QRectF(rect.left() + column * tile, rect.top() + row * tile, tile, tile), QColor::fromRgbF(0.35, 0.35, 0.35));
            }
        }
    }
    if (viewport.zoom() >= crispZoom && !visible.isEmpty()) {
        drawDocumentPixels(visible, pixels, *document, context);
    } else {
        // Its own group: blend modes never meet the checkerboard.
        AdjustmentSurface::draw(context, [&](QPainter &group) {
            drawLayers(*document, points, [&](QPointF point) { return viewport.viewPoint(point, document->size()); }, group);
        }, samplingMargin(*document) * points);
    }
    if (m_session.showsPixelGrid() && viewport.zoom() >= pixelGridZoom && !visible.isEmpty())
        drawPixelGrid(visible, *document, context);
    context.restore();
    // A hairline astride the edge, soft as CoreGraphics strokes it.
    context.save();
    context.setRenderHint(QPainter::Antialiasing, true);
    context.setPen(QPen(QColor::fromRgbF(1, 1, 1, 0.13), 1 / viewport.backingScale));
    context.setBrush(Qt::NoBrush);
    context.drawRect(rect);
    context.restore();
}

// The folder's live mask, multiplied into the coverage.
FolderMaskClip::Applier CanvasView::liveFolderMaskClip(const BrushStroke &edit, double scale, const Center &center) const
{
    return [&edit, scale, center](const QPainter &painter, QImage &coverage) {
        QImage live(coverage.size(), QImage::Format_Grayscale8);
        if (live.isNull())
            throw ExportError(ExportError::Kind::render);
        // Outside the mask's bounds stays hidden, as when committed.
        live.fill(0);
        LayerTransform transform = edit.paintTransform;
        transform.sampling = LayerSampling::nearest;
        const std::optional<ImportedImage> &base = edit.layer.mask ? std::optional(edit.layer.mask->asset) : std::nullopt;
        {
            QPainter drawing(&live);
            drawing.setTransform(painter.deviceTransform());
            // Swift's displayImage: halved near the size drawn.
            const double device = LayerRenderer::deviceScale(drawing);
            const auto shown = [device](const QImage &image, double width) {
                return DownsampleCache::shared().imageDrawnAt(image, width * device / std::max(1, image.width()));
            };
            const std::shared_ptr<const RasterSnapshot> raster = base ? base->raster : nullptr;
            const QImage image = base && !raster ? shown(base->image(), transform.size.width() * scale) : QImage();
            const QImage rasterBase = raster && !raster->base.isNull()
                ? shown(raster->base, transform.size.width() * scale * raster->baseRect.width() / std::max(1, raster->width))
                : QImage();
            LayerRenderer::drawBrushPreview(image, transform, center(transform.center()), drawing, {.scale = scale},
                                            {.patches = edit.patches(), .pixelWidth = edit.width, .pixelHeight = edit.height, .paintingMask = false,
                                             .sourceRect = edit.sourceRect, .raster = raster, .rasterBase = rasterBase});
        }
        QPainter multiplying(&coverage);
        if (!multiplying.isActive())
            throw ExportError(ExportError::Kind::render);
        multiplying.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        multiplying.drawImage(QRectF(coverage.rect()), BrushRaster::alphaView(live));
    };
}

void CanvasView::drawLayers(const CanvasDocument &document, double scale, const Center &center, QPainter &context)
{
    handOnDraftEffects(document);
    m_session.effectsPreviews.prepare(document.layers);
    std::map<QUuid, ImageLayer> byID;
    for (const ImageLayer &layer : document.layers) {
        if (!byID.emplace(layer.id, layer).second)
            throw std::logic_error("a document repeats a layer id");
    }
    // A preview that lands repaints the view.
    const std::function<void()> repaint = [canvas = QPointer<CanvasView>(this)] {
        if (canvas)
            canvas->update();
    };
    const auto drawOwn = [&](QUuid id, QPainter &target, const QImage &clip) {
        if (!byID.contains(id))
            return;
        const ImageLayer &layer = byID.at(id);
        // Its folders dim it with everything inside them.
        const double opacity = layer.effectiveOpacity(byID);
        // Text being edited draws as it will be committed.
        if (m_session.textDraft() && m_session.textDraft()->layerID == id) {
            drawTypedText(layer, {.scale = scale, .opacity = opacity, .blendMode = m_session.displayedBlendMode(layer), .clip = clip}, center, target);
            return;
        }
        // A stroke or pixel move stands in for the pixels.
        const PixelMove *move = m_session.pixelMove();
        const std::optional<GradientEdit> &gradient = m_session.gradientEdit();
        const BrushStroke *stroke = m_session.brushStroke() && m_session.brushStroke()->layer.id == id ? m_session.brushStroke()
            : gradient && gradient->raster->layer.id == id                   ? gradient->raster.get()
            : move && move->raster->layer.id == id                           ? move->raster.get()
                                                                             : nullptr;
        // An empty layer draws nothing, unless Vignette previews onto it.
        if (!layer.asset && !stroke && !(m_session.filterEdit() && m_session.filterEdit()->previewImage(id)))
            return;
        // Smudge or Liquify under way: the reshaped copy, canvas-wide.
        if (const WarpStroke *warp = m_session.warpStroke(); warp && warp->layer.id == id) {
            const LayerTransform canvas{.origin = {0, 0}, .size = document.size()};
            const std::optional<QImage> mask = layer.mask ? layer.mask->clipImage(layer.maskTransform(), canvas, warp->width, warp->height, 2048)
                                                          : std::nullopt;
            LayerRenderer::draw(warp->image(), canvas, center(canvas.center()), target,
                                {.scale = scale, .opacity = opacity, .blendMode = m_session.displayedBlendMode(layer),
                                 .mask = mask.value_or(QImage()), .clip = clip});
            return;
        }
        // A stroke draws its grid, a placed mask's the layer's.
        const bool placedStroke = stroke && stroke->isMask && stroke->layer.mask && stroke->layer.mask->placement;
        const LayerTransform transform = stroke && !placedStroke ? stroke->paintTransform : m_session.displayedTransform(layer);
        const std::optional<LayerTransform> placement = m_session.displayedMaskPlacement(layer);
        std::optional<QImage> mask;
        if (const std::optional<QImage> distorted = m_session.maskDistortPreview(layer)) {
            mask = distorted;
        } else if (layer.mask && !placement) {
            mask = layer.mask->enabledImage();
        } else if (layer.mask) {
            // A placed mask in the layer's grid; 2048 while moving.
            const ImageLayer &owner = stroke ? stroke->layer : layer;
            const LayerTransform base = stroke ? owner.transform : transform;
            const double drawn = std::max(base.size.width(), base.size.height()) * scale * LayerRenderer::deviceScale(context);
            const double steady = std::pow(2, std::ceil(std::log2(std::max(64.0, drawn))));
            const QSize size = owner.asset ? owner.asset->size() : QSize(int(std::round(base.size.width())), int(std::round(base.size.height())));
            mask = layer.mask->clipImage(placement, base, size.width(), size.height(), m_session.transformEdit() ? std::min(2048.0, steady) : steady);
        }
        const LayerRenderer::Options options{.scale = scale, .opacity = opacity, .blendMode = m_session.displayedBlendMode(layer),
                                             .mask = mask.value_or(QImage()), .clip = clip};
        // Swift asks twice; once, with what shows, is one request.
        const std::optional<EffectsPreviewCache::Result> effects =
            stroke ? std::nullopt : m_session.effectsPreviews.preview(layer, mask, transform, placement, repaint);
        const LayerRenderer::Options bare{.scale = scale, .opacity = opacity, .blendMode = options.blendMode, .clip = clip};
        // A pending distortion warps the effects along with the layer.
        if (const auto warped = effects ? m_session.distortedEffects(layer, effects->image, effects->inset) : std::nullopt) {
            LayerRenderer::draw(warped->image, warped->transform, center(warped->transform.center()), target, bare);
            return;
        }
        // A pending distortion shows the layer warped into its shape.
        if (const std::optional<DistortPreview> distorted = m_session.distortPreview(layer)) {
            LayerRenderer::Options through = bare;
            through.mask = distorted->mask.value_or(QImage());
            LayerRenderer::draw(distorted->image, distorted->transform, center(distorted->transform.center()), target, through);
            return;
        }
        // Effects round the pixels, on a grown canvas.
        if (effects) {
            const LayerTransform grown = effects->placement.value_or(LayerEffectsRenderer::placed(transform, effects->image, effects->inset));
            LayerRenderer::draw(effects->image, grown, center(grown.center()), target, bare);
            return;
        }
        if (stroke) {
            drawStroke(*stroke, layer, transform, mask, options, scale, center, target);
            return;
        }
        // A rounded rectangle keeps its corners while it is resized.
        if (const QImage shaped = m_session.shapeTransformPreview(layer, transform); !shaped.isNull()) {
            LayerRenderer::draw(shaped, transform, center(transform.center()), target, options);
            return;
        }
        // An adjustment's or filter's preview stands in for the pixels.
        std::optional<QImage> preview = m_session.filterEdit() ? m_session.filterEdit()->previewImage(layer.id) : std::nullopt;
        if (!preview && m_session.levels())
            preview = m_session.levels()->previewImage(layer.id);
        if (!preview && m_session.hueSaturation())
            preview = m_session.hueSaturation()->previewImage(layer.id);
        if (preview)
            LayerRenderer::draw(*preview, transform, center(transform.center()), target, options);
        else if (layer.asset->raster)
            TiledLayerRenderer::drawRaster(layer.asset->raster, transform, center(transform.center()), target, options);
        else
            LayerRenderer::draw(layer.asset->image(), transform, center(transform.center()), target, options);
    };
    // Above the active layer, where the new layer will go.
    bool drewNewText = false;
    const auto drawOwnWithDraft = [&](QUuid id, QPainter &target, const QImage &clip) {
        drawOwn(id, target, clip);
        if (id != m_session.activeLayerID())
            return;
        drawShapeDraft(scale, center, target, clip);
        drawNewText(drewNewText, scale, center, target, clip);
    };
    const auto parent = [&](QUuid id) { return byID.contains(id) ? byID.at(id).parentID : std::nullopt; };
    // A mask being painted exists as the edit's tiles alone.
    const auto maskEdit = [&](QUuid id) -> const BrushStroke * {
        const BrushStroke *brush = m_session.brushStroke();
        const BrushStroke *gradient = m_session.gradientEdit() ? m_session.gradientEdit()->raster.get() : nullptr;
        for (const BrushStroke *edit : {brush, gradient}) {
            if (edit && edit->isMask && edit->layer.id == id)
                return edit;
        }
        return nullptr;
    };
    LiveMaskRenderer live([&](QUuid id) { return byID.contains(id) ? byID.at(id).maskSourceID : std::nullopt; }, drawOwnWithDraft);
    live.adjustment = [&](QUuid id) { return byID.contains(id) ? byID.at(id).adjustment : std::nullopt; };
    live.adjustmentOpacity = [&](QUuid id) { return byID.at(id).effectiveOpacity(byID); };
    live.adjustmentScale = scale;
    live.adjustmentClip = [&](QUuid id, const QPainter &painter, QImage &coverage) {
        const ImageLayer &layer = byID.at(id);
        if (!layer.mask || !layer.mask->isEnabled)
            return;
        if (const BrushStroke *edit = maskEdit(id)) {
            liveFolderMaskClip(*edit, scale, center)(painter, coverage);
            return;
        }
        FolderMaskClip{layer.mask->asset.image(), layer.transform}.apply(center(layer.transform.center()), painter, coverage, scale);
    };
    std::vector<QUuid> ids;
    for (const ImageLayer &layer : document.renderLayers())
        ids.push_back(layer.id);
    live.prepareStacks(ids, parent, [&](QUuid id) { return byID.contains(id) ? m_session.displayedBlendMode(byID.at(id)) : LayerBlendMode::normal; });
    FolderMaskClip::draw(ids, parent, [&](QUuid id) -> std::optional<FolderMaskClip::Applier> {
        if (!byID.contains(id) || !byID.at(id).mask || !byID.at(id).mask->isEnabled)
            return std::nullopt;
        const ImageLayer &folder = byID.at(id);
        if (const BrushStroke *edit = maskEdit(id))
            return liveFolderMaskClip(*edit, scale, center);
        const LayerTransform transform = m_session.displayedTransform(folder);
        // A sharp halving near the mask's drawn size.
        const QImage image = folder.mask->asset.image();
        const double factor = transform.size.width() * scale * LayerRenderer::deviceScale(context) / std::max(1, image.width());
        const FolderMaskClip clip{DownsampleCache::shared().imageDrawnAt(image, factor), transform};
        const QPointF origin = center(transform.center());
        return [clip, origin, scale](const QPainter &painter, QImage &coverage) { clip.apply(origin, painter, coverage, scale); };
    }, context, [&](QUuid id, const QImage &clip) { live.drawComposite(id, context, clip); });
    // Else on top: the active layer, a folder or hidden.
    drawNewText(drewNewText, scale, center, context, QImage());
}

void CanvasView::drawDocumentPixels(const QRectF &view, const QRectF &pixels, const CanvasDocument &document, QPainter &context)
{
    const CanvasViewport &viewport = m_session.viewport;
    const QPointF topLeft = viewport.documentPoint(view.topLeft(), document.size());
    const QPointF bottomRight = viewport.documentPoint(view.bottomRight(), document.size());
    const QRectF region = QRectF(std::floor(topLeft.x()), std::floor(topLeft.y()), std::ceil(bottomRight.x()) - std::floor(topLeft.x()),
                                 std::ceil(bottomRight.y()) - std::floor(topLeft.y()))
                              .intersected(QRectF(pixels.toAlignedRect()));
    if (region.isEmpty() || region.width() < 1 || region.height() < 1)
        return;
    QImage raster;
    try {
        raster = BrushRaster::context(int(region.width()), int(region.height()), false);
    } catch (const ExportError &error) {
        qCWarning(lcRendering) << "document pixels could not be drawn:" << error.what();
        return;
    }
    {
        QPainter painter(&raster);
        AdjustmentSurface::draw(painter, [&](QPainter &group) {
            drawLayers(document, 1, [&](QPointF point) { return QPointF(point.x() - region.left(), point.y() - region.top()); }, group);
        }, samplingMargin(document));
    }
    const QPointF origin = viewport.viewPoint(region.topLeft(), document.size());
    const QRectF target(origin, QSizeF(region.width() * viewport.pointsPerPixel(), region.height() * viewport.pointsPerPixel()));
    context.save();
    context.setRenderHint(QPainter::SmoothPixmapTransform, false);
    context.drawImage(target, raster);
    context.restore();
}
