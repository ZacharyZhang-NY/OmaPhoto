#pragma once
#include "CanvasFixtures.h"
#include "SelectionFixtures.h"
#include <QPaintEvent>

// Shared by the selection canvas tests: canvas, events, spy.
inline QImage image(const QCursor &cursor)
{
    return cursor.pixmap().toImage();
}

// The document fills the canvas: view points are pixels.
struct Canvas : Shown {
    Canvas() : Shown(QSize(400, 300), QSize(400, 300))
    {
        settle();
        session.zoom(1);
        session.selectTool(NavigationTool::marquee);
        canvas->synchronizeDisplay();
    }
    void press(QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier) { QTest::mousePress(canvas, Qt::LeftButton, modifiers, at.toPoint()); }
    void move(QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QMouseEvent event(QEvent::MouseMove, to, to, canvas->mapToGlobal(to.toPoint()), Qt::NoButton, Qt::LeftButton, modifiers);
        QApplication::sendEvent(canvas, &event);
    }
    void hover(QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QMouseEvent event(QEvent::MouseMove, at, at, canvas->mapToGlobal(at.toPoint()), Qt::NoButton, Qt::NoButton, modifiers);
        QApplication::sendEvent(canvas, &event);
    }
    void release(QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier) { QTest::mouseRelease(canvas, Qt::LeftButton, modifiers, at.toPoint()); }
    void click(QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        press(at, modifiers);
        release(at, modifiers);
    }
    void drag(QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        press(from, modifiers);
        move(to, modifiers);
        release(to, modifiers);
    }
    bool shows(const QCursor &cursor) const { return image(canvas->cursor()) == image(cursor); }
    int coverage(int x, int y) const { return ::coverage(session, x, y); }
};

// Counts repaints and unites what they covered.
class PaintSpy : public QObject {
public:
    explicit PaintSpy(QWidget &widget) { widget.installEventFilter(this); }
    QRect painted;
    int count = 0;

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::Paint) {
            painted |= static_cast<QPaintEvent *>(event)->region().boundingRect();
            ++count;
        }
        return false;
    }
};
