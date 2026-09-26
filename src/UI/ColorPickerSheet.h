#pragma once
#include "Document/EditorSession.h"
#include "UI/FloatingPanel.h"
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>
#include <array>
#include <functional>

// A picker entry commits on Return or leaving, as Swift's.
class PickerField : public QLineEdit {
    Q_OBJECT
public:
    PickerField(std::function<void()> commit, std::function<void(int)> step, QWidget *parent);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    const std::function<void()> m_commit;
    const std::function<void(int)> m_step;
};

// Swift's ColorPickerSheet: field, hue strip, preview, entry.
class ColorPickerSheet : public QWidget {
    Q_OBJECT
public:
    ColorPickerSheet(EditorSession &session, std::function<void(bool)> finish, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    // A channel's entry: typed, stepped, clamped, as Swift's row.
    PickerField *channel(int index);
    // Swift writes the working hsb; here through the session.
    void change(const std::function<void(PickerHSB &)> &edit);
    // The field or strip's pick replaces the channels' typing.
    void pick(const std::function<void(PickerHSB &)> &edit);
    void setChannel(int channel, double value);
    void commitHex();
    void synchronize();

    EditorSession &m_session;
    QWidget *const m_field;
    QWidget *const m_hue;
    QWidget *const m_preview;
    // Made first: keys start at R, as the grid reads.
    const std::array<PickerField *, 3> m_channels;
    PickerField *const m_hex;
    QPushButton *const m_ok;
    QPushButton *const m_cancel;
};

// Swift's ColorPickerPanelController: the picker in its panel.
class ColorPickerPanelController {
public:
    explicit ColorPickerPanelController(QWidget &owner);
    void show(const ColorPickerState &state, EditorSession &session);
    void close();
    // Hands the keys back to the picker after a sample.
    static void refocus();
    static QString identifier() { return QStringLiteral("colorPickerPanel"); }

private:
    FloatingPanel m_panel;
};
