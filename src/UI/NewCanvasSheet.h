#pragma once
#include "Document/EditorSession.h"
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QToolButton>
#include <QWidget>
#include <functional>
#include <vector>

// New Canvas sizes in pixels, upright as held.
struct CanvasPreset {
    QString title;
    int width;
    int height;
    // Resolutions, Apple screens, then social formats; the menu divides them.
    static const std::vector<std::vector<CanvasPreset>> groups;
    static const std::vector<CanvasPreset> all;
};

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
    // Checks the preset the fields match, else Custom.
    void checkPreset();

    EditorSession &m_session;
    const std::function<void(int, int)> m_onCreate;
    QLineEdit *const m_width;
    QLineEdit *const m_height;
    QLabel *const m_note;
    QPushButton *const m_create;
    QToolButton *const m_presets;
    bool m_suggestedClipboardSize = false;
};
