#pragma once
#include <QPointer>
#include <QString>
#include <QWidget>
#include <functional>

class QDialog;

// Swift's FloatingPanelController: a movable, non-modal tool panel.
class FloatingPanel {
public:
    FloatingPanel(const QString &name, QWidget &owner);
    ~FloatingPanel();
    FloatingPanel(const FloatingPanel &) = delete;
    FloatingPanel &operator=(const FloatingPanel &) = delete;
    // New content and title; a dock slot takes it docked.
    void show(const QString &title, QWidget *content, QWidget *dock = nullptr);
    // Hides the panel without reporting a close.
    void close();
    bool isVisible() const;
    // Its close button and Escape report here, as Cancel.
    std::function<void()> onClose;
    // Swift's docked width: Camera Raw's grading wheels fit it.
    static constexpr int dockedWidth = 440;
    // Hands the keys back to a shown panel, after clicks.
    static void refocus(const QString &name);

private:
    friend class PanelWindow;
    void remember();
    void closed();
    void place(QWidget *content);
    void showDocked(const QString &title, QWidget *content, QWidget &dock);

    const QString m_name;
    QWidget &m_owner;
    QPointer<QDialog> m_panel;
    QPointer<QWidget> m_docked;
    QPointer<QWidget> m_content;
};
