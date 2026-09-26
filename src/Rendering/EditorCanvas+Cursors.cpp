#include "Rendering/EditorCanvas.h"
#include "UI/KeyboardShortcuts.h"
#include "Rendering/EyedropperIcon.h"
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace {
// A haloed magnifier with a plus or a minus.
QCursor zoomCursor(bool out, double ratio)
{
    QPixmap pixmap(QSize(24, 24) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    const QRectF glyph(2, 2, 20, 20);
    // The lens sits toward the top left, as the symbol's.
    const QPointF center(glyph.left() + glyph.width() * 0.41, glyph.top() + glyph.height() * 0.40);
    const double radius = glyph.width() * 0.27;
    QPainterPath shape;
    shape.addEllipse(center, radius, radius);
    const QPointF grip = center + QPointF(radius, radius) * std::sqrt(0.5);
    shape.moveTo(grip);
    shape.lineTo(glyph.bottomRight() - QPointF(1, 1));
    shape.moveTo(center - QPointF(radius * 0.55, 0));
    shape.lineTo(center + QPointF(radius * 0.55, 0));
    if (!out) {
        shape.moveTo(center - QPointF(0, radius * 0.55));
        shape.lineTo(center + QPointF(0, radius * 0.55));
    }
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::white, 4, Qt::SolidLine, Qt::RoundCap));
    painter.drawPath(shape);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawEllipse(center, radius, radius);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::black, 1.6, Qt::SolidLine, Qt::RoundCap));
    painter.drawPath(shape);
    return QCursor(pixmap, 10, 10);
}

QPixmap cursorPixmap(QSize size, double ratio)
{
    QPixmap pixmap(size * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    return pixmap;
}

// A black stroke over a white one, like the crosshair.
void strokeHaloed(QPainter &painter, const QPainterPath &path, double width)
{
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::white, width + 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(path);
    painter.setPen(QPen(Qt::black, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(path);
}

// The crosshair: two 15-point lines through the hot spot.
void drawCrosshair(QPainter &painter, QPointF hotSpot)
{
    QPainterPath cross;
    cross.moveTo(hotSpot - QPointF(7.5, 0));
    cross.lineTo(hotSpot + QPointF(7.5, 0));
    cross.moveTo(hotSpot - QPointF(0, 7.5));
    cross.lineTo(hotSpot + QPointF(0, 7.5));
    strokeHaloed(painter, cross, 1.2);
}

// A plus to add or a minus to subtract, beside.
void drawBadge(QPainter &painter, QPointF center, SelectionMode mode)
{
    if (mode == SelectionMode::replace)
        return;
    QPainterPath badge;
    badge.moveTo(center - QPointF(3, 0));
    badge.lineTo(center + QPointF(3, 0));
    if (mode == SelectionMode::add) {
        badge.moveTo(center - QPointF(0, 3));
        badge.lineTo(center + QPointF(0, 3));
    }
    strokeHaloed(painter, badge, 1.2);
}

// An icon on its 18-unit grid fitted to `box`.
void drawIcon(QPainter &painter, SelectionIcon icon, const QRectF &box, const QColor &colour)
{
    painter.save();
    painter.translate(box.topLeft());
    painter.scale(box.width() / 18, box.height() / 18);
    painter.setPen(QPen(colour, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    SelectionIcons::paint(painter, icon);
    painter.restore();
}

// Black over eight white offsets: the icon's outline.
void drawHaloedIcon(QPainter &painter, SelectionIcon icon, const QRectF &box)
{
    for (const QPointF offset : {QPointF(-1, 0), QPointF(1, 0), QPointF(0, -1), QPointF(0, 1), QPointF(-0.7, -0.7), QPointF(0.7, 0.7), QPointF(-0.7, 0.7), QPointF(0.7, -0.7)})
        drawIcon(painter, icon, box.translated(offset), Qt::white);
    drawIcon(painter, icon, box, Qt::black);
}

// Scissors on an 11-point grid: handles below, blades crossed.
QPainterPath scissorsPath(const QRectF &box)
{
    const double unit = box.width() / 11;
    const auto at = [&](double x, double y) { return box.topLeft() + QPointF(x * unit, y * unit); };
    QPainterPath path;
    path.addEllipse(at(2.8, 8.4), 2 * unit, 2 * unit);
    path.addEllipse(at(8.2, 8.4), 2 * unit, 2 * unit);
    path.moveTo(at(3.9, 6.7));
    path.lineTo(at(8.8, 0.6));
    path.moveTo(at(7.1, 6.7));
    path.lineTo(at(2.2, 0.6));
    return path;
}

// A dashed selection box, white under black, as Swift badges.
void drawSelectionBox(QPainter &painter, const QRectF &box)
{
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::white, 2.5));
    painter.drawRect(box);
    QPen dashed(Qt::black, 1);
    dashed.setCapStyle(Qt::FlatCap);
    dashed.setDashPattern({2, 1.5});
    painter.setPen(dashed);
    painter.drawRect(box);
}
}

QCursor CanvasView::moveSelectionCursor(double ratio)
{
    QPixmap pixmap = cursorPixmap(QSize(28, 32), ratio);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF tip(4, 3);
    const QPainterPath arrow = arrowPath().translated(tip);
    painter.setPen(QPen(Qt::white, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(arrow);
    painter.fillPath(arrow, Qt::black);
    drawSelectionBox(painter, QRectF(tip + QPointF(9.5, 13.5), QSizeF(8, 6)));
    return QCursor(pixmap, 4, 3);
}

QCursor CanvasView::selectionCursor(SelectionIcon icon, SelectionMode mode, double ratio)
{
    QPixmap pixmap = cursorPixmap(QSize(44, 36), ratio);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF hotSpot(8, 8);
    drawCrosshair(painter, hotSpot);
    // The icon below right of the cross, then its badge.
    const QRectF box(hotSpot + QPointF(7, 7), QSizeF(12, 12));
    drawHaloedIcon(painter, icon, box);
    drawBadge(painter, QPointF(box.right() + 5, box.center().y()), mode);
    return QCursor(pixmap, int(hotSpot.x()), int(hotSpot.y()));
}

// Swift's wand: sparkle at the hot spot, stick down right.
QCursor CanvasView::wandCursor(SelectionMode mode, double ratio)
{
    QPixmap pixmap = cursorPixmap(QSize(34, 34), ratio);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF hotSpot(7, 7);
    QPainterPath stick(hotSpot + QPointF(6, 6));
    stick.lineTo(hotSpot + QPointF(20, 20));
    QPainterPath marks;
    for (const QPointF direction : {QPointF(0, -1), QPointF(0, 1), QPointF(-1, 0), QPointF(1, 0)}) {
        marks.moveTo(hotSpot + direction * 2.5);
        marks.lineTo(hotSpot + direction * 6);
    }
    if (mode != SelectionMode::replace) {
        const QPointF center = hotSpot + QPointF(17, 5);
        marks.moveTo(center - QPointF(3, 0));
        marks.lineTo(center + QPointF(3, 0));
        if (mode == SelectionMode::add) {
            marks.moveTo(center - QPointF(0, 3));
            marks.lineTo(center + QPointF(0, 3));
        }
    }
    // White outlines first, so neither covers the other's black.
    painter.setBrush(Qt::NoBrush);
    for (const auto &[colour, stickWidth, markWidth] : {std::tuple(Qt::white, 5.0, 3.2), std::tuple(Qt::black, 2.4, 1.2)}) {
        painter.setPen(QPen(colour, stickWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(stick);
        painter.setPen(QPen(colour, markWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(marks);
    }
    return QCursor(pixmap, int(hotSpot.x()), int(hotSpot.y()));
}

QCursor CanvasView::eyedropperCursor(double ratio)
{
    QPixmap pixmap = cursorPixmap(QSize(24, 24), ratio);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Swift's box: sixteen white offsets first, then black.
    const QRectF glyph(2, 2, 20, 20);
    const double scale = glyph.width() / 18;
    const auto draw = [&painter, &glyph, scale](QPointF offset, const QColor &colour) {
        painter.save();
        painter.translate(glyph.topLeft() + offset);
        painter.scale(scale, scale);
        painter.setPen(QPen(colour, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        EyedropperIcon::paint(painter);
        painter.restore();
    };
    for (int step = 0; step < 16; ++step)
        draw(QPointF(std::cos(step * M_PI / 8), std::sin(step * M_PI / 8)) * 1.25, Qt::white);
    draw(QPointF(), Qt::black);
    // The dropper's tip is the hot spot, as Swift's.
    const QPointF tip = glyph.topLeft() + EyedropperIcon::tip() * scale;
    return QCursor(pixmap, int(std::round(tip.x())), int(std::round(tip.y())));
}

// Swift's arrow with the scissors symbol beside, haloed white.
QCursor CanvasView::movePixelsCursor(double ratio)
{
    QPixmap pixmap = cursorPixmap(QSize(36, 36), ratio);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF tip(4, 3);
    const QPainterPath arrow = arrowPath().translated(tip);
    painter.setPen(QPen(Qt::white, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(arrow);
    painter.fillPath(arrow, Qt::black);
    strokeHaloed(painter, scissorsPath(QRectF(tip + QPointF(9, 12), QSizeF(11, 11))), 1.2);
    return QCursor(pixmap, 4, 3);
}

// Ctrl-drag cuts and moves; with Alt it copies.
QCursor CanvasView::pixelDragCursor(bool duplicate, double ratio)
{
    return duplicate ? duplicateCursor(ratio) : movePixelsCursor(ratio);
}

void CanvasView::updateCursor()
{
    const NavigationTool tool = m_session.tool();
    // A transform, outline or pixel drag keeps its cursor.
    if (m_dragCursor)
        setCursor(*m_dragCursor);
    else if (m_selectionDragStart)
        setCursor(moveSelectionCursor(devicePixelRatio()));
    else if (m_pixelDragStart)
        setCursor(pixelDragCursor(m_session.pixelMove() && m_session.pixelMove()->duplicate, devicePixelRatio()));
    else if (m_lastDragPoint)
        setCursor(Qt::ClosedHandCursor);
    // Picking outranks Space and the tools, as Swift's cursor rects.
    else if (picking())
        setCursor(eyedropperCursor(devicePixelRatio()));
    else if (m_session.hueTargeting())
        setCursor(Qt::SizeHorCursor);
    else if (m_spaceHeld || tool == NavigationTool::hand)
        setCursor(Qt::OpenHandCursor);
    else if (tool == NavigationTool::crop)
        setCursor(m_hover ? cropCursor(*m_hover) : QCursor(Qt::CrossCursor));
    else if (tool == NavigationTool::zoom)
        setCursor(zoomCursor(m_optionHeld, devicePixelRatio()));
    else if (tool == NavigationTool::type)
        setCursor(m_inlineTextEditor && m_hover ? m_inlineTextEditor->cursorAt(*m_hover).value_or(QCursor(Qt::IBeamCursor)) : QCursor(Qt::IBeamCursor));
    else if (tool == NavigationTool::move)
        setCursor(m_hover ? transformCursor(*m_hover, heldModifiers()) : QCursor(Qt::ArrowCursor));
    else if (isSelectionTool(tool))
        setCursor(lassoCursor(heldModifiers(), m_hover));
    // The circle, preview and crosshair stand in; Alt picks anew.
    else if (tool == NavigationTool::cloneStamp && m_session.cloneSource() && !m_optionHeld)
        setCursor(Qt::BlankCursor);
    else if (tool == NavigationTool::idle)
        setCursor(Qt::ArrowCursor);
    else
        setCursor(Qt::CrossCursor);
}

Qt::KeyboardModifiers CanvasView::heldModifiers() const
{
    return (m_optionHeld ? Qt::AltModifier : Qt::NoModifier) | (m_controlHeld ? Qt::ControlModifier : Qt::NoModifier)
        | (m_shiftHeld ? Qt::ShiftModifier : Qt::NoModifier);
}

void CanvasView::readModifiers(Qt::KeyboardModifiers modifiers)
{
    m_optionHeld = modifiers.testFlag(Qt::AltModifier);
    m_controlHeld = modifiers.testFlag(Qt::ControlModifier);
    m_shiftHeld = modifiers.testFlag(Qt::ShiftModifier);
}

// Swift's monitors: the window's keys, and modifiers from anywhere.
bool CanvasView::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease)
        return false;
    const auto *key = static_cast<QKeyEvent *>(event);
    // Alt, Ctrl and Shift from any window, as Swift's monitor.
    if (key->key() == Qt::Key_Alt || key->key() == Qt::Key_Control || key->key() == Qt::Key_Shift) {
        if (!key->isAutoRepeat()) {
            (key->key() == Qt::Key_Alt ? m_optionHeld : key->key() == Qt::Key_Control ? m_controlHeld : m_shiftHeld) = event->type() == QEvent::KeyPress;
            modifiersChanged();
        }
        return false;
    }
    const auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget || widget->window() != window())
        return false;
    // Remapped keys stand for theirs; a moved key passes on.
    const std::unique_ptr<QKeyEvent> typed = ShortcutSettings::shared().canvasEvent(*key);
    if (!typed)
        return false;
    key = typed.get();
    // Shift-+ and Shift-− step the blend mode, except while typing.
    const bool typing = qobject_cast<QLineEdit *>(QApplication::focusWidget()) || (m_inlineTextEditor && hasFocus());
    if (event->type() != QEvent::KeyPress || typing || key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return false;
    const bool forward = key->key() == Qt::Key_Plus || key->key() == Qt::Key_Equal;
    if (key->modifiers().testFlag(Qt::ShiftModifier) && (forward || key->key() == Qt::Key_Underscore || key->key() == Qt::Key_Minus)) {
        m_session.cycleBlendMode(forward);
        return true;
    }
    // Brush brackets too; the canvas's own keys take its own.
    return QApplication::focusWidget() != this && !m_session.levels() && brushBracket(key->text());
}

// A key changed: badge, box and cursor follow at once.
void CanvasView::modifiersChanged()
{
    const std::optional<LassoDraft> &draft = m_session.lassoDraft();
    if (m_marqueeDragPixel && draft && (draft->kind == LassoKind::rectangle || draft->kind == LassoKind::ellipse))
        dragMarqueeDraft(*m_marqueeDragPixel, heldModifiers());
    // So does a crop drag, as Alt and Ctrl change.
    if (m_cropDrag && m_session.tool() == NavigationTool::crop && m_session.document())
        dragCrop(m_hover.value(), heldModifiers());
    // As Swift's monitor: Alt may start or end picking.
    synchronizeDisplay();
    m_session.updateHeldSelectionKeys(m_shiftHeld, m_optionHeld);
    updateCursor();
    updateBrushCursor();
}

// The pointer arrow as a path, tip at the origin.
QPainterPath CanvasView::arrowPath()
{
    QPainterPath arrow(QPointF(0, 0));
    for (const QPointF point : {QPointF(0, 16.5), QPointF(3.9, 12.8), QPointF(6.6, 19), QPointF(9.2, 17.9), QPointF(6.6, 11.8), QPointF(11.8, 11.8)})
        arrow.lineTo(point);
    arrow.closeSubpath();
    return arrow;
}

// Arrows back to front, each outlined under its fill.
QCursor CanvasView::duplicateCursor(double ratio)
{
    QPixmap pixmap(QSize(28, 32) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF tip(4, 3);
    for (const auto &[offset, fill, outline] : {std::tuple(5.0, Qt::white, Qt::black), std::tuple(0.0, Qt::black, Qt::white)}) {
        const QPainterPath arrow = arrowPath().translated(tip + QPointF(offset, offset));
        painter.setPen(QPen(outline, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(arrow);
        painter.fillPath(arrow, fill);
    }
    return QCursor(pixmap, 4, 3);
}
