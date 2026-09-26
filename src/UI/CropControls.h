#pragma once
#include "UI/ToolHeaderStyle.h"

class EditorSession;
class QComboBox;
class QPushButton;

// Swift's CropControls: the ratio, the frame's size, cancel and apply.
class CropControls : public ToolHeaderBar {
    Q_OBJECT
public:
    explicit CropControls(EditorSession &session, QWidget *parent = nullptr);

private:
    void synchronize();

    EditorSession &m_session;
    QComboBox *const m_ratio;
    QLabel *const m_size;
    QPushButton *const m_cancel;
    QPushButton *const m_apply;
};
