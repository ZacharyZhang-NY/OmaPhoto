#pragma once
#include "Document/EditorSession.h"
#include <QToolButton>

// One click adds a mask: white, or the selection black.
class LayerMaskMenu : public QToolButton {
    Q_OBJECT
public:
    explicit LayerMaskMenu(EditorSession &session, QWidget *parent = nullptr);

protected:
    void changeEvent(QEvent *event) override;

private:
    void synchronize();

    EditorSession &m_session;
};
