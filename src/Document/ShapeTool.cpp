#include "Document/EditorSession.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include <QPainter>
#include <QPainterPathStroker>
#include <cmath>
#include <numbers>
#include <stdexcept>

QString rawValue(ShapeKind kind)
{
    switch (kind) {
    case ShapeKind::rectangle:
        return QStringLiteral("Rectangle");
    case ShapeKind::ellipse:
        return QStringLiteral("Ellipse");
    case ShapeKind::line:
        return QStringLiteral("Line");
    }
    throw std::logic_error("unknown shape kind");
}

std::optional<ShapeKind> shapeKind(const QString &text)
{
    for (const ShapeKind kind : allShapeKinds) {
        if (rawValue(kind) == text)
            return kind;
    }
    return std::nullopt;
}

QPainterPath path(ShapeKind kind, const QRectF &rect, double cornerRadius)
{
    QPainterPath made;
    if (kind == ShapeKind::ellipse) {
        made.addEllipse(rect);
        return made;
    }
    // Below zero, like zero, keeps the corners square.
    const double radius = std::min({cornerRadius, rect.width() / 2, rect.height() / 2});
    if (radius > 0)
        made.addRoundedRect(rect, radius, radius);
    else
        made.addRect(rect);
    return made;
}

QPainterPath strokedLine(QPointF from, QPointF to, double width)
{
    QPainterPath outline;
    if (from == to) {
        // CoreGraphics caps a zero-length line as a dot.
        outline.addEllipse(from, width / 2, width / 2);
        return outline;
    }
    QPainterPath segment(from);
    segment.lineTo(to);
    QPainterPathStroker stroker;
    stroker.setWidth(width);
    stroker.setCapStyle(Qt::RoundCap);
    return stroker.createStroke(segment);
}

std::optional<LayerShape> LayerShape::loaded(const std::optional<LayerShapeStyle> &style, const std::optional<ImportedImage> &image)
{
    if (!style || !image)
        return std::nullopt;
    return LayerShape{*style, image->identity()};
}

std::optional<LayerShape> ImageLayer::liveShape() const
{
    if (!shape || !asset || asset->identity() != shape->image)
        return std::nullopt;
    return shape;
}

void EditorSession::setShapeKind(ShapeKind kind)
{
    m_shapeKind = kind;
    notify();
}

void EditorSession::setShapeCornerRadius(double radius)
{
    m_shapeCornerRadius = radius;
    notify();
}

void EditorSession::setShapeLineWidth(double width)
{
    m_shapeLineWidth = width;
    notify();
}

void EditorSession::beginShape(QPointF point)
{
    if (m_tool != NavigationTool::shape || !canEditLayers() || !std::isfinite(point.x()) || !std::isfinite(point.y()))
        return;
    const QPointF anchor(std::round(point.x()), std::round(point.y()));
    m_shapeDraft = ShapeDraft{.kind = m_shapeKind, .anchor = anchor, .rect = QRectF(anchor, QSizeF(0, 0)),
                              .cornerRadius = m_shapeKind == ShapeKind::rectangle ? m_shapeCornerRadius : 0};
    notify();
}

std::optional<std::pair<QPointF, QPointF>> EditorSession::shapeLineEnds() const
{
    // Only a line's drag sets an end.
    if (!m_shapeDraft || !m_shapeDraft->end)
        return std::nullopt;
    return std::pair(m_shapeDraft->anchor, *m_shapeDraft->end);
}

void EditorSession::dragShape(QPointF point, bool square, bool fromCenter)
{
    if (!m_shapeDraft || !std::isfinite(point.x()) || !std::isfinite(point.y()))
        return;
    ShapeDraft &draft = *m_shapeDraft;
    if (draft.kind == ShapeKind::line && square) {
        // Shift snaps a line to eighths of a turn.
        const double dx = point.x() - draft.anchor.x(), dy = point.y() - draft.anchor.y();
        const double angle = std::round(std::atan2(dy, dx) / (std::numbers::pi / 4)) * (std::numbers::pi / 4);
        const double length = std::hypot(dx, dy);
        const QPointF snapped(draft.anchor.x() + std::cos(angle) * length, draft.anchor.y() + std::sin(angle) * length);
        draft.end = snapped;
        draft.rect = DragBox::rect(draft.anchor, snapped, false, fromCenter);
    } else {
        if (draft.kind == ShapeKind::line)
            draft.end = point;
        draft.rect = DragBox::rect(draft.anchor, point, square, fromCenter);
    }
    notify();
}

void EditorSession::cancelShape()
{
    if (!m_shapeDraft)
        return;
    m_shapeDraft = std::nullopt;
    notify();
}

void EditorSession::toggleShapeKind()
{
    cancelShape();
    setShapeKind(allShapeKinds[(size_t(m_shapeKind) + 1) % allShapeKinds.size()]);
}

void EditorSession::finishShape()
{
    if (!m_shapeDraft)
        return;
    const ShapeDraft draft = *std::exchange(m_shapeDraft, std::nullopt);
    notify();
    QRectF rect = draft.rect;
    const double thickness = m_shapeLineWidth;
    // A line's box holds its two points and its thickness.
    std::optional<std::pair<QPointF, QPointF>> ends;
    if (draft.kind == ShapeKind::line) {
        const QPointF from = draft.anchor, to = draft.end.value_or(draft.anchor);
        rect = QRectF(std::min(from.x(), to.x()), std::min(from.y(), to.y()), std::abs(to.x() - from.x()), std::abs(to.y() - from.y()))
                   .adjusted(-thickness / 2, -thickness / 2, thickness / 2, thickness / 2);
        ends = std::pair(from, to);
    }
    if (!canEditLayers() || !m_document || rect.width() < 1 || rect.height() < 1)
        return;
    // Swift's Int(): each side truncated before multiplying.
    if (std::trunc(rect.width()) * std::trunc(rect.height()) > double(maxShapePixels)) {
        setBrushError(QStringLiteral("That shape is too large. A shape can cover up to 100 megapixels."));
        return;
    }
    // The ends as fractions of the box, for resizing.
    const auto unit = [&rect](QPointF point) { return QPointF((point.x() - rect.left()) / rect.width(), (point.y() - rect.top()) / rect.height()); };
    const std::optional<QPointF> start = ends ? std::optional(unit(ends->first)) : std::nullopt;
    const std::optional<QPointF> finish = ends ? std::optional(unit(ends->second)) : std::nullopt;
    const PaletteColor color = foregroundColor();
    try {
        const QImage image = shapeImage(draft.kind, rect.size(), color, draft.cornerRadius, thickness, start, finish);
        const LayerShapeStyle style{.kind = draft.kind, .red = color.red, .green = color.green, .blue = color.blue,
                                    .cornerRadius = draft.cornerRadius,
                                    .lineWidth = draft.kind == ShapeKind::line ? std::optional(thickness) : std::nullopt,
                                    .start = start, .end = finish};
        addPixelLayer(image, rect.topLeft(), nextShapeName(draft.kind), rawValue(draft.kind), false, style);
    } catch (const ExportError &error) {
        setBrushError(QString::fromUtf8(error.what()));
    }
}

QString EditorSession::nextShapeName(ShapeKind kind) const
{
    QSet<QString> names;
    if (m_document) {
        for (const ImageLayer &layer : m_document->layers)
            names.insert(layer.name);
    }
    int number = 1;
    while (names.contains(QStringLiteral("%1 %2").arg(rawValue(kind)).arg(number)))
        ++number;
    return QStringLiteral("%1 %2").arg(rawValue(kind)).arg(number);
}

void EditorSession::redrawShape(int index)
{
    ImageLayer &layer = m_document->layers[size_t(index)];
    const std::optional<LayerShape> shape = layer.liveShape();
    if (!shape)
        return;
    // A valid transform is a pixel wide at least.
    const int width = int(std::round(layer.transform.size.width())), height = int(std::round(layer.transform.size.height()));
    if ((width == layer.asset->size().width() && height == layer.asset->size().height()) || qint64(width) * height > maxShapePixels)
        return;
    QImage image, thumbnail;
    try {
        image = shapeImage(shape->style.kind, QSizeF(width, height), shape->style.color(), shape->style.cornerRadius,
                           shape->style.lineWidth.value_or(0), shape->style.start, shape->style.end);
        thumbnail = PixelAdjust::thumbnail(image);
    } catch (const ExportError &error) {
        // Swift's try? keeps the stretched pixels without a word.
        qCWarning(lcApp) << "a shape could not be drawn at its new size:" << error.what();
        return;
    }
    // The mask stays where it shows while the grid changes.
    if (layer.mask)
        layer.mask->placement = layer.maskTransform();
    layer.asset = ImportedImage(image, thumbnail, layer.asset->name);
    layer.shape = LayerShape{shape->style, layer.asset->identity()};
}

QImage EditorSession::shapeTransformPreview(const ImageLayer &layer, const LayerTransform &transform) const
{
    const std::optional<LayerShape> shape = layer.liveShape();
    if (!m_transformEdit || !shape || shape->style.kind != ShapeKind::rectangle || shape->style.cornerRadius <= 0) {
        if (!m_shapeTransformPreviewCache.empty() && !m_transformEdit)
            m_shapeTransformPreviewCache.clear();
        return QImage();
    }
    const QSizeF size = transform.size;
    const QSize pixels = layer.asset->size();
    if (size.width() < 1 || size.height() < 1
        || (std::abs(size.width() - pixels.width()) < 0.5 && std::abs(size.height() - pixels.height()) < 0.5))
        return QImage();
    const double factor = std::min(1.0, 2048 / std::max(size.width(), size.height()));
    const QSizeF drawn(std::max(1.0, std::round(size.width() * factor)), std::max(1.0, std::round(size.height() * factor)));
    if (m_shapeTransformPreviewCache.contains(layer.id) && m_shapeTransformPreviewCache.at(layer.id).size == drawn)
        return m_shapeTransformPreviewCache.at(layer.id).image;
    QImage image;
    try {
        image = shapeImage(ShapeKind::rectangle, drawn, shape->style.color(), shape->style.cornerRadius * factor);
    } catch (const ExportError &error) {
        qCWarning(lcRendering) << "no rounded preview for a resized shape:" << error.what();
        return QImage();
    }
    m_shapeTransformPreviewCache.insert_or_assign(layer.id, ShapePreview{drawn, image});
    return image;
}

QImage EditorSession::shapeImage(ShapeKind kind, QSizeF size, const PaletteColor &color, double cornerRadius, double lineWidth,
                                 std::optional<QPointF> start, std::optional<QPointF> end)
{
    QImage context = BrushRaster::context(int(size.width()), int(size.height()), false);
    const QRectF bounds(QPointF(0, 0), size);
    QPainter painter(&context);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (kind != ShapeKind::line) {
        painter.fillPath(path(kind, bounds, cornerRadius), color.color());
        painter.end();
        return context;
    }
    const double thickness = std::max(1.0, lineWidth);
    // Lines saved without ends ran corner to corner, inset.
    const double dx = std::min(thickness, size.width()) / 2, dy = std::min(thickness, size.height()) / 2;
    const QRectF inset = bounds.adjusted(dx, dy, -dx, -dy);
    const QPointF from = start ? QPointF(start->x() * size.width(), start->y() * size.height()) : inset.topLeft();
    const QPointF to = end ? QPointF(end->x() * size.width(), end->y() * size.height()) : inset.bottomRight();
    // An outline, not a pen: Qt thins a one-pixel pen.
    painter.fillPath(strokedLine(from, to, thickness), color.color());
    painter.end();
    return context;
}
