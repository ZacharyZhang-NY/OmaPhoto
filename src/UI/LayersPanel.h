#pragma once
#include "Document/EditorSession.h"
#include <QLabel>
#include <QStackedWidget>
#include <QToolButton>
#include <QWidget>

class NativeLayerList;

// The layers panel: heading, appearance, the rows, the footer.
class LayersPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayersPanel(EditorSession &session, QWidget *parent = nullptr);
    // Dragging the panel's left edge sets it, within these.
    static constexpr double minimumWidth = 202;
    static constexpr double maximumWidth = 352;
    static constexpr double defaultWidth = 252;

    NativeLayerList &list() const { return *m_list; }

protected:
    void changeEvent(QEvent *event) override;

private:
    void synchronize();
    void applyIcons();
    QToolButton *footerButton(const QString &name, const QString &label, const QString &tip, const std::function<void()> &run);

    EditorSession &m_session;
    QLabel *const m_count;
    QStackedWidget *const m_body;
    NativeLayerList *const m_list;
    QLabel *const m_emptyHint;
    QLabel *m_emptyIcon = nullptr;
    QToolButton *m_addBlankLayer = nullptr;
    QToolButton *m_group = nullptr;
    QToolButton *m_effects = nullptr;
    QToolButton *m_adjustments = nullptr;
    QToolButton *m_delete = nullptr;
    // The request Swift's .task(id:) last began.
    std::optional<QUuid> m_adjustmentTask;
};
