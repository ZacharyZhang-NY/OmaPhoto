#pragma once
#include "Document/EditorSession.h"
#include <QLabel>
#include <QToolButton>

// One click adds a mask revealing the selection; Alt, hiding.
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

// While a mask shows alone: whose, and a way back.
class MaskAloneBadge : public QWidget {
    Q_OBJECT
public:
    explicit MaskAloneBadge(EditorSession &session, QWidget *parent = nullptr);
    void setLayerName(const QString &name);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QLabel *const m_name;
};
