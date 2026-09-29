#pragma once
#include "Document/TypeTool.h"
#include "Rendering/TextLayout.h"
#include <QInputMethodEvent>
#include <QCursor>
#include <QTimer>
#include <QTransform>
#include <QVariant>
#include <memory>
#include <optional>
#include <vector>

class EditorSession;
class QKeyEvent;
class QPainter;
class CanvasView;

// Swift's InlineTextEditor and its text view, painted by the canvas.
class InlineTextEditor {
public:
    InlineTextEditor(EditorSession &session, CanvasView &canvas);
    // Swift's synchronize: size, placement, content and style.
    void synchronize(const TextDraft &draft);
    QUuid draftID() const { return m_draftID; }
    // The text view's bounds, in layer pixels.
    QSizeF logicalSize() const { return m_logicalSize; }
    // Point text grows as typed, so this is what shows.
    LayerTransform shownTransform() const { return m_shownTransform; }
    double handleSize() const { return m_handleSize; }
    // Layer pixels to view points, without the layer's flips.
    QTransform boxTransform() const;
    // The text surface's own mapping, flipped with its layer.
    QTransform textTransform() const;
    void draw(QPainter &painter) const;
    // Where the editor draws, in view points.
    QRectF drawnRect() const;
    // The text view's keys; false leaves a key unused.
    bool keyPress(const QKeyEvent &event);
    // Keys the editor takes ahead of the menus' shortcuts.
    static bool claims(const QKeyEvent &event);
    void inputMethod(const QInputMethodEvent &event);
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const;
    // The caret shows, blinking, while the canvas has focus.
    void setFocused(bool focused);
    // The pointer: handles resize, the text view selects.
    bool press(QPointF point, Qt::KeyboardModifiers modifiers, int clicks);
    // NSTextView's click, wherever it lands, as the canvas forwards it.
    void pressText(QPointF point, Qt::KeyboardModifiers modifiers, int clicks);
    bool drag(QPointF point);
    void release();
    // A handle's cursor under the point, turned with the box.
    std::optional<QCursor> cursorAt(QPointF point) const;
    // NSText's actions, as the Edit menu sends them.
    void undo();
    void redo();
    void cut();
    void copy() const;
    void paste();
    void selectAll();
    // The text view's selection: an anchor and the caret.
    int anchor() const { return m_anchor; }
    int caretPosition() const { return m_position; }
    std::optional<TextRange> marked() const { return m_composing ? std::optional(m_composing->marked) : std::nullopt; }
    bool caretShown() const { return m_focused && m_caretShown; }

private:
    struct Snapshot {
        QString content;
        int anchor;
        int position;
    };
    // An input method's preedit, which Qt keeps out of history.
    struct Composing {
        TextRange marked;
        // Qt keeps a selection beside the preedit, without it.
        int anchor;
        // Formats count from the preedit's start.
        QList<QTextLayout::FormatRange> formats = {};
        bool caretHidden = false;
        std::optional<QColor> caretColour = std::nullopt;
        // The text before the preedit, for its landing step.
        Snapshot before;
    };
    // A handle's drag: the draft and box it began from.
    struct Resize {
        int handle;
        TextDraft draft;
        LayerTransform transform;
        QPointF start;
    };
    // A click's unit, kept for Shift while its selection stands.
    struct Tracking {
        TextRange origin;
        int clicks;
        int anchor;
        int position;
    };
    // Runs of these coalesce into one undo step, as NSTextView's.
    enum class Edit { typing, deleting };
    const LayerTextStyle &style() const { return m_shownStyle.value(); }
    static TextRange spanning(int anchor, int position);
    TextRange selection() const;
    void replace(TextRange range, const QString &text, std::optional<Edit> edit);
    void publish(const QString &content);
    void moveTo(int position, bool extend, bool keepGoal = false);
    void restore(std::vector<Snapshot> &from, std::vector<Snapshot> &to);
    void changeSpacing(int key, double step);
    bool move(const QKeyEvent &event);
    bool rightToLeft() const;
    static bool separates(QChar character);
    std::optional<int> handleAt(QPointF point) const;
    int positionAt(QPointF point, bool onCharacter) const;
    TextRange unitAt(int position, int clicks) const;
    void land();
    void beginResize(int handle, QPointF point);
    void resizeTo(QPointF point);
    void compose(const QString &preedit, const QList<QInputMethodEvent::Attribute> &attributes, const Snapshot &before, int anchor, QString &text,
                 int cursor);
    TextRange shownSelection() const;
    Snapshot plain() const;
    void change(const QString &text);
    void settle(QString text, int anchor, int cursor);
    bool erase(const QKeyEvent &event);
    int nextCharacter(int position) const;
    int previousCharacter(int position) const;
    int nextWordEnd(int position) const;
    int previousWordStart(int position) const;
    int lineBound(bool end) const;
    int verticalTarget(int direction);
    void showCaret();
    void repaint() const;
    void updateInputMethod() const;

    EditorSession &m_session;
    CanvasView &m_canvas;
    QUuid m_draftID;
    std::optional<LayerTextStyle> m_shownStyle;
    QSizeF m_logicalSize{360, 160};
    // Point text's measure, kept while its style holds.
    std::optional<LayerTextStyle> m_measuredStyle;
    QSizeF m_measuredSize;
    LayerTransform m_shownTransform;
    double m_handleSize = 6;
    std::unique_ptr<TextLines> m_lines;
    int m_anchor = 0;
    int m_position = 0;
    std::optional<Composing> m_composing;
    // Up and Down keep the caret's first x, as NSTextView.
    std::optional<double> m_goalX;
    std::vector<Snapshot> m_undo;
    std::vector<Snapshot> m_redo;
    std::optional<Edit> m_lastEdit;
    std::optional<Resize> m_resize;
    std::optional<Tracking> m_tracking;
    // The button that began a selection is still down.
    bool m_selecting = false;
    QTimer m_blink;
    bool m_focused = false;
    bool m_caretShown = true;
};
