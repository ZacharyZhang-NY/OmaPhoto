#pragma once
#include "SelectionCanvasFixtures.h"
#include <QInputMethodEvent>

// Shared by the inline editor's key and input tests.
namespace {
// A canvas that follows its session, as ContentView makes it.
struct TextCanvas : Canvas {
    TextCanvas()
    {
        QObject::connect(&session, &EditorSession::changed, canvas, [this] { canvas->synchronizeDisplay(); });
        session.addBlankLayer();
        session.selectTool(NavigationTool::type);
        session.beginText(QPointF(20, 30), true);
    }
    InlineTextEditor &editor() const
    {
        if (!canvas->inlineTextEditor())
            throw std::runtime_error("no editor");
        return *canvas->inlineTextEditor();
    }
    QString content() const { return session.textDraft().value().style.content; }
    int caret() const { return editor().caretPosition(); }
    void key(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) { QTest::keyClick(canvas, key, modifiers); }
    void type(const QString &text) { QTest::keyClicks(canvas, text); }
    // A key whose text is more than one UTF-16 unit.
    void typeText(const QString &text, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QKeyEvent press(QEvent::KeyPress, 0, modifiers, text);
        QApplication::sendEvent(canvas, &press);
    }
    // Input methods: a preedit, or a commit replacing text.
    void compose(const QString &preedit, const QList<QInputMethodEvent::Attribute> &attributes = {})
    {
        QInputMethodEvent event(preedit, attributes);
        QApplication::sendEvent(canvas, &event);
    }
    void commit(const QString &text, int from = 0, int length = 0)
    {
        QInputMethodEvent event;
        event.setCommitString(text, from, length);
        QApplication::sendEvent(canvas, &event);
    }
    TextRange selection() const { return {std::min(editor().anchor(), editor().caretPosition()), std::max(editor().anchor(), editor().caretPosition())}; }
};
}
