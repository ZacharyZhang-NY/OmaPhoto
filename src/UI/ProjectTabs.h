#pragma once
#include "Document/ProjectWorkspace.h"
#include "UI/ProjectTabLayout.h"
#include <QToolButton>
#include <QWidget>

// Swift's TabDragPhase: a reorder drag goes on, or ends.
enum class TabDragPhase { changed, ended };

// One tab: its title, the unsaved dot, its close button.
class ProjectTabButton : public QWidget {
    Q_OBJECT
public:
    ProjectTabButton(ProjectWorkspace &workspace, std::shared_ptr<ProjectTab> tab, std::function<void(TabDragPhase, double)> onReorder,
                     QWidget *parent = nullptr);

    const std::shared_ptr<ProjectTab> tab;
    // What the session and the workspace hold, shown.
    void synchronize();
    bool isActive() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    bool acceptsDrop(const QMimeData &data) const;
    void setTargeted(bool targeted);

    ProjectWorkspace &m_workspace;
    const std::function<void(TabDragPhase, double)> m_onReorder;
    QToolButton *const m_select;
    QToolButton *const m_close;
    bool m_targeted = false;
    // Swift's DragGesture: the press, in the window's space.
    std::optional<QPointF> m_press;
    bool m_dragging = false;
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

// Swift's OverflowTabsPill: the tabs that do not fit, a menu.
class OverflowTabsPill : public QToolButton {
    Q_OBJECT
public:
    explicit OverflowTabsPill(ProjectWorkspace &workspace, QWidget *parent = nullptr);

    // Swift's projectTabOverflowPillWidth.
    static double pillWidth(int hiddenCount);
    void setHiddenIDs(std::vector<QUuid> hiddenIDs);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void showMenu();

    ProjectWorkspace &m_workspace;
    std::vector<QUuid> m_hiddenIDs;
};

// The row of tabs; the rest gather in a menu.
class ProjectTabStrip : public QWidget {
    Q_OBJECT
public:
    explicit ProjectTabStrip(ProjectWorkspace &workspace, QWidget *parent = nullptr);

    QList<ProjectTabButton *> buttons() const;
    OverflowTabsPill *pill() const { return m_pill; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    // Swift's TabReorderState, taken once as the drag starts.
    struct Reorder {
        QUuid id;
        std::vector<QUuid> others;
        std::map<QUuid, double> widths;
        std::map<QUuid, double> compactedX;
        double startX;
        double originX;
        double translation = 0;
        int targetIndex = 0;
        int nearestSlot() const;
    };

    void synchronize();
    std::map<QUuid, double> widths() const;
    ProjectTabOverflow overflow() const;
    // Places the pill and the tabs; others slide when `animated`.
    void layOut(bool animated = false);
    double renderX(const ProjectTabSlot &slot, double contentWidth) const;
    void handleReorder(QUuid id, TabDragPhase phase, double translation);
    Reorder makeReorder(QUuid id) const;
    void commitReorder(const Reorder &state);

    ProjectWorkspace &m_workspace;
    OverflowTabsPill *const m_pill;
    std::vector<ProjectTabButton *> m_buttons;
    std::optional<Reorder> m_reorder;
};
