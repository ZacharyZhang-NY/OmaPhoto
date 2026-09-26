#pragma once
#include "Document/TypeTool.h"
#include "UI/ToolHeaderStyle.h"
#include <QComboBox>
#include <QLineEdit>
#include <array>
#include <functional>

class EditorSession;
class QPushButton;
class QToolButton;
class SwatchButton;

// A text style number: typed at once, stepped by arrows.
class TextStyleField : public QLineEdit {
    Q_OBJECT
public:
    TextStyleField(std::function<QString()> shown, std::function<void(const QString &)> typed, std::function<void(double)> step,
                   QWidget *parent);
    // Shows the session's number unless being edited.
    void sync();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    const std::function<QString()> m_shown;
    const std::function<void(const QString &)> m_typed;
    const std::function<void(double)> m_step;
    // A menu or popup borrows the focus: the typing stays.
    bool m_borrowed = false;
};

// Swift's TypeFontPicker: the catalog loads when the menu opens.
class TypeFontPicker : public QComboBox {
    Q_OBJECT
public:
    explicit TypeFontPicker(QWidget *parent = nullptr);
    // Shows `name`; an open menu is left alone.
    void sync(const QString &name);
    void showPopup() override;

protected:
    // A long name is cut short, never widening the control.
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_loaded = false;
};

// Swift's TypeControls: font, size, alignment, spacing, Done.
class TypeControls : public ToolHeaderBar {
    Q_OBJECT
public:
    explicit TypeControls(EditorSession &session, QWidget *parent = nullptr);

protected:
    void changeEvent(QEvent *event) override;

private:
    QToolButton *alignment(TextAlignment value);
    void applyGlyphs();
    void synchronize();

    EditorSession &m_session;
    TypeFontPicker *const m_font;
    TextStyleField *const m_size;
    // Open text's colour, else the next text's: the foreground.
    SwatchButton *const m_colour;
    const std::array<QToolButton *, 3> m_alignments;
    TextStyleField *const m_tracking;
    TextStyleField *const m_leading;
    QPushButton *const m_cancel;
    QPushButton *const m_done;
    QPushButton *const m_edit;
};
