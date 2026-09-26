#pragma once
#include "Document/EditorSession.h"
#include "UI/ToolHeaderStyle.h"
#include <QLineEdit>

// The Hand and Zoom tools' bar: Zoom's percentage field.
class NavigationToolHeader : public ToolHeaderBar {
    Q_OBJECT
public:
    explicit NavigationToolHeader(EditorSession &session, QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void synchronize();
    void syncZoom();
    void applyZoom();
    void step(double percent);

    EditorSession &m_session;
    QLineEdit *const m_zoom;
    QLabel *const m_unit;
    // What the field showed before the user typed.
    QString m_displayed;
};
