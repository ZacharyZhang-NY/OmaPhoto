#pragma once
#include "Document/EditorSession.h"
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QWidget>
#include <functional>

// The welcome: a new canvas, a project or an image.
class NewCanvasSheet : public QWidget {
    Q_OBJECT
public:
    NewCanvasSheet(EditorSession &session, std::function<void(int, int)> onCreate, std::function<void()> onOpen, QWidget *parent = nullptr);

    // A copied picture's size, read from its header alone.
    static std::optional<QSize> clipboardDimensions(const QMimeData *clipboard);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void synchronize();
    void validate();
    void create();

    EditorSession &m_session;
    const std::function<void(int, int)> m_onCreate;
    QLineEdit *const m_width;
    QLineEdit *const m_height;
    QLabel *const m_note;
    QPushButton *const m_create;
    bool m_suggestedClipboardSize = false;
};
