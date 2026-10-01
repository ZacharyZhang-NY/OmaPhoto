#include "UI/ProjectTabs.h"
#include "IO/ImageFileDrop.h"
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionToolButton>
#include <QStylePainter>

namespace {
QFont tabFont(bool active)
{
    QFont font;
    font.setPixelSize(12);
    font.setWeight(active ? QFont::DemiBold : QFont::Medium);
    return font;
}

// Swift's leading label: text from the left, 8 kept after.
class TabTitle : public QToolButton {
public:
    using QToolButton::QToolButton;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QStylePainter painter(this);
        QStyleOptionToolButton option;
        initStyleOption(&option);
        option.text.clear();
        painter.drawComplexControl(QStyle::CC_ToolButton, option);
        painter.setPen(palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::ButtonText));
        painter.drawText(rect().adjusted(0, 0, -8, 0), Qt::AlignLeft | Qt::AlignVCenter, text());
    }
};

// Swift's projectTabLabelWidth: the title's own width, 35 to 155.
int labelWidth(const QString &title, bool active, bool modified)
{
    const double titleWidth = QFontMetricsF(tabFont(active)).horizontalAdvance(title);
    return int(std::min(155.0, std::max(35.0, std::ceil(titleWidth) + (modified ? 10 : 0))));
}
}

ProjectTabButton::ProjectTabButton(ProjectWorkspace &workspace, std::shared_ptr<ProjectTab> tab, std::function<void(TabDragPhase, double)> onReorder,
                                   QWidget *parent)
    : QWidget(parent), tab(std::move(tab)), m_workspace(workspace), m_onReorder(std::move(onReorder)), m_select(new TabTitle(this)),
      m_close(new QToolButton(this))
{
    // 28 high: the strip's 34 less its margins.
    m_select->setObjectName(QStringLiteral("selectTab"));
    m_select->setAutoRaise(true);
    m_select->setFocusPolicy(Qt::NoFocus);
    m_close->setObjectName(QStringLiteral("closeTab"));
    m_close->setAutoRaise(true);
    m_close->setFocusPolicy(Qt::NoFocus);
    m_close->setText(QStringLiteral("×"));
    m_close->setFixedSize(16, 28);
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(11, 0, 5, 0);
    row->setSpacing(0);
    row->addWidget(m_select);
    row->addWidget(m_close);
    setAcceptDrops(true);
    m_select->installEventFilter(this);
    connect(m_select, &QToolButton::clicked, this, [this] { m_workspace.select(this->tab->id); });
    connect(m_close, &QToolButton::clicked, this, [this] { m_workspace.close(this->tab->id); });
    // The strip syncs every button on the workspace's news.
    connect(&this->tab->session, &EditorSession::changed, this, &ProjectTabButton::synchronize);
    synchronize();
}

void ProjectTabButton::synchronize()
{
    const bool active = m_workspace.selectedID() == tab->id;
    // The unsaved dot leads; fixed widths move later tabs only.
    const bool modified = tab->session.isModified();
    const int label = labelWidth(tab->title(), active, modified);
    const QString dot = modified ? QStringLiteral("● ") : QString();
    const QFontMetrics metrics(tabFont(active));
    m_select->setText(dot + metrics.elidedText(tab->title(), Qt::ElideRight, label - (modified ? 10 : 0)));
    m_select->setFont(tabFont(active));
    // Swift's trailing 8 inside; a character dot outgrows Swift's 10.
    m_select->setFixedWidth(label + 8 + (modified ? metrics.horizontalAdvance(dot) - 10 : 0));
    m_select->setToolTip(tab->title());
    m_select->setEnabled(m_workspace.canSwitch() || active);
    m_close->setToolTip(QStringLiteral("Close %1").arg(tab->title()));
    m_close->setAccessibleName(QStringLiteral("Close %1").arg(tab->title()));
    m_close->setEnabled(m_workspace.canSwitch());
    // The capsule repaints with the buttons it holds.
    setProperty("active", active);
}

// A press moved three points reorders; measured in the window.
bool ProjectTabButton::eventFilter(QObject *watched, QEvent *event)
{
    const auto *mouse = static_cast<QMouseEvent *>(event);
    if (event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton) {
        m_press = mouse->globalPosition();
        m_dragging = false;
    } else if (event->type() == QEvent::MouseMove && m_press && mouse->buttons().testFlag(Qt::LeftButton)) {
        m_dragging = m_dragging || QLineF(*m_press, mouse->globalPosition()).length() >= 3;
        if (m_dragging)
            m_onReorder(TabDragPhase::changed, mouse->globalPosition().x() - m_press->x());
    } else if (event->type() == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton && m_press) {
        const double translation = mouse->globalPosition().x() - m_press->x();
        m_press.reset();
        if (std::exchange(m_dragging, false))
            m_onReorder(TabDragPhase::ended, translation);
    }
    return QWidget::eventFilter(watched, event);
}

// Another tab's layer, unless Alt duplicates it, or files.
bool ProjectTabButton::acceptsDrop(const QMimeData &data) const
{
    const bool layer = data.hasFormat(ProjectWorkspace::layerType);
    if ((layer && QApplication::keyboardModifiers().testFlag(Qt::AltModifier)) || !(layer || ImageFileDrop::holdsImages(data)))
        return false;
    return m_workspace.canSwitch() && m_workspace.canReceiveDrag(data, tab->id);
}

void ProjectTabButton::setTargeted(bool targeted)
{
    m_targeted = targeted;
    m_select->setToolTip(targeted ? QStringLiteral("Add to %1").arg(tab->title()) : tab->title());
    QWidget::update();
}

void ProjectTabButton::dragEnterEvent(QDragEnterEvent *event)
{
    if (!acceptsDrop(*event->mimeData()))
        return;
    event->setDropAction(Qt::CopyAction);
    event->accept();
    setTargeted(true);
}

void ProjectTabButton::dragMoveEvent(QDragMoveEvent *event)
{
    if (!acceptsDrop(*event->mimeData())) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
}

void ProjectTabButton::dragLeaveEvent(QDragLeaveEvent *)
{
    setTargeted(false);
}

void ProjectTabButton::dropEvent(QDropEvent *event)
{
    setTargeted(false);
    if (!acceptsDrop(*event->mimeData()))
        return;
    event->setDropAction(Qt::CopyAction);
    event->accept();
    m_workspace.receiveProviders(*event->mimeData(), tab->id);
}

bool ProjectTabButton::isActive() const
{
    return property("active").toBool();
}

void ProjectTabButton::paintEvent(QPaintEvent *)
{
    // A capsule: brighter under the front tab.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool active = property("active").toBool();
    QColor fill = palette().color(QPalette::WindowText), edge = fill;
    fill.setAlphaF(active ? 0.12 : 0.035);
    edge.setAlphaF(active ? 0.22 : 0.08);
    // A drop target takes the accent, filled and rimmed.
    if (m_targeted) {
        fill = palette().color(QPalette::Highlight);
        fill.setAlphaF(0.3);
        edge = palette().color(QPalette::Highlight);
    }
    painter.setPen(QPen(edge, m_targeted ? 2 : 1));
    painter.setBrush(fill);
    const double inset = m_targeted ? 1 : 0.5;
    painter.drawRoundedRect(QRectF(inset, inset, width() - 2 * inset, height() - 2 * inset), 14, 14);
}

NewCanvasButton::NewCanvasButton(ProjectWorkspace &workspace, QWidget *parent) : QToolButton(parent), m_workspace(workspace)
{
    setObjectName(QStringLiteral("newCanvasButton"));
    setAcceptDrops(true);
}

// Any tab's layer or files, for a fresh tab.
bool NewCanvasButton::acceptsDrop(const QMimeData &data) const
{
    const bool layer = data.hasFormat(ProjectWorkspace::layerType);
    if ((layer && QApplication::keyboardModifiers().testFlag(Qt::AltModifier)) || !(layer || ImageFileDrop::holdsImages(data)))
        return false;
    return m_workspace.canSwitch() && m_workspace.canReceiveDrag(data, std::nullopt);
}

void NewCanvasButton::setTargeted(bool targeted)
{
    m_targeted = targeted;
    setToolTip(targeted ? QStringLiteral("Open in a new project tab") : defaultAction()->toolTip());
    update();
}

void NewCanvasButton::paintEvent(QPaintEvent *event)
{
    QToolButton::paintEvent(event);
    if (!m_targeted)
        return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 6, 6);
}

void NewCanvasButton::dragEnterEvent(QDragEnterEvent *event)
{
    if (!acceptsDrop(*event->mimeData()))
        return;
    event->setDropAction(Qt::CopyAction);
    event->accept();
    setTargeted(true);
}

void NewCanvasButton::dragMoveEvent(QDragMoveEvent *event)
{
    if (!acceptsDrop(*event->mimeData())) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
}

void NewCanvasButton::dragLeaveEvent(QDragLeaveEvent *)
{
    setTargeted(false);
}

void NewCanvasButton::dropEvent(QDropEvent *event)
{
    setTargeted(false);
    if (!acceptsDrop(*event->mimeData()))
        return;
    event->setDropAction(Qt::CopyAction);
    event->accept();
    m_workspace.receiveProviders(*event->mimeData(), std::nullopt);
}

OverflowTabsPill::OverflowTabsPill(ProjectWorkspace &workspace, QWidget *parent) : QToolButton(parent), m_workspace(workspace)
{
    setObjectName(QStringLiteral("projectTabsOverflow"));
    setFocusPolicy(Qt::NoFocus);
    QFont font;
    font.setPixelSize(12);
    font.setWeight(QFont::Medium);
    setFont(font);
    connect(this, &QToolButton::pressed, this, &OverflowTabsPill::showMenu);
}

double OverflowTabsPill::pillWidth(int hiddenCount)
{
    QFont font;
    font.setPixelSize(12);
    font.setWeight(QFont::Medium);
    // Leading, gap before the chevron, chevron, trailing.
    return std::ceil(QFontMetricsF(font).horizontalAdvance(projectTabOverflowLabel(hiddenCount))) + 11 + 4 + 10 + 11;
}

void OverflowTabsPill::setHiddenIDs(std::vector<QUuid> hiddenIDs)
{
    m_hiddenIDs = std::move(hiddenIDs);
    const QString label = projectTabOverflowLabel(int(m_hiddenIDs.size()));
    setText(label);
    setToolTip(label);
    setAccessibleName(label);
    update();
}

// Below the pill: hidden tabs in order, unsaved marked.
void OverflowTabsPill::showMenu()
{
    setDown(false);
    if (!m_workspace.canSwitch())
        return;
    auto *menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("projectTabsOverflowMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);
    for (const QUuid &id : m_hiddenIDs) {
        const std::shared_ptr<ProjectTab> tab = m_workspace.tab(id);
        const QString title = (tab->session.isModified() ? QStringLiteral("• ") : QString()) + tab->title();
        connect(menu->addAction(title), &QAction::triggered, this, [this, id] { m_workspace.select(id); });
    }
    menu->popup(mapToGlobal(QPoint(0, height() + 4)));
}

void OverflowTabsPill::paintEvent(QPaintEvent *)
{
    // An inactive tab's capsule; the label dims while busy.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor fill = palette().color(QPalette::WindowText), edge = fill;
    fill.setAlphaF(0.035);
    edge.setAlphaF(0.08);
    painter.setPen(QPen(edge, 1));
    painter.setBrush(fill);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);
    painter.setOpacity(m_workspace.canSwitch() ? 1 : 0.5);
    painter.setPen(palette().color(QPalette::ButtonText));
    painter.drawText(QRectF(11, 0, width() - 36, height()), Qt::AlignLeft | Qt::AlignVCenter, text());
    // Swift's chevron.down, nine points, medium.
    QPainterPath chevron;
    const double left = width() - 21, middle = height() / 2.0;
    chevron.moveTo(left + 1, middle - 2);
    chevron.lineTo(left + 5, middle + 2);
    chevron.lineTo(left + 9, middle - 2);
    painter.setPen(QPen(palette().color(QPalette::ButtonText), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(chevron);
}
