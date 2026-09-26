#pragma once
#include "SelectionCanvasFixtures.h"
#include "UI/ColorPaletteControls.h"
#include <QApplication>
#include <QLabel>
#include <QPushButton>

// Shared by the palette's and the picker sheet's tests.
struct Palette {
    EditorSession session;
    QWidget window;
    ColorPaletteControls *const controls;
    Palette() : controls((session.createDocument(40, 40, true), new ColorPaletteControls(session, &window)))
    {
        window.resize(300, 200);
        controls->move(20, 20);
        window.show();
        if (!QTest::qWaitForWindowActive(&window))
            throw std::runtime_error("the window never became active");
    }
    QAbstractButton &button(const char *name) const
    {
        auto *found = controls->findChild<QAbstractButton *>(QString::fromLatin1(name));
        if (!found)
            throw std::runtime_error(name);
        return *found;
    }
};

inline QWidget *panel()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == ColorPickerPanelController::identifier() && widget->isVisible())
            return widget;
    }
    return nullptr;
}

template <typename Widget> Widget &inPanel(const char *name)
{
    auto *found = panel()->findChild<Widget *>(QString::fromLatin1(name));
    if (!found)
        throw std::runtime_error(name);
    return *found;
}

inline QColor middle(QWidget &widget)
{
    return widget.grab().toImage().pixelColor(widget.width() / 2, widget.height() / 2);
}

inline void type(QWidget &field, const QString &text)
{
    field.setFocus();
    QTest::keyClick(&field, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(&field, Qt::Key_Backspace);
    QTest::keyClicks(&field, text);
}

// Opens the foreground's picker, the panel holding the keys.
inline void openForeground(Palette &shown)
{
    // Window managers hand the keys back once a panel closes.
    shown.window.activateWindow();
    QTRY_COMPARE(QApplication::activeWindow(), &shown.window);
    QTest::mouseClick(&shown.button("foregroundSwatch"), Qt::LeftButton);
    // Tool windows read active with their parent: wait for keys.
    QTRY_COMPARE(QApplication::activeWindow(), panel());
}
