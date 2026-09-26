#include "Document/Selection.h"
#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "Document/PixelAdjust.h"
#include <QPainterPathStroker>
#include <cmath>
#include <stdexcept>

namespace {
QPainterPath rectPath(const QRectF &rect)
{
    QPainterPath path;
    path.addRect(rect);
    return path;
}

QPainterPath canvasPath(const CanvasDocument &document)
{
    return rectPath(QRectF(QPointF(0, 0), document.size()));
}

void fill(QPainter &painter, const QPainterPath &path, bool antialiased)
{
    painter.setRenderHint(QPainter::Antialiasing, antialiased);
    painter.fillPath(path, Qt::white);
}
}

QString rawValue(LassoKind kind)
{
    switch (kind) {
    case LassoKind::freehand:
        return QStringLiteral("Freehand");
    case LassoKind::polygonal:
        return QStringLiteral("Polygonal");
    case LassoKind::rectangle:
        return QStringLiteral("Rectangle");
    case LassoKind::ellipse:
        return QStringLiteral("Ellipse");
    }
    throw std::logic_error("unknown lasso kind");
}

QString rawValue(SelectionAmountOperation operation)
{
    switch (operation) {
    case SelectionAmountOperation::expand:
        return QStringLiteral("Expand");
    case SelectionAmountOperation::contract:
        return QStringLiteral("Contract");
    case SelectionAmountOperation::feather:
        return QStringLiteral("Feather");
    }
    throw std::logic_error("unknown selection amount operation");
}

QString rawValue(SelectionMode mode)
{
    switch (mode) {
    case SelectionMode::replace:
        return QStringLiteral("New");
    case SelectionMode::add:
        return QStringLiteral("Add");
    case SelectionMode::subtract:
        return QStringLiteral("Subtract");
    }
    throw std::logic_error("unknown selection mode");
}

bool DocumentSelection::isEmpty() const
{
    return path.isEmpty() || path.boundingRect().isEmpty();
}

QImage DocumentSelection::coverage(int width, int height) const
{
    QImage image = BrushRaster::context(width, height, true);
    {
        QPainter painter(&image);
        fill(painter, path, antialiased || feather > 0);
    }
    if (!(feather > 0))
        return image;
    // A feathered edge fades either side of the outline.
    return PixelAdjust::gaussianBlur(image, feather / 2, true);
}

QRectF DocumentSelection::coverageBounds() const
{
    // Four standard deviations keep the falloff past the outline.
    const double reach = std::ceil(feather * 2);
    return path.boundingRect().adjusted(-reach, -reach, reach, reach);
}

SelectionClip DocumentSelection::clip(QSizeF canvas) const
{
    const QRect region = coverageBounds().adjusted(-1, -1, 1, 1).toAlignedRect()
        .intersected(QRect(0, 0, int(canvas.width()), int(canvas.height())));
    if (isEmpty() || region.isEmpty())
        return SelectionClip{QRectF(), std::nullopt};
    const DocumentSelection local{path.translated(-region.x(), -region.y()), antialiased, feather};
    return SelectionClip{QRectF(region), local.coverage(region.width(), region.height())};
}

QRectF DragBox::rect(QPointF anchor, QPointF point, bool square, bool fromCenter)
{
    double dx = std::round(point.x()) - anchor.x(), dy = std::round(point.y()) - anchor.y();
    if (square) {
        const double side = std::max(std::abs(dx), std::abs(dy));
        dx = dx < 0 ? -side : side;
        dy = dy < 0 ? -side : side;
    }
    if (fromCenter)
        return QRectF(anchor.x() - std::abs(dx), anchor.y() - std::abs(dy), std::abs(dx) * 2, std::abs(dy) * 2);
    return QRectF(std::min(anchor.x(), anchor.x() + dx), std::min(anchor.y(), anchor.y() + dy), std::abs(dx), std::abs(dy));
}

std::optional<DocumentSelection> EditorSession::selection() const
{
    return m_document ? m_document->selection : std::nullopt;
}

bool EditorSession::canEditSelection() const
{
    return canEditLayers();
}

SelectionMode EditorSession::selectionMode(bool shift, bool option) const
{
    return option ? SelectionMode::subtract : shift ? SelectionMode::add : m_selectionModeChoice;
}

SelectionMode EditorSession::lassoCursorMode(bool shift, bool option) const
{
    return m_lassoDraft ? m_lassoDraft->mode : selectionMode(shift, option);
}

SelectionMode EditorSession::displayedSelectionMode() const
{
    return m_lassoDraft ? m_lassoDraft->mode : m_heldSelectionMode.value_or(m_selectionModeChoice);
}

void EditorSession::updateHeldSelectionKeys(bool shift, bool option)
{
    const std::optional<SelectionMode> held = option ? std::optional(SelectionMode::subtract) : shift ? std::optional(SelectionMode::add) : std::nullopt;
    if (m_heldSelectionMode == held)
        return;
    m_heldSelectionMode = held;
    notify();
}

void EditorSession::setLassoKind(LassoKind kind)
{
    m_lassoKind = kind;
    notify();
}

void EditorSession::setMarqueeKind(LassoKind kind)
{
    m_marqueeKind = kind;
    notify();
}

void EditorSession::setSelectionModeChoice(SelectionMode mode)
{
    m_selectionModeChoice = mode;
    notify();
}

void EditorSession::setSelectionAntialiased(bool antialiased)
{
    m_selectionAntialiased = antialiased;
    notify();
}

void EditorSession::setSelectionExpandAmount(int amount)
{
    m_selectionExpandAmount = amount;
    notify();
}

void EditorSession::setSelectionContractAmount(int amount)
{
    m_selectionContractAmount = amount;
    notify();
}

void EditorSession::setSelectionFeatherAmount(int amount)
{
    m_selectionFeatherAmount = amount;
    notify();
}

void EditorSession::setSelectionAmountOperation(std::optional<SelectionAmountOperation> operation)
{
    m_selectionAmountOperation = operation;
    resumeFileRequests();
    notify();
}

void EditorSession::beginLasso(QPointF point, SelectionMode mode)
{
    // The Magic Wand selects with a click, never an outline.
    if (!isSelectionTool(m_tool) || m_tool == NavigationTool::wand || !canEditSelection() || m_selectionMoveOrigin)
        return;
    if (m_tool == NavigationTool::marquee) {
        const QPointF anchor(std::round(point.x()), std::round(point.y()));
        m_lassoDraft = LassoDraft{{anchor}, std::nullopt, mode, m_marqueeKind, anchor};
    } else {
        m_lassoDraft = LassoDraft{{point}, std::nullopt, mode, m_lassoKind, std::nullopt};
    }
    notify();
}

void EditorSession::dragMarquee(QPointF point, bool square, bool fromCenter)
{
    if (!m_lassoDraft || (m_lassoDraft->kind != LassoKind::rectangle && m_lassoDraft->kind != LassoKind::ellipse) || !m_lassoDraft->anchor
        || !std::isfinite(point.x()) || !std::isfinite(point.y()))
        return;
    const QRectF rect = DragBox::rect(*m_lassoDraft->anchor, point, square, fromCenter);
    m_lassoDraft->points = {rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()};
    notify();
}

void EditorSession::extendLasso(QPointF point)
{
    if (!m_lassoDraft || !std::isfinite(point.x()) || !std::isfinite(point.y()))
        return;
    // Points closer than a quarter pixel are skipped.
    const QPointF last = m_lassoDraft->points.back();
    if (std::hypot(point.x() - last.x(), point.y() - last.y()) < 0.25)
        return;
    m_lassoDraft->points.push_back(point);
    notify();
}

void EditorSession::moveLassoCursor(std::optional<QPointF> point)
{
    if (!m_lassoDraft)
        return;
    m_lassoDraft->cursor = point;
    notify();
}

void EditorSession::removeLastLassoPoint()
{
    if (!m_lassoDraft)
        return;
    m_lassoDraft->points.pop_back();
    if (m_lassoDraft->points.empty())
        m_lassoDraft = std::nullopt;
    notify();
}

void EditorSession::cancelLasso()
{
    if (!m_lassoDraft)
        return;
    m_lassoDraft = std::nullopt;
    notify();
}

// The key chooses the tool; the bar switches its kind.
void EditorSession::pressMarqueeKey()
{
    selectTool(NavigationTool::marquee);
}

void EditorSession::pressLassoKey()
{
    selectTool(NavigationTool::lasso);
}

void EditorSession::toggleMarqueeKind()
{
    cancelLasso();
    m_marqueeKind = m_marqueeKind == LassoKind::rectangle ? LassoKind::ellipse : LassoKind::rectangle;
    notify();
}

void EditorSession::toggleLassoKind()
{
    cancelLasso();
    m_lassoKind = m_lassoKind == LassoKind::freehand ? LassoKind::polygonal : LassoKind::freehand;
    notify();
}

void EditorSession::finishLasso()
{
    if (!m_lassoDraft)
        return;
    const LassoDraft draft = *std::exchange(m_lassoDraft, std::nullopt);
    QPainterPath outline;
    outline.setFillRule(Qt::WindingFill);
    if (draft.kind == LassoKind::ellipse && draft.points.size() == 4) {
        // The drag's box, whole pixels; the oval fills it.
        double left = draft.points[0].x(), top = draft.points[0].y(), right = left, bottom = top;
        for (const QPointF &point : draft.points) {
            left = std::min(left, point.x());
            right = std::max(right, point.x());
            top = std::min(top, point.y());
            bottom = std::max(bottom, point.y());
        }
        outline.addEllipse(QRectF(left, top, right - left, bottom - top));
    } else {
        outline.moveTo(draft.points.front());
        for (std::size_t index = 1; index < draft.points.size(); ++index)
            outline.lineTo(draft.points[index]);
        outline.closeSubpath();
    }
    const QRectF bounds = outline.boundingRect();
    if ((draft.points.size() < 3 && draft.kind != LassoKind::ellipse) || bounds.width() <= 0 || bounds.height() <= 0) {
        // A click that encloses nothing deselects in New mode.
        if (draft.mode == SelectionMode::replace)
            deselect();
        notify();
        return;
    }
    applySelection(outline, draft.mode,
                   draft.kind == LassoKind::freehand ? QStringLiteral("Lasso") : draft.kind == LassoKind::polygonal ? QStringLiteral("Polygonal Lasso")
                       : draft.kind == LassoKind::ellipse ? QStringLiteral("Elliptical Marquee") : QStringLiteral("Rectangular Marquee"));
    notify();
}

void EditorSession::applySelection(const QPainterPath &shape, SelectionMode mode, const QString &name)
{
    if (!m_document || !canEditSelection())
        return;
    const QPainterPath clipped = shape.intersected(canvasPath(*m_document));
    const std::optional<DocumentSelection> current = selection();
    QPainterPath result;
    switch (mode) {
    case SelectionMode::replace:
        result = clipped;
        break;
    case SelectionMode::add:
        result = current ? current->path.united(clipped) : clipped;
        break;
    case SelectionMode::subtract:
        // Nothing to subtract from: nothing changes.
        if (!current)
            return;
        result = current->path.subtracted(clipped);
        break;
    }
    setSelection(DocumentSelection{result, m_selectionAntialiased}, name);
}

void EditorSession::setSelection(std::optional<DocumentSelection> value, const QString &name)
{
    if (!m_document || !canEditSelection() || value == selection())
        return;
    beginEdit(name);
    m_document->selection = std::move(value);
    endEdit();
}

bool EditorSession::canMoveSelection(QPointF point) const
{
    const std::optional<DocumentSelection> current = selection();
    if (!current || current->isEmpty() || !canEditSelection() || m_lassoDraft)
        return false;
    return current->path.contains(point);
}

bool EditorSession::beginSelectionMove()
{
    const std::optional<DocumentSelection> current = selection();
    if (m_selectionMoveOrigin || !current || current->isEmpty() || !canEditSelection())
        return false;
    beginEdit(QStringLiteral("Move Selection"));
    m_selectionMoveOrigin = current;
    return true;
}

void EditorSession::moveSelection(QSizeF offset)
{
    // The document may have gone mid-drag: nothing to move.
    if (!m_selectionMoveOrigin || !m_document)
        return;
    // Whole pixels, so edges stay crisp; not re-clipped.
    m_document->selection = DocumentSelection{m_selectionMoveOrigin->path.translated(std::round(offset.width()), std::round(offset.height())),
                                              m_selectionMoveOrigin->antialiased, m_selectionMoveOrigin->feather};
    notify();
}

void EditorSession::endSelectionMove()
{
    if (!m_selectionMoveOrigin)
        return;
    m_selectionMoveOrigin = std::nullopt;
    endEdit();
}

void EditorSession::nudgeSelection(double dx, double dy)
{
    if (!beginSelectionMove())
        return;
    moveSelection(QSizeF(dx, dy));
    endSelectionMove();
}

bool EditorSession::canModifySelection() const
{
    const std::optional<DocumentSelection> current = selection();
    return current && !current->isEmpty() && canEditSelection() && !m_lassoDraft;
}

void EditorSession::expandSelection(int amount)
{
    resizeSelection(amount, QStringLiteral("Expand Selection"));
}

void EditorSession::contractSelection(int amount)
{
    resizeSelection(-amount, QStringLiteral("Contract Selection"));
}

void EditorSession::promptSelectionAmount(SelectionAmountOperation operation)
{
    if (!canModifySelection())
        return;
    setSelectionAmountOperation(operation);
}

void EditorSession::confirmSelectionAmount(int amount)
{
    const std::optional<SelectionAmountOperation> operation = m_selectionAmountOperation;
    if (!operation || amount < 1 || amount > (*operation == SelectionAmountOperation::feather ? 250 : 500))
        return;
    setSelectionAmountOperation(std::nullopt);
    switch (*operation) {
    case SelectionAmountOperation::expand:
        setSelectionExpandAmount(amount);
        expandSelection(amount);
        break;
    case SelectionAmountOperation::contract:
        setSelectionContractAmount(amount);
        contractSelection(amount);
        break;
    case SelectionAmountOperation::feather:
        setSelectionFeatherAmount(amount);
        featherSelection(amount);
        break;
    }
}

void EditorSession::featherSelection(int amount)
{
    const std::optional<DocumentSelection> current = selection();
    if (!canModifySelection() || !current || amount <= 0)
        return;
    // Two soft edges spread less than their sum.
    const double softened = std::sqrt(current->feather * current->feather + double(amount) * amount);
    setSelection(DocumentSelection{current->path, current->antialiased, std::min(250.0, softened)}, QStringLiteral("Feather Selection"));
}

void EditorSession::resizeSelection(double delta, const QString &name)
{
    const std::optional<DocumentSelection> current = selection();
    if (!m_document || !current || !canModifySelection() || delta == 0 || std::abs(delta) > 500)
        return;
    // A band `|delta|` wide on each side, added or removed.
    QPainterPathStroker stroker;
    stroker.setWidth(std::abs(delta) * 2);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    const QPainterPath band = stroker.createStroke(current->path);
    const QPainterPath result = delta > 0 ? current->path.united(band).intersected(canvasPath(*m_document)) : current->path.subtracted(band);
    setSelection(DocumentSelection{result, current->antialiased, current->feather}, name);
}

void EditorSession::selectAll()
{
    if (!m_document)
        return;
    setSelection(DocumentSelection{canvasPath(*m_document)}, QStringLiteral("Select All"));
}

void EditorSession::deselect()
{
    if (!selection())
        return;
    setSelection(std::nullopt, QStringLiteral("Deselect"));
}

void EditorSession::invertSelection()
{
    const std::optional<DocumentSelection> current = selection();
    if (!m_document || !current)
        return;
    setSelection(DocumentSelection{canvasPath(*m_document).subtracted(current->path), current->antialiased, current->feather}, QStringLiteral("Inverse"));
}
