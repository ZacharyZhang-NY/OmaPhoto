#include "UI/NativeLayerList.h"
#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>

EyeSwipeButton::EyeSwipeButton(NativeLayerList &list, QWidget *parent) : QToolButton(parent), m_list(list)
{
    setAutoRaise(true);
    setFixedSize(20, 32);
}

// A row rebuilt mid-swipe takes the button; the step ends.
EyeSwipeButton::~EyeSwipeButton()
{
    if (!m_swipe)
        return;
    qApp->removeEventFilter(this);
    QMetaObject::invokeMethod(&m_list.session(), &EditorSession::endVisibilitySwipe, Qt::QueuedConnection);
}

void EyeSwipeButton::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !isEnabled())
        return;
    m_swipe = m_list.session().beginVisibilitySwipe(layerID);
    if (m_swipe)
        qApp->installEventFilter(this);
}

void EyeSwipeButton::endSwipe()
{
    m_swipe = std::nullopt;
    qApp->removeEventFilter(this);
    m_list.session().endVisibilitySwipe();
}

// Swift tracks the drag itself: no release is missed.
bool EyeSwipeButton::eventFilter(QObject *watched, QEvent *event)
{
    if (m_swipe && event->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton)
        endSwipe();
    return QToolButton::eventFilter(watched, event);
}

void EyeSwipeButton::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_swipe)
        return;
    // Past the viewport's edge the list scrolls, as autoscroll does.
    const QPoint inList = mapTo(&m_list, event->position().toPoint());
    const int overshoot = inList.y() < 0 ? inList.y() : std::max(0, inList.y() - m_list.viewport()->height());
    if (overshoot != 0)
        m_list.verticalScrollBar()->setValue(m_list.verticalScrollBar()->value() + overshoot);
    const int row = m_list.rowAt(inList);
    if (row >= 0 && row < int(m_list.cells().size()))
        m_list.session().setVisibilityInSwipe(m_list.cells()[row]->layerID(), *m_swipe);
}

void EyeSwipeButton::mouseReleaseEvent(QMouseEvent *event)
{
    // Another button's release leaves the left one's swipe.
    if (m_swipe && event->button() == Qt::LeftButton)
        endSwipe();
}

LayerThumbnailButton::LayerThumbnailButton(NativeLayerList &list, bool maskTarget, QWidget *parent)
    : QToolButton(parent), isMaskTarget(maskTarget), m_list(list)
{
    setAutoRaise(true);
}

void LayerThumbnailButton::setTargeted(bool targeted, bool alone)
{
    m_targeted = targeted;
    m_alone = alone;
    QWidget::update();
}

void LayerThumbnailButton::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !isEnabled())
        return;
    auto *cell = static_cast<LayerCell *>(parentWidget());
    // Every press starts from nothing: no earlier arm survives.
    cell->disarmDrag();
    const QPoint inCell = mapToParent(event->position().toPoint());
    const bool control = event->modifiers().testFlag(Qt::ControlModifier), shift = event->modifiers().testFlag(Qt::ShiftModifier);
    // Alt on a mask drags a copy; else the row's.
    if (event->modifiers().testFlag(Qt::AltModifier) && !control) {
        if (isMaskTarget) {
            m_altMaskPress = event->position().toPoint();
            qApp->installEventFilter(this);
        } else if (!m_list.clickRow(*cell, event->modifiers(), inCell))
            cell->armDrag(inCell, event->modifiers());
        return;
    }
    // Ctrl loads a selection: Ctrl-Shift adds, Ctrl-Alt subtracts.
    if (control) {
        const SelectionMode mode = event->modifiers().testFlag(Qt::AltModifier) ? SelectionMode::subtract : shift ? SelectionMode::add : SelectionMode::replace;
        if (isMaskTarget)
            m_list.session().loadMaskSelection(layerID, mode);
        else
            m_list.session().loadLayerSelection(layerID, mode);
        return;
    }
    if (isMaskTarget && shift) {
        m_list.session().selectLayerTarget(layerID, true);
        m_list.session().toggleLayerMask();
        return;
    }
    if (!shift)
        m_list.session().selectLayerTarget(layerID, isMaskTarget);
    if (!m_list.clickRow(*cell, event->modifiers(), inCell))
        cell->armDrag(inCell, event->modifiers());
}

// Moved far enough, Alt on a mask drags a copy.
void LayerThumbnailButton::mouseMoveEvent(QMouseEvent *event)
{
    auto *cell = static_cast<LayerCell *>(parentWidget());
    if (!m_altMaskPress) {
        cell->moveDrag(mapToParent(event->position().toPoint()), event->buttons());
        return;
    }
    if ((event->position().toPoint() - *m_altMaskPress).manhattanLength() < QApplication::startDragDistance())
        return;
    endAltMaskPress();
    m_list.startMaskDrag(*this);
}

LayerThumbnailButton::~LayerThumbnailButton()
{
    if (m_altMaskPress)
        qApp->removeEventFilter(this);
}

void LayerThumbnailButton::endAltMaskPress()
{
    m_altMaskPress = std::nullopt;
    qApp->removeEventFilter(this);
}

// Another widget's release ends it; the window's does not.
bool LayerThumbnailButton::eventFilter(QObject *watched, QEvent *event)
{
    if (m_altMaskPress && watched->isWidgetType() && watched != this && event->type() == QEvent::MouseButtonRelease
        && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton)
        endAltMaskPress();
    return QToolButton::eventFilter(watched, event);
}

void LayerThumbnailButton::mouseReleaseEvent(QMouseEvent *event)
{
    // Only the left button's release ends its press.
    if (!m_altMaskPress || event->button() != Qt::LeftButton)
        return;
    endAltMaskPress();
    // Released where pressed: the mask alone, or the image again.
    if (rect().contains(event->position().toPoint()))
        m_list.session().toggleMaskAlone(layerID);
}

// With a modifier the second press is the button's again.
void LayerThumbnailButton::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier))
        mousePressEvent(event);
    else
        event->ignore();
}

void LayerThumbnailButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.drawPixmap(rect(), icon().pixmap(size()));
    if (!m_targeted)
        return;
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(m_alone ? QColor(Qt::white) : palette().color(QPalette::Highlight), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 3, 3);
}
