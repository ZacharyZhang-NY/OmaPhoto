#pragma once
#include "CanvasFixtures.h"

// Shared by the Move tool's canvas tests: a red square, presses.
// The document fills the canvas: view points are pixels.
struct Canvas : Shown {
    QUuid red;
    Canvas() : Shown(QSize(400, 300), QSize(400, 300))
    {
        settle();
        session.zoom(1);
        session.insert(filled(100, 100, qRgba(255, 0, 0, 255), "Red"));
        red = session.activeLayerID().value();
        session.selectTool(NavigationTool::move);
        canvas->synchronizeDisplay();
        if (session.viewport.viewPoint(QPointF(0, 0), documentSize()) != QPointF(0, 0))
            throw std::runtime_error("the document does not fill the canvas");
    }
    QPointF origin() const { return layerWith(session, red).transform.origin; }
    void press(QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier) { QTest::mousePress(canvas, Qt::LeftButton, modifiers, at.toPoint()); }
    void move(QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QMouseEvent event(QEvent::MouseMove, to, to, canvas->mapToGlobal(to.toPoint()), Qt::NoButton, Qt::LeftButton, modifiers);
        QApplication::sendEvent(canvas, &event);
    }
    void release(QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier) { QTest::mouseRelease(canvas, Qt::LeftButton, modifiers, at.toPoint()); }
    void drag(QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        press(from, modifiers);
        move((from + to) / 2, modifiers);
        move(to, modifiers);
        release(to, modifiers);
    }
    void click(QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        press(at, modifiers);
        release(at, modifiers);
    }
};
