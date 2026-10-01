#include "Document/ProjectWorkspace.h"
#include "IO/ProjectStore.h"
#include "Rendering/EditorCanvas.h"
#include "UI/NativeLayerList.h"
#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <cmath>

const QString NativeLayerList::maskType = QStringLiteral("com.compositor.layer-mask");
const QString NativeLayerList::sourceType = QStringLiteral("com.compositor.layer-list");

namespace {
// A glyph drawn black over a white halo.
void drawHaloed(QPainter &painter, const QPainterPath &path, double width)
{
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::white, width + 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(path);
    painter.setPen(QPen(Qt::black, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(path);
}
}

void LayerColumn::paintEvent(QPaintEvent *)
{
    if (!indicator)
        return;
    QPainter painter(this);
    QColor accent = palette().color(QPalette::Highlight);
    if (!indicatorFills) {
        painter.fillRect(*indicator, accent);
        return;
    }
    accent.setAlphaF(0.3);
    painter.fillRect(*indicator, accent);
    painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
    painter.drawRect(indicator->adjusted(1, 1, -1, -1));
}

std::unique_ptr<QMimeData> NativeLayerList::dragData(const LayerCell &cell) const
{
    // The pressed row, with the selection it belongs to.
    const QSet<QUuid> selected = m_session.selectedLayerIDs();
    const bool whole = selected.contains(cell.layerID());
    QStringList lines;
    for (const ImageLayer &row : m_rows) {
        if (row.id == cell.layerID() || (whole && selected.contains(row.id)))
            lines << uuidString(row.id);
    }
    auto data = std::make_unique<QMimeData>();
    data->setData(ProjectWorkspace::layerType, lines.join(QLatin1Char('\n')).toUtf8());
    data->setData(sourceType, m_dragToken.toUtf8());
    return data;
}

// Alt offers a copy alone; plain drags move or copy.
void NativeLayerList::startDrag(LayerCell &cell, Qt::KeyboardModifiers modifiers)
{
    if (!m_session.canEditLayers())
        return;
    const bool copying = modifiers.testFlag(Qt::AltModifier) && !modifiers.testFlag(Qt::ControlModifier);
    auto *drag = new QDrag(this);
    drag->setMimeData(dragData(cell).release());
    drag->setPixmap(cell.grab());
    if (copying)
        drag->setDragCursor(CanvasView::duplicateCursor(devicePixelRatio()).pixmap(), Qt::CopyAction);
    drag->exec(copying ? Qt::CopyAction : Qt::MoveAction | Qt::CopyAction, copying ? Qt::CopyAction : Qt::MoveAction);
}

void NativeLayerList::startMaskDrag(LayerThumbnailButton &thumbnail)
{
    auto *data = new QMimeData;
    data->setData(maskType, uuidString(thumbnail.layerID).toUtf8());
    auto *drag = new QDrag(this);
    drag->setMimeData(data);
    drag->setPixmap(thumbnail.grab());
    drag->exec(Qt::CopyAction);
}

// Dragged rows in order, folder contents left out; strangers none.
std::vector<QUuid> NativeLayerList::draggedLayers(const QMimeData &data) const
{
    const std::vector<QUuid> dropped = ProjectWorkspace::layerIDs(data);
    const QSet<QUuid> dragged(dropped.begin(), dropped.end());
    QSet<QUuid> carried;
    for (const QUuid &id : dragged)
        carried.unite(m_session.descendantIDs(id));
    std::vector<QUuid> result;
    for (const ImageLayer &row : m_rows) {
        if (dragged.contains(row.id) && !carried.contains(row.id))
            result.push_back(row.id);
    }
    return result;
}

std::optional<LayerDropTarget> NativeLayerList::dropTarget(const QMimeData &data, Qt::DropActions offered, QPoint listPoint) const
{
    if (data.hasFormat(effectType)) {
        // An effect lands on whichever row is under the pointer.
        const int row = rowAt(listPoint);
        const std::optional<LayerEffectSelection> source = draggedEffect(data);
        if (row < 0 || !source || !m_session.canCopyEffect(source->kind, source->layerID, m_rows[row].id))
            return std::nullopt;
        return LayerDropTarget{.row = row, .onRow = true, .atBottom = false, .copying = true};
    }
    if (data.hasFormat(maskType)) {
        // A mask lands on whichever row is under the pointer.
        const int row = rowAt(listPoint);
        const std::optional<QUuid> source = ProjectWorkspace::layerID(QString::fromUtf8(data.data(maskType)));
        if (row < 0 || !source || !m_session.canCopyMask(*source, m_rows[row].id))
            return std::nullopt;
        return LayerDropTarget{.row = row, .onRow = true, .atBottom = false, .copying = true};
    }
    // Only this list's rows; canPlaceLayer covers canEditLayers.
    if (!data.hasFormat(ProjectWorkspace::layerType) || QString::fromUtf8(data.data(sourceType)) != m_dragToken)
        return std::nullopt;
    const std::vector<QUuid> ids = draggedLayers(data);
    if (ids.empty())
        return std::nullopt;
    // Swift duplicates when the source offers a copy alone (Alt).
    const bool copying = offered == Qt::CopyAction;
    const QPoint inColumn = m_column->mapFrom(viewport(), viewport()->mapFrom(this, listPoint));
    int row = int(m_rows.size());
    bool onRow = false;
    for (size_t each = 0; each < m_cells.size(); ++each) {
        const QRect box = m_cells[each]->geometry();
        if (inColumn.y() < box.top() - 1) {
            row = int(each);
            break;
        }
        if (inColumn.y() > box.bottom() + 1)
            continue;
        // A folder's middle half takes the drop in.
        const bool middle = std::abs(inColumn.y() - box.center().y()) <= box.height() / 4;
        onRow = m_rows[each].isGroup && middle;
        row = onRow || inColumn.y() < box.center().y() ? int(each) : int(each) + 1;
        break;
    }
    const std::optional<QUuid> parent = onRow ? std::optional(m_rows[row].id) : row < int(m_rows.size()) ? m_rows[row].parentID : std::nullopt;
    for (const QUuid &id : ids) {
        if (!m_session.canPlaceLayer(id, parent))
            return std::nullopt;
    }
    return LayerDropTarget{.row = row, .onRow = onRow, .atBottom = row == int(m_rows.size()), .copying = copying};
}

bool NativeLayerList::acceptDrop(const QMimeData &data, Qt::DropActions offered, QPoint listPoint)
{
    const std::optional<LayerDropTarget> target = dropTarget(data, offered, listPoint);
    if (!target)
        return false;
    if (data.hasFormat(effectType)) {
        const LayerEffectSelection source = draggedEffect(data).value();
        m_session.copyEffect(source.kind, source.layerID, m_rows[target->row].id);
        return true;
    }
    if (data.hasFormat(maskType)) {
        m_session.copyMask(ProjectWorkspace::layerID(QString::fromUtf8(data.data(maskType))).value(), m_rows[target->row].id);
        return true;
    }
    const std::vector<QUuid> ids = draggedLayers(data);
    // Where the drop lands is worked out once.
    const std::optional<QUuid> parent = target->onRow ? std::optional(m_rows[target->row].id)
        : target->atBottom                             ? std::nullopt
                                                       : m_rows[target->row].parentID;
    const std::optional<QUuid> above = target->onRow || target->atBottom ? std::nullopt : std::optional(m_rows[target->row].id);
    // Above a row the last lands nearest; in folders, first.
    std::vector<QUuid> order = ids;
    if (target->onRow)
        std::reverse(order.begin(), order.end());
    const bool several = ids.size() > 1;
    m_session.beginEdit(target->copying ? (several ? QStringLiteral("Duplicate Layers") : QStringLiteral("Duplicate Layer"))
                                        : (several ? QStringLiteral("Move Layers") : QStringLiteral("Move Layer")));
    bool placed = false;
    for (const QUuid &id : order) {
        const bool done = target->copying ? m_session.duplicateLayer(id, parent, above, target->atBottom)
                                          : m_session.placeLayer(id, parent, above, target->atBottom);
        placed = done || placed;
    }
    // The moved layers stay selected, to drag on together.
    if (placed && !target->copying)
        m_session.selectLayers(QSet<QUuid>(ids.begin(), ids.end()), ids.front());
    m_session.endEdit();
    return placed;
}

void NativeLayerList::showIndicator(const std::optional<LayerDropTarget> &target)
{
    m_column->indicator = std::nullopt;
    if (target && target->onRow) {
        m_column->indicator = m_cells[size_t(target->row)]->geometry();
        m_column->indicatorFills = true;
    } else if (target) {
        const int y = target->atBottom ? m_cells.back()->geometry().bottom() + 1 : m_cells[size_t(target->row)]->geometry().top() - 1;
        m_column->indicator = QRect(0, y - 1, m_column->width(), 2);
        m_column->indicatorFills = false;
    }
    m_column->update();
}

// The list moves, or copies what Alt offers alone.
void NativeLayerList::dragEnterEvent(QDragEnterEvent *event)
{
    const std::optional<LayerDropTarget> target = dropTarget(*event->mimeData(), event->possibleActions(), viewport()->mapToParent(event->position().toPoint()));
    showIndicator(target);
    if (!target)
        return;
    event->setDropAction(target->copying ? Qt::CopyAction : Qt::MoveAction);
    event->accept();
}

void NativeLayerList::dragMoveEvent(QDragMoveEvent *event)
{
    // Near an edge the list scrolls, as the table does.
    const QPoint inViewport = event->position().toPoint();
    m_dragPoint = inViewport;
    m_dragData = event->mimeData();
    m_dragActions = event->possibleActions();
    autoscroll(inViewport, *event->mimeData(), event->possibleActions());
    const std::optional<LayerDropTarget> target = dropTarget(*event->mimeData(), event->possibleActions(), viewport()->mapToParent(inViewport));
    if (!target) {
        event->ignore();
        return;
    }
    event->setDropAction(target->copying ? Qt::CopyAction : Qt::MoveAction);
    event->accept();
}

void NativeLayerList::autoscroll(QPoint inViewport, const QMimeData &data, Qt::DropActions offered)
{
    const int margin = 16;
    const int overshoot = inViewport.y() < margin ? inViewport.y() - margin : std::max(0, inViewport.y() - (viewport()->height() - margin));
    if (overshoot != 0)
        verticalScrollBar()->setValue(verticalScrollBar()->value() + overshoot);
    // Held at the edge, the timer scrolls on; elsewhere, rests.
    if (overshoot != 0 && !m_edgeScroll.isActive())
        m_edgeScroll.start();
    else if (overshoot == 0)
        m_edgeScroll.stop();
    showIndicator(dropTarget(data, offered, viewport()->mapToParent(inViewport)));
}

void NativeLayerList::dragLeaveEvent(QDragLeaveEvent *)
{
    m_edgeScroll.stop();
    m_dragData = nullptr;
    showIndicator(std::nullopt);
}

void NativeLayerList::dropEvent(QDropEvent *event)
{
    m_edgeScroll.stop();
    m_dragData = nullptr;
    showIndicator(std::nullopt);
    const bool copying = event->possibleActions() == Qt::CopyAction;
    if (!acceptDrop(*event->mimeData(), event->possibleActions(), viewport()->mapToParent(event->position().toPoint())))
        return;
    event->setDropAction(copying ? Qt::CopyAction : Qt::MoveAction);
    event->accept();
}

// Photoshop's create and release clipping pointers, haloed.
QCursor NativeLayerList::clippingCursor(bool releasing, double ratio)
{
    QPixmap pixmap(QSize(30, 28) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // An arrow turning down and right, at the left.
    QPainterPath arrow(QPointF(4, 3));
    arrow.lineTo(QPointF(4, 12));
    arrow.lineTo(QPointF(14, 12));
    arrow.moveTo(QPointF(10.5, 8.5));
    arrow.lineTo(QPointF(14, 12));
    arrow.lineTo(QPointF(10.5, 15.5));
    drawHaloed(painter, arrow, 1.8);
    // A box with a badge: plus creates, minus releases.
    QPainterPath box;
    box.addRoundedRect(QRectF(12, 11, 15, 12), 2, 2);
    drawHaloed(painter, box, 1.6);
    QPainterPath badge;
    badge.addEllipse(QPointF(25, 12), 4.5, 4.5);
    painter.setPen(QPen(Qt::white, 1.4));
    painter.setBrush(Qt::black);
    painter.drawPath(badge);
    painter.setPen(QPen(Qt::white, 1.4, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(22.5, 12), QPointF(27.5, 12));
    if (!releasing)
        painter.drawLine(QPointF(25, 9.5), QPointF(25, 14.5));
    return QCursor(pixmap, 3, 3);
}

// Swift's eye.fill: an almond, a ring round the pupil.
QCursor NativeLayerList::showMaskCursor(double ratio)
{
    const QCursor base = CanvasView::duplicateCursor(ratio);
    const QPointF hotSpot = base.hotSpot();
    const QRectF eye(hotSpot + QPointF(15, 18), QSizeF(7.5, 5.5));
    const QSizeF baseSize = base.pixmap().deviceIndependentSize();
    const QSize size(int(std::ceil(std::max(baseSize.width(), eye.right() + 2))), int(std::ceil(std::max(baseSize.height(), eye.bottom() + 2))));
    QPixmap pixmap(size * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath shape;
    shape.setFillRule(Qt::OddEvenFill);
    shape.moveTo(eye.left(), eye.center().y());
    shape.quadTo(eye.center().x(), eye.top() - eye.height() / 2, eye.right(), eye.center().y());
    shape.quadTo(eye.center().x(), eye.bottom() + eye.height() / 2, eye.left(), eye.center().y());
    shape.addEllipse(eye.center(), 1.9, 1.9);
    shape.addEllipse(eye.center(), 1.1, 1.1);
    // The eye first, haloed, so the arrows sit in front.
    for (int step = 0; step < 16; ++step) {
        const double angle = step * M_PI / 8;
        painter.fillPath(shape.translated(std::cos(angle), std::sin(angle)), Qt::white);
    }
    painter.fillPath(shape, Qt::black);
    painter.drawPixmap(QPointF(0, 0), base.pixmap());
    painter.end();
    return QCursor(pixmap, int(hotSpot.x()), int(hotSpot.y()));
}

QCursor NativeLayerList::cursorFor(QPoint listPoint, Qt::KeyboardModifiers modifiers) const
{
    const int row = rowAt(listPoint);
    if (row < 0 || !(modifiers & (Qt::AltModifier | Qt::ControlModifier)))
        return QCursor(Qt::ArrowCursor);
    const ImageLayer &layer = m_rows[size_t(row)];
    LayerCell &cell = *m_cells[size_t(row)];
    const QPoint inCell = cell.mapFrom(this, listPoint);
    const double ratio = devicePixelRatio();
    // Ctrl over a thumbnail loads it as a selection.
    if (modifiers.testFlag(Qt::ControlModifier)) {
        for (const LayerThumbnailButton *thumbnail : {&cell.thumbnail(), &cell.maskThumbnail()}) {
            if (thumbnail->isVisible() && thumbnail->isEnabled() && thumbnail->geometry().contains(inCell))
                return CanvasView::loadSelectionCursor(ratio);
        }
        return QCursor(Qt::ArrowCursor);
    }
    const bool editable = m_session.canEditLayers();
    // A mask shows alone, the row duplicates, the strip clips.
    if (cell.maskThumbnail().isVisible() && cell.maskThumbnail().geometry().contains(inCell))
        return showMaskCursor(ratio);
    if (!isClippingZone(cell, inCell))
        return editable ? CanvasView::duplicateCursor(ratio) : QCursor(Qt::ArrowCursor);
    if (!m_session.canToggleClippingMask(layer.id))
        return QCursor(Qt::ArrowCursor);
    return clippingCursor(layer.maskSourceID.has_value(), ratio);
}

// A modifier changed: the cursor is worked out again.
void NativeLayerList::refreshCursor()
{
    viewport()->setCursor(m_hover ? cursorFor(*m_hover, QApplication::keyboardModifiers()) : QCursor(Qt::ArrowCursor));
}

void NativeLayerList::showEvent(QShowEvent *event)
{
    QScrollArea::showEvent(event);
    // Alt pressed anywhere changes the cursor over the rows.
    qApp->installEventFilter(this);
}

void NativeLayerList::hideEvent(QHideEvent *event)
{
    qApp->removeEventFilter(this);
    QScrollArea::hideEvent(event);
}

// Alt, Ctrl or hover over rows sets the cursor.
bool NativeLayerList::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Alt || key == Qt::Key_Control)
            refreshCursor();
    } else if (event->type() == QEvent::MouseMove && watched->isWidgetType() && isAncestorOf(static_cast<QWidget *>(watched))) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        m_hover = mapFromGlobal(mouse->globalPosition().toPoint());
        viewport()->setCursor(cursorFor(*m_hover, mouse->modifiers()));
    }
    return QScrollArea::eventFilter(watched, event);
}

bool NativeLayerList::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::Leave) {
        m_hover = std::nullopt;
        viewport()->setCursor(QCursor(Qt::ArrowCursor));
    }
    return QScrollArea::viewportEvent(event);
}
