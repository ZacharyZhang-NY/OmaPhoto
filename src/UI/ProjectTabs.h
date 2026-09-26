#pragma once
#include "Document/ProjectWorkspace.h"
#include <QScrollArea>
#include <QToolButton>
#include <QWidget>

// One tab: its title, the unsaved dot, its close button.
class ProjectTabButton : public QWidget {
    Q_OBJECT
public:
    ProjectTabButton(ProjectWorkspace &workspace, std::shared_ptr<ProjectTab> tab, QWidget *parent = nullptr);

    const std::shared_ptr<ProjectTab> tab;
    // What the session and the workspace hold, shown.
    void synchronize();
    bool isActive() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    bool acceptsDrop(const QMimeData &data) const;
    void setTargeted(bool targeted);

    ProjectWorkspace &m_workspace;
    QToolButton *const m_select;
    QToolButton *const m_close;
    bool m_targeted = false;
};

// Swift's NewProjectDropTarget: the New canvas button takes drops.
class NewCanvasButton : public QToolButton {
    Q_OBJECT
public:
    explicit NewCanvasButton(ProjectWorkspace &workspace, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    bool acceptsDrop(const QMimeData &data) const;
    void setTargeted(bool targeted);

    ProjectWorkspace &m_workspace;
    bool m_targeted = false;
};

// The row of tabs, one a project, scrolling when long.
class ProjectTabStrip : public QScrollArea {
    Q_OBJECT
public:
    explicit ProjectTabStrip(ProjectWorkspace &workspace, QWidget *parent = nullptr);

    QList<ProjectTabButton *> buttons() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void synchronize();
    void showFront();

    ProjectWorkspace &m_workspace;
    QWidget *const m_row;
};
