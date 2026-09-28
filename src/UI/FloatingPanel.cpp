#include "UI/FloatingPanel.h"
#include "Rendering/EditorCanvas.h"
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QHash>
#include <QLabel>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
// Top-left corners by panel, for this run of the app.
QHash<QString, QPoint> &positions()
{
    static QHash<QString, QPoint> positions;
    return positions;
}
}

// Swift's NSPanel: a tool window whose close is Cancel.
class PanelWindow : public QDialog {
public:
    PanelWindow(FloatingPanel &owner, QWidget *parent) : QDialog(parent), m_owner(owner)
    {
        // A dialog is non-modal unless asked: Swift's panel.
        setWindowFlag(Qt::Tool);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSizeConstraint(QLayout::SetFixedSize);
    }

protected:
    // QDialog's own would reject, and so close, again.
    void closeEvent(QCloseEvent *event) override
    {
        m_owner.closed();
        QWidget::closeEvent(event);
    }
    // Escape closes as the close button does.
    void reject() override { close(); }
    void moveEvent(QMoveEvent *event) override
    {
        QDialog::moveEvent(event);
        // A docked frame is the window's: never remembered.
        if (isVisible() && isWindow())
            m_owner.remember();
    }

private:
    FloatingPanel &m_owner;
};

FloatingPanel::FloatingPanel(const QString &name, QWidget &owner) : m_name(name), m_owner(owner) {}

FloatingPanel::~FloatingPanel()
{
    delete m_panel;
    delete m_docked;
}

void FloatingPanel::show(const QString &title, QWidget *content, QWidget *dock)
{
    if (dock) {
        showDocked(title, content, *dock);
        return;
    }
    if (!m_panel) {
        m_panel = new PanelWindow(*this, m_owner.window());
        m_panel->setObjectName(m_name);
    }
    const bool wasVisible = m_panel->isVisible();
    m_panel->setWindowTitle(title);
    place(content);
    m_panel->layout()->addWidget(content);
    // Shown now: layouts show late children from the event loop.
    content->show();
    m_panel->adjustSize();
    // A shown panel stays; without a canvas QDialog centres itself.
    if (!wasVisible) {
        const CanvasView *canvas = m_owner.window()->findChild<CanvasView *>();
        if (positions().contains(m_name))
            m_panel->move(positions().value(m_name));
        else if (canvas)
            m_panel->move(canvas->mapToGlobal(canvas->rect().center()) - QPoint(m_panel->width() / 2, m_panel->height() / 2));
    }
    m_panel->show();
    m_panel->raise();
    m_panel->activateWindow();
    remember();
}

// Wayland lets no client place a window: a slot instead.
void FloatingPanel::showDocked(const QString &title, QWidget *content, QWidget &dock)
{
    if (!m_docked) {
        m_docked = new PanelWindow(*this, &dock);
        m_docked->setWindowFlags(Qt::Widget);
        m_docked->setObjectName(m_name);
        m_docked->layout()->setSizeConstraint(QLayout::SetDefaultConstraint);
        auto *header = new QWidget(m_docked);
        auto *row = new QHBoxLayout(header);
        row->setContentsMargins(12, 8, 8, 0);
        auto *heading = new QLabel(header);
        heading->setObjectName(QStringLiteral("dockedTitle"));
        auto *closer = new QToolButton(header);
        closer->setObjectName(QStringLiteral("dockedClose"));
        closer->setIcon(closer->style()->standardIcon(QStyle::SP_TitleBarCloseButton));
        closer->setAccessibleName(QStringLiteral("Close"));
        closer->setAutoRaise(true);
        QObject::connect(closer, &QToolButton::clicked, m_docked, &QWidget::close);
        row->addWidget(heading, 1);
        row->addWidget(closer);
        m_docked->layout()->addWidget(header);
        dock.layout()->addWidget(m_docked);
    }
    m_docked->findChild<QLabel *>(QStringLiteral("dockedTitle"))->setText(title);
    place(content);
    m_docked->layout()->addWidget(content);
    content->show();
    m_docked->show();
    dock.show();
    m_docked->setFocus();
}

// Later: a signal from the old content may still run.
void FloatingPanel::place(QWidget *content)
{
    if (m_content) {
        m_content->hide();
        m_content->deleteLater();
    }
    m_content = content;
}

// Moves were remembered as they came; hide sends no close.
void FloatingPanel::close()
{
    if (m_panel)
        m_panel->hide();
    if (m_docked)
        m_docked->parentWidget()->hide();
}

bool FloatingPanel::isVisible() const
{
    return (m_panel && m_panel->isVisible()) || (m_docked && m_docked->isVisible());
}

void FloatingPanel::refocus(const QString &name)
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == name && widget->isVisible())
            widget->activateWindow();
        // A docked panel takes the keys inside its window.
        for (QDialog *docked : widget->findChildren<QDialog *>(name)) {
            if (docked->isVisible() && !docked->isWindow())
                docked->setFocus();
        }
    }
}

void FloatingPanel::remember()
{
    positions().insert(m_name, m_panel->pos());
}

void FloatingPanel::closed()
{
    if (onClose)
        onClose();
}
