#include "Document/ProjectWorkspace.h"
#include "Rendering/EditorCanvas.h"
#include "UI/LayerIcons.h"
#include "UI/NativeLayerList.h"
#include <QAccessibleWidget>
#include <QApplication>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>

const QString NativeLayerList::effectType = QStringLiteral("com.compositor.layer-effect");

namespace {
// Swift's group element, whose press chooses the effect.
class EffectRowAccessible : public QAccessibleWidget {
public:
    explicit EffectRowAccessible(LayerEffectRow *row) : QAccessibleWidget(row, QAccessible::Grouping) {}
    QStringList actionNames() const override { return {pressAction()}; }
    void doAction(const QString &name) override
    {
        if (name == pressAction())
            static_cast<LayerEffectRow *>(widget())->select(false);
    }
};

QAccessibleInterface *effectRowAccessible(const QString &key, QObject *object)
{
    return key == QLatin1String("LayerEffectRow") ? new EffectRowAccessible(static_cast<LayerEffectRow *>(object)) : nullptr;
}

bool option(Qt::KeyboardModifiers modifiers)
{
    return modifiers.testFlag(Qt::AltModifier) && !modifiers.testFlag(Qt::ControlModifier);
}
}

LayerEffectRow::LayerEffectRow(NativeLayerList &list, QUuid layerID, LayerEffectKind kind, QWidget *parent)
    : QWidget(parent), layerID(layerID), kind(kind), m_list(list), m_eye(new QToolButton(this)), m_label(new QLabel(this))
{
    [[maybe_unused]] static const bool accessible = (QAccessible::installFactory(effectRowAccessible), true);
    setObjectName(QStringLiteral("layerEffectRow"));
    setToolTip(QStringLiteral("Click to select; double-click to edit; Alt-drag to copy ") + rawValue(kind).toLower());
    setAccessibleName(rawValue(kind) + QStringLiteral(" effect"));
    m_eye->setObjectName(QStringLiteral("effectEye"));
    m_eye->setAutoRaise(true);
    m_eye->setFixedSize(20, 22);
    m_eye->installEventFilter(this);
    connect(m_eye, &QToolButton::clicked, this, [this] { m_list.session().toggleEffect(this->kind, this->layerID); });
    QFont small = m_label->font();
    small.setPixelSize(11);
    m_label->setFont(small);
    // Hover reaches the list's cursor, as every row's controls.
    for (QWidget *each : {static_cast<QWidget *>(this), static_cast<QWidget *>(m_eye), static_cast<QWidget *>(m_label)})
        each->setMouseTracking(true);
}

void LayerEffectRow::configure(bool enabled, bool editable, int indent)
{
    m_eye->setIcon(LayerIcons::pixmap(enabled ? LayerIcon::eye : LayerIcon::eyeSlash, 16, palette().color(QPalette::PlaceholderText), devicePixelRatio()));
    m_eye->setEnabled(editable);
    m_eye->setAccessibleName((enabled ? QStringLiteral("Hide ") : QStringLiteral("Show ")) + rawValue(kind));
    m_label->setForegroundRole(enabled ? QPalette::WindowText : QPalette::PlaceholderText);
    m_indent = indent;
    relayout();
}

void LayerEffectRow::relayout()
{
    m_eye->move(38 + m_indent, (height() - m_eye->height()) / 2);
    const int left = m_eye->geometry().right() + 1 + 8;
    m_label->setGeometry(left, 0, std::max(0, width() - left - 8), height());
    m_label->setText(m_label->fontMetrics().elidedText(rawValue(kind), Qt::ElideRight, m_label->width()));
}

void LayerEffectRow::resizeEvent(QResizeEvent *)
{
    relayout();
}

void LayerEffectRow::select(bool editing)
{
    m_list.session().selectEffect(kind, layerID, editing);
    m_list.focusList();
}

// Alt clips on the strip, else arms; plain, it chooses.
void LayerEffectRow::press(QMouseEvent *event, bool twice)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_copyDown = std::nullopt;
    auto &cell = *static_cast<LayerCell *>(parentWidget());
    const QPoint inCell = mapToParent(event->position().toPoint());
    if (option(event->modifiers()) && NativeLayerList::isClippingZone(cell, inCell)) {
        m_list.clickRow(cell, event->modifiers(), inCell);
        return;
    }
    if (option(event->modifiers()) && m_list.session().canEditLayers())
        m_copyDown = event->position().toPoint();
    else
        select(twice);
}

void LayerEffectRow::mousePressEvent(QMouseEvent *event)
{
    press(event, false);
}

void LayerEffectRow::mouseDoubleClickEvent(QMouseEvent *event)
{
    press(event, true);
}

// Held past the distance with Alt, the effect is dragged.
void LayerEffectRow::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_copyDown || !event->buttons().testFlag(Qt::LeftButton)
        || (event->position().toPoint() - *m_copyDown).manhattanLength() < QApplication::startDragDistance())
        return;
    m_copyDown = std::nullopt;
    if (event->modifiers().testFlag(Qt::AltModifier) && m_list.session().canEditLayers())
        m_list.startEffectDrag(*this);
}

// Released without a drag, an Alt press chooses the effect.
void LayerEffectRow::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_copyDown && event->button() == Qt::LeftButton)
        select(false);
}

// The eye's Alt press, ignored, goes on to the row.
bool LayerEffectRow::eventFilter(QObject *watched, QEvent *event)
{
    const bool press = event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick;
    if (press && option(static_cast<QMouseEvent *>(event)->modifiers())) {
        event->ignore();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void LayerEffectRow::paintEvent(QPaintEvent *)
{
    if (m_list.session().selectedEffect() != LayerEffectSelection{layerID, kind})
        return;
    QColor fill = palette().color(QPalette::Highlight);
    fill.setAlphaF(0.3);
    QPainter(this).fillRect(rect(), fill);
}

// Alt offers a copy alone, within this list.
void NativeLayerList::startEffectDrag(LayerEffectRow &row)
{
    auto *data = new QMimeData;
    data->setData(effectType, (uuidString(row.layerID) + QLatin1Char(':') + rawValue(row.kind)).toUtf8());
    data->setData(sourceType, m_dragToken.toUtf8());
    auto *drag = new QDrag(this);
    drag->setMimeData(data);
    drag->setPixmap(row.grab());
    drag->setDragCursor(CanvasView::duplicateCursor(devicePixelRatio()).pixmap(), Qt::CopyAction);
    drag->exec(Qt::CopyAction);
}

// This list's dragged effect: its layer and kind.
std::optional<LayerEffectSelection> NativeLayerList::draggedEffect(const QMimeData &data) const
{
    if (QString::fromUtf8(data.data(sourceType)) != m_dragToken)
        return std::nullopt;
    const QStringList parts = QString::fromUtf8(data.data(effectType)).split(QLatin1Char(':'));
    const std::optional<QUuid> id = parts.size() == 2 ? ProjectWorkspace::layerID(parts[0]) : std::nullopt;
    for (const LayerEffectKind kind : allLayerEffectKinds) {
        if (id && rawValue(kind) == parts[1])
            return LayerEffectSelection{*id, kind};
    }
    return std::nullopt;
}
