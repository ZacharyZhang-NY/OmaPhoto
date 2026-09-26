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
    // New content and title; a shown panel keeps its place.
    void show(const QString &title, QWidget *content);
    // Hides the panel without reporting a close.
    void close();
    bool isVisible() const;
    // Its close button and Escape report here, as Cancel.
    std::function<void()> onClose;
    // Hands the keys back to a shown panel, after clicks.
    static void refocus(const QString &name);

private:
    friend class PanelWindow;
    void remember();
    void closed();

    const QString m_name;
    QWidget &m_owner;
    QPointer<QDialog> m_panel;
    QPointer<QWidget> m_content;
};
