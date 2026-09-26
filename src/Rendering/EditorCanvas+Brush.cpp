#include "Rendering/EditorCanvas.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore.h"
#include "Logging.h"
#include <QKeyEvent>
#include <cmath>

// Swift's brush on the canvas: strokes, tip drag, circle.
void CanvasView::brushMouseDown(QPointF point, Qt::KeyboardModifiers modifiers)
{
    const QPointF pixel = m_session.viewport.documentPoint(point, m_session.document().value().size());
    // Alt-click with Clone Stamp sets where it copies from.
    if (m_session.tool() == NavigationTool::cloneStamp && modifiers.testFlag(Qt::AltModifier)) {
        m_session.setCloneSource(pixel);
        updateBrushCursor();
        return;
    }
    m_brushPointer = point;
    // Shift paints a line on from the last stroke's end.
    const bool shift = modifiers.testFlag(Qt::ShiftModifier);
    const std::optional<QPointF> from = shift ? m_session.shiftLineStart() : std::nullopt;
    if (from) {
        m_session.beginBrush(*from);
        m_session.continueBrush(pixel);
    } else {
        m_session.beginBrush(pixel);
    }
    m_brushAxisAnchor = shift ? std::optional(pixel) : std::nullopt;
    m_brushAxisHorizontal = std::nullopt;
    m_brushLastPixel = pixel;
    synchronizeDisplay();
}

// The circle follows; a drag goes on, Shift keeping axis.
bool CanvasView::brushMouseMove(QPointF point, Qt::KeyboardModifiers modifiers, bool dragging)
{
    m_brushPointer = point;
    updateBrushCursor();
    // Swift's mouseMoved paints nothing; mouseDragged does.
    if (!dragging || (!m_session.brushStroke() && !m_session.warpStroke()) || m_session.isProjectBusy() || !m_session.document())
        return false;
    QPointF pixel = m_session.viewport.documentPoint(point, m_session.document()->size());
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        // The first three pixels settle the axis; it never flips.
        const QPointF anchor = m_brushAxisAnchor ? *m_brushAxisAnchor : m_brushLastPixel.value_or(pixel);
        if (!m_brushAxisAnchor) {
            m_brushAxisAnchor = anchor;
            m_brushAxisHorizontal = std::nullopt;
        }
        if (!m_brushAxisHorizontal && std::hypot(pixel.x() - anchor.x(), pixel.y() - anchor.y()) >= 3)
            m_brushAxisHorizontal = std::abs(pixel.x() - anchor.x()) >= std::abs(pixel.y() - anchor.y());
        if (m_brushAxisHorizontal)
            pixel = *m_brushAxisHorizontal ? QPointF(pixel.x(), anchor.y()) : QPointF(anchor.x(), pixel.y());
        else
            pixel = anchor;
    } else {
        // Re-anchoring resets the axis, so the anchor alone goes.
        m_brushAxisAnchor = std::nullopt;
    }
    m_brushLastPixel = pixel;
    m_session.continueBrush(pixel);
    synchronizeDisplay();
    return true;
}

// The release reaches its own point, then the stroke lands.
void CanvasView::brushMouseUp(QPointF point)
{
    if ((!m_session.brushStroke() && !m_session.warpStroke()) || m_session.isProjectBusy())
        return;
    if (m_session.document())
        m_session.continueBrush(m_session.viewport.documentPoint(point, m_session.document()->size()));
    m_session.finishBrushImmediately();
    synchronizeDisplay();
}

// Right-drag: size across, hardness with Shift; the circle stays.
void CanvasView::beginBrushTipDrag(QPointF point, Qt::KeyboardModifiers modifiers)
{
    const BrushSettings &settings = m_session.brushSettings();
    m_brushTipDrag = BrushTipDrag{point, settings.diameter, settings.hardness, modifiers.testFlag(Qt::ShiftModifier)};
    m_brushPointer = point;
    updateBrushCursor();
}

void CanvasView::dragBrushTip(QPointF point, Qt::KeyboardModifiers modifiers)
{
    BrushTipDrag &drag = m_brushTipDrag.value();
    drag.hardnessShown = modifiers.testFlag(Qt::ShiftModifier);
    const double dx = point.x() - drag.start.x();
    BrushSettings settings = m_session.brushSettings();
    if (drag.hardnessShown) {
        // The full range across 200 points.
        settings.hardness = std::clamp(drag.hardness + dx / 200, 0.0, 1.0);
        settings.diameter = drag.diameter;
    } else {
        // The circle's edge follows the pointer on screen.
        const double perPixel = std::max(0.0001, m_session.viewport.pointsPerPixel());
        settings.diameter = std::clamp(std::round(drag.diameter + 2 * dx / perPixel), 1.0, 2000.0);
        settings.hardness = drag.hardness;
    }
    m_session.setBrushSettings(settings);
    m_brushPointer = drag.start;
    updateBrushCursor();
}

void CanvasView::endBrushTipDrag(QPointF point)
{
    m_brushTipDrag = std::nullopt;
    m_brushPointer = point;
    updateBrushCursor();
}

// The circle at the pointer, a stroke's own size.
void CanvasView::updateBrushCursor()
{
    const bool shows = isBrushTool(m_session.tool()) && !m_spaceHeld && !picking();
    const double diameter = m_session.brushStroke() ? m_session.brushStroke()->settings.diameter : m_session.brushSettings().diameter;
    // Clone Stamp marks its source; between strokes, previews a click.
    std::optional<QPointF> sample;
    QImage preview;
    const std::optional<CanvasDocument> &document = m_session.document();
    if (shows && m_session.tool() == NavigationTool::cloneStamp && m_brushPointer && document) {
        const QPointF point = m_session.viewport.documentPoint(*m_brushPointer, document->size());
        if (const std::optional<QPointF> source = m_session.cloneSamplePoint(point))
            sample = m_session.viewport.viewPoint(*source, document->size());
        const std::optional<QSizeF> offset = m_session.cloneStrokeOffset(point);
        if (!m_session.brushStroke() && !m_optionHeld && offset)
            preview = clonePreview(point + QPointF(offset->width(), offset->height()), diameter, *document);
    }
    const std::optional<double> hardness = m_brushTipDrag && m_brushTipDrag->hardnessShown ? std::optional(m_session.brushSettings().hardness) : std::nullopt;
    const QImage tip = preview.isNull() ? QImage() : cloneTip(diameter, m_session.brushSettings().hardness);
    update(m_brushCursor.update(shows ? m_brushPointer : std::nullopt, std::max(1.0, diameter * m_session.viewport.pointsPerPixel()), hardness, sample,
                                preview, m_session.brushSettings().opacity, tip));
}

// The brush engine paints one click; the preview softens alike.
QImage CanvasView::cloneTip(double diameter, double hardness)
{
    if (m_cloneTip && m_cloneTip->diameter == diameter && m_cloneTip->hardness == hardness)
        return m_cloneTip->image;
    const int side = std::max(1, int(std::ceil(diameter)));
    const QSizeF size(side, side);
    BrushSettings settings{.diameter = diameter, .hardness = hardness, .red = 1, .green = 1, .blue = 1};
    QImage image;
    try {
        BrushStroke stroke(ImageLayer(QStringLiteral("Tip"), size), false, settings, size);
        stroke.append(QPointF(side / 2.0, side / 2.0));
        stroke.flush();
        const PaintSnapshot painted = stroke.paintSnapshot();
        QImage context = BrushRaster::context(side, side, false);
        QPainter painter(&context);
        BrushRaster::draw(painted.asset.image(), painted.bounds, painter);
        painter.end();
        image = context;
    } catch (const ProjectError &error) {
        qCWarning(lcRendering) << "the clone tip cannot be painted:" << error.what();
    } catch (const ExportError &error) {
        qCWarning(lcRendering) << "the clone tip cannot be painted:" << error.what();
    }
    m_cloneTip = CloneTip{diameter, hardness, image};
    return image;
}

// The source around `center` at screen resolution, kept until changed.
QImage CanvasView::clonePreview(QPointF center, double diameter, const CanvasDocument &document)
{
    const double scale = m_session.viewport.pointsPerPixel() * m_session.viewport.backingScale;
    const ClonePreviewKey key{center, diameter, scale, m_session.brushRevision(), m_session.history.undoCount(),
                              m_session.cloneSettings().sampleAllLayers, m_session.activeLayerID()};
    if (m_clonePreview && m_clonePreview->key == key)
        return m_clonePreview->image;
    const int side = std::min(1024, std::max(1, int(std::ceil(diameter * scale))));
    QImage image;
    try {
        QImage context = BrushRaster::context(side, side, false);
        QPainter painter(&context);
        const double perPixel = side / diameter;
        painter.scale(perPixel, perPixel);
        painter.translate(diameter / 2 - center.x(), diameter / 2 - center.y());
        // A painted asset flattens here; the catch covers it too.
        if (m_session.cloneSettings().sampleAllLayers) {
            m_session.drawLiveComposite(document, painter);
        } else if (const std::optional<ImageLayer> layer = m_session.activeLayer(); layer && layer->asset) {
            const LayerTransform transform = m_session.displayedTransform(*layer);
            LayerRenderer::draw(layer->asset->image(), transform, transform.center(), painter, {});
        }
        painter.end();
        image = context;
    } catch (const ExportError &error) {
        qCWarning(lcRendering) << "the clone preview cannot be drawn:" << error.what();
    }
    m_clonePreview = ClonePreview{key, image};
    return image;
}

// Swift's brush keys: Tab, B, E, digits, brackets.
bool CanvasView::brushKey(const QKeyEvent &key)
{
    if (key.modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return false;
    if (key.key() == Qt::Key_Tab && !key.modifiers().testFlag(Qt::ShiftModifier)) {
        m_session.cycleToolMode();
        updateCursor();
        updateBrushCursor();
        return true;
    }
    if (key.key() == Qt::Key_B || key.key() == Qt::Key_E) {
        m_session.selectTool(NavigationTool::brush);
        m_session.setBrushMode(key.key() == Qt::Key_E ? BrushToolMode::erase : BrushToolMode::paint);
        return true;
    }
    const QString text = key.text();
    if (text.size() == 1 && text.front().isDigit() && m_session.usesOpacityKeys()) {
        m_session.typeOpacityDigit(text.front().digitValue());
        return true;
    }
    return brushBracket(text);
}

// Brackets: size, and hardness with Shift, for brushes.
bool CanvasView::brushBracket(const QString &text)
{
    if (!isBrushTool(m_session.tool()) || (text != u"[" && text != u"]" && text != u"{" && text != u"}"))
        return false;
    if (text == u"[" || text == u"]")
        m_session.changeBrushSize(text == u"]");
    else
        m_session.changeBrushHardness(text == u"}");
    return true;
}
