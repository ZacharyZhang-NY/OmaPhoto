#include "UI/ProjectTabs.h"
#include "IO/ImageFileDrop.h"
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QScrollBar>

namespace {
QFont tabFont(bool active)
{
    QFont font;
    font.setPixelSize(12);
    font.setWeight(active ? QFont::DemiBold : QFont::Medium);
    return font;
}
}

ProjectTabButton::ProjectTabButton(ProjectWorkspace &workspace, std::shared_ptr<ProjectTab> tab, QWidget *parent)
    : QWidget(parent), tab(std::move(tab)), m_workspace(workspace), m_select(new QToolButton(this)), m_close(new QToolButton(this))
{
    // 28 high: the strip's 34 less its margins.
    m_select->setObjectName(QStringLiteral("selectTab"));
    m_select->setAutoRaise(true);
    m_select->setFocusPolicy(Qt::NoFocus);
    m_select->setMinimumWidth(35);
    m_select->setMaximumWidth(155);
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
    connect(m_select, &QToolButton::clicked, this, [this] { m_workspace.select(this->tab->id); });
    connect(m_close, &QToolButton::clicked, this, [this] { m_workspace.close(this->tab->id); });
    // The strip syncs every button on the workspace's news.
    connect(&this->tab->session, &EditorSession::changed, this, &ProjectTabButton::synchronize);
    synchronize();
}

void ProjectTabButton::synchronize()
{
    const bool active = m_workspace.selectedID() == tab->id;
    // Swift's dot for unsaved changes stands before the title.
    m_select->setText((tab->session.isModified() ? QStringLiteral("● ") : QString()) + tab->title());
    m_select->setFont(tabFont(active));
    m_select->setToolTip(tab->title());
    m_select->setEnabled(m_workspace.canSwitch() || active);
    m_close->setToolTip(QStringLiteral("Close %1").arg(tab->title()));
    m_close->setAccessibleName(QStringLiteral("Close %1").arg(tab->title()));
    m_close->setEnabled(m_workspace.canSwitch());
    // The capsule repaints with the buttons it holds.
    setProperty("active", active);
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

ProjectTabStrip::ProjectTabStrip(ProjectWorkspace &workspace, QWidget *parent)
    : QScrollArea(parent), m_workspace(workspace), m_row(new QWidget(this))
{
    setObjectName(QStringLiteral("projectTabs"));
    setAccessibleName(QStringLiteral("Project tabs"));
    setFixedHeight(34);
    setFrameShape(QFrame::NoFrame);
    // The toolbar shows through; a stylesheet would freeze the palette.
    setAutoFillBackground(false);
    viewport()->setAutoFillBackground(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *row = new QHBoxLayout(m_row);
    row->setContentsMargins(0, 3, 0, 3);
    row->setSpacing(6);
    row->addStretch(1);
    setWidget(m_row);
    // setWidget turns the row's fill on; the toolbar shows through.
    m_row->setAutoFillBackground(false);
    m_row->installEventFilter(this);
    connect(&m_workspace, &ProjectWorkspace::changed, this, &ProjectTabStrip::synchronize);
    synchronize();
}

QList<ProjectTabButton *> ProjectTabStrip::buttons() const
{
    QList<ProjectTabButton *> result;
    for (int index = 0; index < m_row->layout()->count(); ++index) {
        if (auto *button = qobject_cast<ProjectTabButton *>(m_row->layout()->itemAt(index)->widget()))
            result << button;
    }
    return result;
}

void ProjectTabStrip::synchronize()
{
    // Buttons follow the tabs: kept where the tab stays.
    auto *row = static_cast<QHBoxLayout *>(m_row->layout());
    QList<ProjectTabButton *> shown = buttons();
    int position = 0;
    for (const std::shared_ptr<ProjectTab> &tab : m_workspace.tabs()) {
        const auto kept = std::find_if(shown.begin(), shown.end(), [&](ProjectTabButton *button) { return button->tab == tab; });
        ProjectTabButton *button = kept == shown.end() ? new ProjectTabButton(m_workspace, tab, m_row) : *kept;
        if (kept != shown.end())
            shown.erase(kept);
        row->removeWidget(button);
        row->insertWidget(position, button);
        button->show();
        button->synchronize();
        position += 1;
    }
    for (ProjectTabButton *gone : shown)
        delete gone;
    // The row is as wide as its tabs, never squeezed.
    m_row->resize(m_row->sizeHint());
    showFront();
}

bool ProjectTabStrip::eventFilter(QObject *watched, QEvent *event)
{
    // A title changed size: the row takes its new width.
    if (event->type() == QEvent::LayoutRequest) {
        m_row->resize(m_row->sizeHint());
        showFront();
    }
    return QScrollArea::eventFilter(watched, event);
}

// The front tab scrolls into view.
void ProjectTabStrip::showFront()
{
    for (ProjectTabButton *button : buttons()) {
        if (button->tab->id == m_workspace.selectedID())
            ensureWidgetVisible(button, 0, 0);
    }
}
