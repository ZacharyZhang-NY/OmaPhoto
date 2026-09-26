#pragma once
#include "SelectionCanvasFixtures.h"
#include <QInputMethodEvent>

// Shared by the inline editor's drawing tests: red text, pixels.
namespace {
struct DrawnText : Canvas {
    DrawnText()
    {
        QObject::connect(&session, &EditorSession::changed, canvas, [this] { canvas->synchronizeDisplay(); });
        session.addBlankLayer();
        session.selectTool(NavigationTool::type);
        session.setPaletteColor(PaletteColor{1, 0, 0}, false);
    }
    InlineTextEditor &editor() const
    {
        if (!canvas->inlineTextEditor())
            throw std::runtime_error("no editor");
        return *canvas->inlineTextEditor();
    }
    // New red text, applied: its layer is active.
    QUuid text(QPointF at, const QString &content)
    {
        session.beginText(at, true);
        QTest::keyClicks(canvas, content);
        if (!session.finishText())
            throw std::runtime_error("the text was refused");
        return session.activeLayerID().value();
    }
    QImage grab() const { return canvas->grab().toImage().convertToFormat(QImage::Format_RGB32); }
    // A layer point on screen, through the editor's own box.
    QPoint onScreen(QPointF logical) const
    {
        const QPointF point = editor().boxTransform().map(logical);
        return QPoint(int(std::floor(point.x())), int(std::floor(point.y())));
    }
};

inline bool reddish(QColor colour)
{
    return colour.red() > 128 && colour.green() < 90 && colour.blue() < 90;
}

inline bool greenish(QColor colour)
{
    return colour.green() > 128 && colour.red() < 90 && colour.blue() < 90;
}

inline QRect where(const QImage &image, bool (*test)(QColor))
{
    QRect found;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (test(image.pixelColor(x, y)))
                found |= QRect(x, y, 1, 1);
        }
    }
    return found;
}
}
