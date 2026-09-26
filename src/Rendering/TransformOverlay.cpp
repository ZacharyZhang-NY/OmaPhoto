#include "Rendering/TransformOverlay.h"
#include <QPainterPath>
#include <cmath>
#include <numbers>

TransformOverlayGeometry::TransformOverlayGeometry(const LayerTransform &transform, const CanvasViewport &viewport, QSizeF documentSize)
{
    for (size_t index = 0; index < handles.size(); ++index)
        handles[index] = viewport.viewPoint(transform.point(LayerTransform::handles[index]), documentSize);
    rotationHandle = handles[1] + QPointF(std::sin(transform.radians()) * 28, -std::cos(transform.radians()) * 28);
    showsRotation = true;
}

TransformOverlayGeometry::TransformOverlayGeometry(const Corners &corners, const CanvasViewport &viewport, QSizeF documentSize)
{
    std::array<QPointF, 4> view;
    for (size_t index = 0; index < 4; ++index)
        view[index] = viewport.viewPoint(corners[index], documentSize);
    const auto middle = [](QPointF a, QPointF b) { return QPointF((a.x() + b.x()) / 2, (a.y() + b.y()) / 2); };
    handles = {view[0], middle(view[0], view[1]), view[1], middle(view[1], view[2]), view[2], middle(view[2], view[3]), view[3], middle(view[3], view[0])};
    rotationHandle = handles[1];
    showsRotation = false;
}

std::optional<TransformDrag::Mode> TransformOverlayGeometry::hit(QPointF point) const
{
    const auto near = [&](QPointF other) { return std::hypot(point.x() - other.x(), point.y() - other.y()) <= 10; };
    if (showsRotation && near(rotationHandle))
        return TransformDrag::Mode{TransformDrag::Kind::rotate};
    for (size_t index = 0; index < handles.size(); ++index) {
        if (near(handles[index]))
            return TransformDrag::Mode{TransformDrag::Kind::resize, int(index)};
    }
    // Along an edge, its middle handle: the whole edge resizes.
    for (const auto &[start, end, handle] : {std::tuple(0, 2, 1), std::tuple(2, 4, 3), std::tuple(4, 6, 5), std::tuple(6, 0, 7)}) {
        const QPointF a = handles[start], b = handles[end];
        const double dx = b.x() - a.x(), dy = b.y() - a.y();
        const double lengthSquared = dx * dx + dy * dy;
        if (lengthSquared <= 0)
            continue;
        const double t = ((point.x() - a.x()) * dx + (point.y() - a.y()) * dy) / lengthSquared;
        if (t >= 0 && t <= 1 && std::hypot(point.x() - a.x() - t * dx, point.y() - a.y() - t * dy) <= 10)
            return TransformDrag::Mode{TransformDrag::Kind::resize, handle};
    }
    return std::nullopt;
}

Qt::CursorShape TransformOverlayGeometry::resizeCursor(int index) const
{
    constexpr double quarter = std::numbers::pi / 4;
    const double angle = std::atan2(handles[2].y() - handles[0].y(), handles[2].x() - handles[0].x());
    const std::array<double, 8> offsets = {quarter, 2 * quarter, 3 * quarter, 0, quarter, 2 * quarter, 3 * quarter, 0};
    const int direction = (int(std::round((angle + offsets[size_t(index)]) / quarter)) % 4 + 4) % 4;
    // Swift's frame positions: right, bottom right, bottom, top right.
    const std::array<Qt::CursorShape, 4> shapes = {Qt::SizeHorCursor, Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor};
    return shapes[size_t(direction)];
}

std::optional<TransformOverlayGeometry> TransformOverlay::geometry() const
{
    const std::optional<TransformEdit> &edit = m_session.transformEdit();
    const std::optional<CanvasDocument> &document = m_session.document();
    if (m_session.tool() != NavigationTool::move || !(m_session.showsTransformControls() || (edit && edit->persistent)) || !document)
        return std::nullopt;
    // Several layers or a folder: one box around them all.
    if ((edit && edit->group) || (!edit && m_session.transformsAsGroup())) {
        if (edit && edit->corners)
            return TransformOverlayGeometry(*edit->corners, m_session.viewport, document->size());
        const std::optional<LayerTransform> box = edit ? std::optional(edit->draft) : m_session.groupTransformBox();
        if (!box)
            return std::nullopt;
        return TransformOverlayGeometry(*box, m_session.viewport, document->size());
    }
    const std::optional<ImageLayer> layer = m_session.activeLayer();
    if (!layer || !layer->asset || layer->isGroup || !document->effectiveVisibleIDs().contains(layer->id))
        return std::nullopt;
    if (edit && edit->layerID == layer->id && edit->corners)
        return TransformOverlayGeometry(*edit->corners, m_session.viewport, document->size());
    return TransformOverlayGeometry(m_session.editedTransform(*layer), m_session.viewport, document->size());
}

QRect TransformOverlay::drawnRect(const QRect &canvas) const
{
    const SnapGuides &guides = m_session.snapGuides;
    if (!guides.xs.empty() || !guides.ys.empty())
        return canvas;
    QRect drawn;
    if (const std::optional<TransformOverlayGeometry> geometry = this->geometry()) {
        QPointF low = geometry->rotationHandle, high = low;
        for (const QPointF handle : geometry->handles) {
            low = {std::min(low.x(), handle.x()), std::min(low.y(), handle.y())};
            high = {std::max(high.x(), handle.x()), std::max(high.y(), handle.y())};
        }
        // The strokes reach five points past a handle's middle.
        drawn = QRectF(low, high).adjusted(-5, -5, 5, 5).toAlignedRect();
    }
    // Outside both frames the dimming stays: only the frames change.
    if (const std::optional<QRectF> crop = cropViewRect())
        drawn |= crop->adjusted(-5, -5, 5, 5).toAlignedRect();
    drawn |= selectionRect(canvas);
    if (const std::optional<LassoDraft> &draft = m_session.lassoDraft()) {
        QPointF low = draft->points.front(), high = low;
        for (const QPointF point : draft->points) {
            low = {std::min(low.x(), point.x()), std::min(low.y(), point.y())};
            high = {std::max(high.x(), point.x()), std::max(high.y(), point.y())};
        }
        if (draft->cursor) {
            low = {std::min(low.x(), draft->cursor->x()), std::min(low.y(), draft->cursor->y())};
            high = {std::max(high.x(), draft->cursor->x()), std::max(high.y(), draft->cursor->y())};
        }
        // The first corner's handle reaches five points out.
        drawn |= documentToView().mapRect(QRectF(low, high)).adjusted(-5, -5, 5, 5).toAlignedRect();
    }
    return drawn.intersected(canvas);
}

QRect TransformOverlay::selectionRect(const QRect &canvas) const
{
    const std::optional<DocumentSelection> selection = m_session.displayedSelection();
    if (!selection || selection->isEmpty())
        return QRect();
    return documentToView().map(selection->path).boundingRect().adjusted(-2, -2, 2, 2).toAlignedRect().intersected(canvas);
}

QTransform TransformOverlay::documentToView() const
{
    const std::optional<CanvasDocument> &document = m_session.document();
    if (!document)
        return QTransform();
    const QPointF origin = m_session.viewport.documentRect(document->size()).topLeft();
    const double scale = m_session.viewport.pointsPerPixel();
    return QTransform::fromTranslate(origin.x(), origin.y()).scale(scale, scale);
}

namespace {
std::array<QPointF, 8> cropHandles(const QRectF &rect)
{
    std::array<QPointF, 8> handles;
    for (size_t index = 0; index < handles.size(); ++index)
        handles[index] = QPointF(rect.left() + LayerTransform::handles[index].x() * rect.width(), rect.top() + LayerTransform::handles[index].y() * rect.height());
    return handles;
}
}

// Swift's document guard is dead: visibleCropRect reads it.
std::optional<QRectF> TransformOverlay::cropViewRect() const
{
    const std::optional<QRectF> rect = m_session.visibleCropRect();
    if (!rect)
        return std::nullopt;
    const double scale = m_session.viewport.pointsPerPixel();
    return QRectF(m_session.viewport.viewPoint(rect->topLeft(), m_session.document().value().size()), rect->size() * scale);
}

std::vector<TransformOverlay::CropRegion> TransformOverlay::cropResizeRegions() const
{
    const std::optional<QRectF> rect = cropViewRect();
    if (!rect)
        return {};
    const std::array<QPointF, 8> handles = cropHandles(*rect);
    constexpr double radius = 10;
    std::vector<CropRegion> regions;
    for (const size_t index : {0, 2, 4, 6})
        regions.push_back({int(index), QRectF(handles[index] - QPointF(radius, radius), QSizeF(radius * 2, radius * 2))});
    // Whole edges resize; Swift's max(0) is dead under the corners.
    for (const size_t index : {1, 5})
        regions.push_back({int(index), QRectF(rect->left() + radius, handles[index].y() - radius, rect->width() - radius * 2, radius * 2)});
    for (const size_t index : {3, 7})
        regions.push_back({int(index), QRectF(handles[index].x() - radius, rect->top() + radius, radius * 2, rect->height() - radius * 2)});
    return regions;
}

std::optional<std::pair<QPointF, QPointF>> TransformOverlay::gradientLine() const
{
    const std::optional<GradientEdit> &edit = m_session.gradientEdit();
    const std::optional<CanvasDocument> &document = m_session.document();
    if (!edit || !edit->hasLine() || !document)
        return std::nullopt;
    return std::pair(m_session.viewport.viewPoint(edit->start, document->size()), m_session.viewport.viewPoint(edit->end, document->size()));
}

void TransformOverlay::draw(QPainter &context, const QPalette &palette) const
{
    if (m_session.tool() == NavigationTool::crop)
        drawCrop(context);
    else if (const std::optional<std::pair<QPointF, QPointF>> line = gradientLine())
        drawGradientLine(context, line->first, line->second);
    else
        drawTransformHandles(context, palette);
    drawSelection(context);
    drawLassoDraft(context);
    drawSnapGuides(context, palette);
}

// The line, its ends in their colours, a radial's rim.
void TransformOverlay::drawGradientLine(QPainter &context, QPointF start, QPointF end) const
{
    context.save();
    context.setRenderHint(QPainter::Antialiasing, true);
    context.setBrush(Qt::NoBrush);
    if (m_session.gradientSettings().shape == GradientShape::radial) {
        // A faint rim where the radial reaches its end colour.
        const double radius = std::hypot(end.x() - start.x(), end.y() - start.y());
        const QRectF rim(start.x() - radius, start.y() - radius, radius * 2, radius * 2);
        for (const auto &[colour, width] : {std::pair(QColor(0, 0, 0, 128), 2.0), std::pair(QColor(255, 255, 255, 204), 1.0)}) {
            QPen pen(colour, width, Qt::SolidLine, Qt::FlatCap);
            // Four on, four off, in points: Qt counts widths.
            pen.setDashPattern({4 / width, 4 / width});
            context.setPen(pen);
            context.drawEllipse(rim);
        }
    }
    context.setPen(QPen(QColor(0, 0, 0, 179), 3));
    context.drawLine(start, end);
    context.setPen(QPen(Qt::white, 1));
    context.drawLine(start, end);
    const std::array<QColor, 2> colors = m_session.gradientColors(false);
    for (const auto &[point, colour] : {std::pair(start, colors[0]), std::pair(end, colors[1])}) {
        const QRectF rect(point.x() - 6, point.y() - 6, 12, 12);
        context.setPen(QPen(Qt::black, 1));
        context.setBrush(Qt::white);
        context.drawEllipse(rect);
        // A gray under the colour shows a clear end.
        const QRectF inner = rect.adjusted(2.5, 2.5, -2.5, -2.5);
        context.setPen(Qt::NoPen);
        context.setBrush(QColor::fromRgbF(0.75, 0.75, 0.75));
        context.drawEllipse(inner);
        context.setBrush(colour);
        context.drawEllipse(inner);
    }
    context.restore();
}

// The outside dimmed, the frame, its thirds and its grips.
void TransformOverlay::drawCrop(QPainter &context) const
{
    const std::optional<QRectF> rect = cropViewRect();
    if (!rect)
        return;
    context.save();
    context.setRenderHint(QPainter::Antialiasing, true);
    // Odd-even, a path's own rule: the frame stays clear.
    QPainterPath outside;
    outside.addRect(context.window());
    outside.addRect(*rect);
    context.fillPath(outside, QColor::fromRgbF(0, 0, 0, 0.6));
    context.setBrush(Qt::NoBrush);
    // CoreGraphics' butt caps and miter joins.
    context.setPen(QPen(Qt::white, 1, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
    context.drawRect(*rect);
    QPainterPath thirds;
    for (const int index : {1, 2}) {
        const double fraction = index / 3.0;
        thirds.moveTo(rect->left() + rect->width() * fraction, rect->top());
        thirds.lineTo(rect->left() + rect->width() * fraction, rect->bottom());
        thirds.moveTo(rect->left(), rect->top() + rect->height() * fraction);
        thirds.lineTo(rect->right(), rect->top() + rect->height() * fraction);
    }
    context.setPen(QPen(QColor::fromRgbF(1, 1, 1, 0.4), 1, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
    context.drawPath(thirds);
    context.setPen(QPen(Qt::black, 1, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
    context.setBrush(Qt::white);
    for (const QPointF point : cropHandles(*rect))
        context.drawRect(QRectF(point.x() - 4, point.y() - 4, 8, 8));
    context.restore();
}

// Marching ants: a white line under an animated black dash.
void TransformOverlay::drawSelection(QPainter &context) const
{
    const std::optional<DocumentSelection> selection = m_session.displayedSelection();
    if (!selection || selection->isEmpty() || !m_session.document())
        return;
    const QPainterPath path = documentToView().map(selection->path);
    context.save();
    context.setRenderHint(QPainter::Antialiasing, true);
    context.setBrush(Qt::NoBrush);
    context.setPen(QPen(Qt::white, 1));
    context.drawPath(path);
    // Butt caps, as CoreGraphics dashes: four on, four off.
    QPen ants(Qt::black, 1);
    ants.setCapStyle(Qt::FlatCap);
    ants.setDashPattern({4, 4});
    ants.setDashOffset(antsPhase);
    context.setPen(ants);
    context.drawPath(path);
    context.restore();
}

void TransformOverlay::drawLassoDraft(QPainter &context) const
{
    const std::optional<LassoDraft> &draft = m_session.lassoDraft();
    if (!draft || !m_session.document())
        return;
    const QTransform transform = documentToView();
    std::vector<QPointF> points;
    for (const QPointF point : draft->points)
        points.push_back(transform.map(point));
    if (draft->kind == LassoKind::polygonal && draft->cursor)
        points.push_back(transform.map(*draft->cursor));
    QPainterPath path;
    if (draft->kind == LassoKind::ellipse && points.size() == 4) {
        QPointF low = points[0], high = low;
        for (const QPointF point : points) {
            low = {std::min(low.x(), point.x()), std::min(low.y(), point.y())};
            high = {std::max(high.x(), point.x()), std::max(high.y(), point.y())};
        }
        path.addEllipse(QRectF(low, high));
    } else {
        path.moveTo(points.front());
        for (std::size_t index = 1; index < points.size(); ++index)
            path.lineTo(points[index]);
        if (draft->kind == LassoKind::rectangle)
            path.closeSubpath();
    }
    context.save();
    context.setRenderHint(QPainter::Antialiasing, true);
    context.setBrush(Qt::NoBrush);
    // Butt caps: an open outline ends at its last point.
    context.setPen(QPen(QColor(0, 0, 0, 204), 2, Qt::SolidLine, Qt::FlatCap));
    context.drawPath(path);
    context.setPen(QPen(Qt::white, 1, Qt::SolidLine, Qt::FlatCap));
    context.drawPath(path);
    if (draft->kind == LassoKind::polygonal) {
        // The first corner: click it to close the outline.
        const QRectF handle(points.front().x() - 4, points.front().y() - 4, 8, 8);
        context.fillRect(handle, Qt::white);
        context.setPen(QPen(Qt::black, 1));
        context.drawRect(handle);
    }
    context.restore();
}

void TransformOverlay::drawTransformHandles(QPainter &context, const QPalette &palette) const
{
    const std::optional<TransformOverlayGeometry> geometry = this->geometry();
    if (!geometry)
        return;
    const QColor accent = palette.color(QPalette::Highlight);
    QPainterPath path(geometry->handles[0]);
    for (const int index : {2, 4, 6})
        path.lineTo(geometry->handles[size_t(index)]);
    path.closeSubpath();
    if (geometry->showsRotation) {
        path.moveTo(geometry->handles[1]);
        path.lineTo(geometry->rotationHandle);
    }
    context.save();
    context.setRenderHint(QPainter::Antialiasing, true);
    context.setBrush(Qt::NoBrush);
    context.setPen(QPen(QColor(0, 0, 0, 179), 3));
    context.drawPath(path);
    context.setPen(QPen(accent, 1));
    context.drawPath(path);
    context.setBrush(Qt::white);
    for (const QPointF point : geometry->handles)
        context.drawRect(QRectF(point.x() - 3.5, point.y() - 3.5, 7, 7));
    if (geometry->showsRotation)
        context.drawEllipse(QRectF(geometry->rotationHandle.x() - 4, geometry->rotationHandle.y() - 4, 8, 8));
    context.restore();
}

// While a move snaps, a line along what it met.
void TransformOverlay::drawSnapGuides(QPainter &context, const QPalette &palette) const
{
    const SnapGuides &guides = m_session.snapGuides;
    const std::optional<CanvasDocument> &document = m_session.document();
    if ((guides.xs.empty() && guides.ys.empty()) || !document)
        return;
    const QSizeF size = document->size();
    const CanvasViewport &viewport = m_session.viewport;
    context.save();
    context.setPen(QPen(palette.color(QPalette::Highlight), 1));
    for (const double x : guides.xs)
        context.drawLine(viewport.viewPoint(QPointF(x, 0), size), viewport.viewPoint(QPointF(x, size.height()), size));
    for (const double y : guides.ys)
        context.drawLine(viewport.viewPoint(QPointF(0, y), size), viewport.viewPoint(QPointF(size.width(), y), size));
    context.restore();
}
