#pragma once
#include "SelectionFixtures.h"
#include "SessionFixtures.h"
#include <QLabel>
#include <QLineEdit>
#include <QtTest>

// Shared by the scrubbable label tests: finding, dragging, typing.

// The scrubbing labels saying `text`, left to right.
inline QLabel &scrubbed(QWidget &root, const QString &text, int index = 0)
{
    std::vector<QLabel *> found;
    for (QLabel *label : root.findChildren<QLabel *>())
        if (label->text() == text && label->testAttribute(Qt::WA_SetCursor) && label->cursor().shape() == Qt::SizeHorCursor)
            found.push_back(label);
    std::sort(found.begin(), found.end(), [&root](QLabel *a, QLabel *b) { return a->mapTo(&root, QPoint()).x() < b->mapTo(&root, QPoint()).x(); });
    if (size_t(index) >= found.size())
        throw std::runtime_error("no scrubbing label " + text.toStdString());
    return *found[size_t(index)];
}

inline void send(QWidget &widget, QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, at, widget.mapToGlobal(at), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
}

// Presses, drags `dx` points sideways, and lets go.
inline void drag(QWidget &label, double dx, bool release = true)
{
    send(label, QEvent::MouseButtonPress, QPointF(2, 2), Qt::LeftButton, Qt::LeftButton);
    send(label, QEvent::MouseMove, QPointF(2 + dx, 2), Qt::NoButton, Qt::LeftButton);
    if (release)
        send(label, QEvent::MouseButtonRelease, QPointF(2 + dx, 2), Qt::LeftButton, Qt::NoButton);
}

// Types 7, which applies, then scrubs, holding the press.
inline QString scrubOverTyping(QLineEdit &field, QWidget &label, double dx)
{
    field.setFocus();
    field.selectAll();
    QTest::keyClicks(&field, QStringLiteral("7"));
    if (!field.hasFocus())
        throw std::runtime_error("the field lost its focus");
    drag(label, dx, false);
    const QString during = field.text();
    send(label, QEvent::MouseButtonRelease, QPointF(2 + dx, 2), Qt::LeftButton, Qt::NoButton);
    if (field.text() != during || !field.hasFocus())
        throw std::runtime_error("the release changed the field");
    return during;
}

// Shown in an active window, so fields take the focus.
inline void showActive(QWidget &widget)
{
    widget.show();
    widget.activateWindow();
    if (!QTest::qWaitForWindowActive(&widget))
        throw std::runtime_error("the bar never became active");
}

template <typename Widget> inline Widget &find(QWidget &root, const char *name)
{
    auto *found = root.findChild<Widget *>(QString::fromUtf8(name));
    if (!found)
        throw std::runtime_error(name);
    return *found;
}

inline ImportedImage pixels(int width, int height)
{
    QImage image(width, height, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::white);
    return ImportedImage(image, image, "Pixels");
}
