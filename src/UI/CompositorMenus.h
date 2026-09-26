#pragma once
#include "Document/ProjectWorkspace.h"
#include "Rendering/EditorCanvas.h"
#include <QAction>
#include <QLineEdit>
#include <QMenuBar>
#include <QPointer>

// The menu bar: Swift's commands whose session functions exist.
class CompositorMenus : public QObject {
    Q_OBJECT
public:
    CompositorMenus(ProjectWorkspace &workspace, QMenuBar &bar, QWidget &window);

    QAction *action(const QString &name) const;

private:
    void synchronize();
    void focusMoved(QWidget *from, QWidget *to);
    // The open text in the focused canvas, Swift's text view.
    InlineTextEditor *typedText() const { return m_canvas ? m_canvas->inlineTextEditor() : nullptr; }
    void watchFront();
    EditorSession &session() const { return m_workspace.current().session; }
    ProjectController &projects() const { return m_workspace.current().controller; }
    QAction *add(QMenu *menu, const QString &name, const QString &text, const QKeySequence &shortcut, const std::function<void()> &run);

    ProjectWorkspace &m_workspace;
    QWidget &m_window;
    QMetaObject::Connection m_sessionWatch;
    std::optional<QUuid> m_watchedTab;
    // The field whose undo the Edit entries drive.
    QPointer<QLineEdit> m_field;
    QPointer<CanvasView> m_canvas;
};
