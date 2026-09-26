#pragma once
#include "Rendering/EditorCanvas.h"
#include "RenderFixtures.h"
#include "SessionFixtures.h"
#include <QtTest>

// A shown canvas and the events that drive it.
inline ImportedImage filled(int width, int height, QRgb premultiplied, const QString &name)
{
    const QImage image = solid(width, height, premultiplied);
    return ImportedImage(image, image, name);
}

// A sized canvas in a window, shown and active.
struct Shown {
    EditorSession session;
    QWidget window;
    CanvasView *const canvas;
    explicit Shown(std::optional<QSize> document = QSize(100, 100), QSize size = QSize(400, 300)) : canvas(new CanvasView(session, &window))
    {
        if (document)
            session.createDocument(document->width(), document->height());
        window.resize(size);
        canvas->setGeometry(QRect(QPoint(0, 0), size));
        window.show();
        if (!QTest::qWaitForWindowActive(&window))
            throw std::runtime_error("the window never became active");
    }
    // The viewport follows the widget after the event loop.
    void settle() { QTRY_COMPARE(session.viewport.viewSize, QSizeF(canvas->size())); }
    QSizeF documentSize() const { return session.document().value().size(); }
};

inline void drag(QWidget &widget, QPointF to)
{
    QMouseEvent event(QEvent::MouseMove, to, to, widget.mapToGlobal(to.toPoint()), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
}

inline void wheel(QWidget &widget, QPointF at, QPoint pixels, QPoint angle, Qt::KeyboardModifiers modifiers)
{
    QWheelEvent event(at, widget.mapToGlobal(at.toPoint()), pixels, angle, Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&widget, &event);
}
