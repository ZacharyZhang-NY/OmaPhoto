#include "UI/ProjectWorkspaceView.h"
#include <QCloseEvent>

namespace {
QAction *action(QToolBar *bar, const QString &text, const QString &help, const QString &name)
{
    QAction *made = bar->addAction(text);
    made->setToolTip(help);
    made->setObjectName(name);
    return made;
}
}

ProjectWorkspaceView::ProjectWorkspaceView(ProjectWorkspace &workspace, QWidget *parent)
    : QMainWindow(parent), m_workspace(workspace), m_toolbar(new QToolBar(this)), m_tabs(new ProjectTabStrip(workspace, this))
{
    setMinimumSize(800, 520);
    resize(1180, 780);
    m_toolbar->setObjectName(QStringLiteral("toolbar"));
    m_toolbar->setMovable(false);
    m_toolbar->setFloatable(false);
    // Its button takes drops for a fresh tab.
    m_newCanvas = new QAction(QStringLiteral("+"), this);
    m_newCanvas->setToolTip(QStringLiteral("New canvas (Ctrl+N) · Drop images here for new tabs"));
    m_newCanvas->setObjectName(QStringLiteral("newCanvasToolbar"));
    auto *newButton = new NewCanvasButton(m_workspace, m_toolbar);
    newButton->setDefaultAction(m_newCanvas);
    m_toolbar->addWidget(newButton);
    m_toolbar->addWidget(m_tabs);
    // The strip takes the toolbar's free width; zooms stay right.
    auto *spacer = new QWidget(m_toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(spacer);
    m_fit = action(m_toolbar, QStringLiteral("Fit"), QStringLiteral("Fit canvas in window (Ctrl+0)"), QStringLiteral("fitCanvas"));
    m_actualPixels = action(m_toolbar, QStringLiteral("100%"), QStringLiteral("Actual pixels (Ctrl+1)"), QStringLiteral("actualPixels"));
    m_zoomIn = action(m_toolbar, QStringLiteral("+"), QStringLiteral("Zoom in (Ctrl++)"), QStringLiteral("zoomIn"));
    m_zoomOut = action(m_toolbar, QStringLiteral("−"), QStringLiteral("Zoom out (Ctrl+−)"), QStringLiteral("zoomOut"));
    addToolBar(Qt::TopToolBarArea, m_toolbar);
    new CompositorMenus(m_workspace, *menuBar(), *this);
    m_workspace.window = this;
    connect(m_newCanvas, &QAction::triggered, this, [this] { m_workspace.current().controller.newCanvas(); });
    connect(m_fit, &QAction::triggered, this, [this] { m_workspace.current().session.fit(); });
    connect(m_actualPixels, &QAction::triggered, this, [this] { m_workspace.current().session.zoom(1); });
    connect(m_zoomIn, &QAction::triggered, this, [this] { m_workspace.current().session.zoomKeyboard(1); });
    connect(m_zoomOut, &QAction::triggered, this, [this] { m_workspace.current().session.zoomKeyboard(-1); });
    connect(&m_workspace, &ProjectWorkspace::changed, this, &ProjectWorkspaceView::synchronize);
    synchronize();
}

void ProjectWorkspaceView::watchFront()
{
    // Every tab's buttons dim while the front tab is busy.
    disconnect(m_sessionWatch);
    m_sessionWatch = connect(&m_workspace.current().session, &EditorSession::changed, this, [this] {
        synchronizeSession();
        for (ProjectTabButton *button : m_tabs->buttons())
            button->synchronize();
    });
}

void ProjectWorkspaceView::synchronize()
{
    const std::shared_ptr<ProjectTab> front = m_workspace.tab(m_workspace.current().id);
    // A new front tab gets a fresh editor.
    if (m_shownTab != front) {
        front->controller.window = this;
        delete m_content;
        // Only now may the old tab, and its session, end.
        m_shownTab = front;
        m_content = new ContentView(front->session, &front->controller, this);
        m_content->setWorkspace(&m_workspace);
        // The window owns the minimum; the editor may be shorter.
        m_content->setMinimumSize(0, 0);
        setCentralWidget(m_content);
        // A late central widget shows only later: show now.
        m_content->show();
        watchFront();
        synchronizeSession();
    }
    m_content->setEnabled(!m_workspace.isManaging());
}

void ProjectWorkspaceView::synchronizeSession()
{
    const EditorSession &session = m_workspace.current().session;
    const bool drawn = session.document().has_value();
    m_newCanvas->setEnabled(!session.isImporting() && !session.showsBusy() && !session.levels());
    for (QAction *zoom : {m_fit, m_actualPixels, m_zoomIn, m_zoomOut})
        zoom->setEnabled(drawn);
    // The front tab's title, as its strip button shows it.
    setWindowTitle(m_workspace.current().title() + QStringLiteral("[*]"));
    setWindowModified(session.isModified());
    setWindowFilePath(session.projectPath().value_or(QString()));
}

void ProjectWorkspaceView::closeEvent(QCloseEvent *event)
{
    // The workspace asks first, then closes us with its mark.
    if (property("closeConfirmed").toBool()) {
        setProperty("closeConfirmed", QVariant());
        event->accept();
        return;
    }
    event->ignore();
    m_workspace.closeWindow(this);
}
