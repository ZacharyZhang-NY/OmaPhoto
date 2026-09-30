#include "Document/EditorSession.h"
#include "Rendering/InlineTextEditor.h"
#include "Rendering/TextLayout.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <algorithm>
#include <cstdlib>

void InlineTextEditor::cut()
{
    const Snapshot around = plain();
    if (spanning(around.anchor, around.position).isEmpty())
        return;
    copy();
    change(QString());
}

void InlineTextEditor::copy() const
{
    const Snapshot around = plain();
    const TextRange range = spanning(around.anchor, around.position);
    if (!range.isEmpty())
        QGuiApplication::clipboard()->setText(around.content.mid(range.start, range.end - range.start));
}

// Swift's paste is pasteAsPlainText.
void InlineTextEditor::paste()
{
    const QString text = QGuiApplication::clipboard()->text();
    if (!text.isEmpty())
        change(text);
}

void InlineTextEditor::selectAll()
{
    // Around a preedit, which follows the cursor to the end.
    if (m_composing) {
        m_lastEdit.reset();
        const QString text = plain().content;
        settle(text, 0, int(text.size()), {{m_composing->marked, 0, true, false}});
        return;
    }
    m_anchor = 0;
    moveTo(int(style().content.size()), true);
}

// The text around a preedit, which Qt's fields keep apart.
InlineTextEditor::Snapshot InlineTextEditor::plain() const
{
    if (!m_composing)
        return {style().content, m_anchor, m_position};
    const TextRange marked = m_composing->marked;
    return {QString(style().content).remove(marked.start, marked.end - marked.start), m_composing->anchor, marked.start};
}

// A clipboard command replaces the selection around a preedit.
void InlineTextEditor::change(const QString &text)
{
    const Snapshot around = plain();
    const TextRange range = spanning(around.anchor, around.position);
    if (style().content.size() - (range.end - range.start) + text.size() > 100'000)
        return;
    m_undo.push_back(around);
    m_redo.clear();
    m_lastEdit.reset();
    const int cursor = range.start + int(text.size());
    std::vector<Step> steps;
    if (m_composing)
        steps.push_back({m_composing->marked, 0, true, false});
    steps.push_back({range, int(text.size())});
    settle(QString(around.content).replace(range.start, range.end - range.start, text), cursor, cursor, steps);
}

// The preedit returns to the cursor, as Qt shows it.
void InlineTextEditor::settle(QString text, int anchor, int cursor, std::vector<Step> steps)
{
    if (m_composing) {
        const TextRange marked = m_composing->marked;
        const QString preedit = style().content.mid(marked.start, marked.end - marked.start);
        const int caret = m_position - marked.start;
        m_composing->before = {text, anchor, cursor};
        m_composing->anchor = anchor;
        m_composing->marked = {cursor, cursor + int(preedit.size())};
        text.insert(cursor, preedit);
        steps.push_back({{cursor, cursor}, int(preedit.size()), true, false});
        anchor = cursor = cursor + caret;
    }
    m_anchor = anchor;
    m_position = cursor;
    publish(text, steps);
}

// Marked text sits in the content, as in NSTextView's storage.
void InlineTextEditor::inputMethod(const QInputMethodEvent &event)
{
    const QString commit = event.commitString(), preedit = event.preeditString();
    const QList<QInputMethodEvent::Attribute> attributes = event.attributes();
    const auto selecting = std::find_if(attributes.begin(), attributes.end(), [](const auto &attribute) { return attribute.type == QInputMethodEvent::Selection; });
    // Qt's steps, on the text without the preedit.
    QString text = style().content, shown;
    int cursor = m_position, anchor = m_anchor;
    const std::optional<TextRange> oldMarked = m_composing ? std::optional(m_composing->marked) : std::nullopt;
    // The preedit's going moves colours, never history.
    std::vector<Step> steps;
    if (m_composing) {
        const TextRange marked = m_composing->marked;
        shown = text.mid(marked.start, marked.end - marked.start);
        text.remove(marked.start, marked.end - marked.start);
        steps.push_back({marked, 0, true, false});
        cursor = marked.start;
        anchor = m_composing->anchor;
    }
    const bool input = !commit.isEmpty() || preedit != shown || event.replacementLength() > 0;
    if (!input && !m_composing && selecting == attributes.end())
        return;
    // Input takes the selection first, as Qt's fields.
    const TextRange selected{std::min(anchor, cursor), std::max(anchor, cursor)};
    const bool takes = input && !selected.isEmpty();
    if (takes) {
        text.remove(selected.start, selected.end - selected.start);
        steps.push_back({selected, 0});
        cursor = selected.start;
    }
    // Where Qt leaves the cursor when nothing commits.
    int after = cursor;
    if (event.replacementStart() <= 0)
        after += int(commit.size()) - std::min(-event.replacementStart(), event.replacementLength());
    const int start = std::clamp(cursor + event.replacementStart(), 0, int(text.size()));
    const int end = event.replacementLength() > 0 && start + event.replacementLength() <= text.size() ? start + event.replacementLength() : start;
    if (text.size() - (end - start) + commit.size() + preedit.size() > 100'000)
        return;
    // A preedit records nothing; what lands makes a typing step.
    const Snapshot before = m_composing ? m_composing->before : Snapshot{style().content, m_anchor, m_position};
    const bool lands = takes || end > start || !commit.isEmpty();
    if (lands) {
        if (m_lastEdit != Edit::typing)
            m_undo.push_back(before);
        m_redo.clear();
        m_lastEdit = Edit::typing;
    }
    text.replace(start, end - start, commit);
    // Over the selection taken, one replacement, as NSTextView's.
    if (takes && start == selected.start && end == start)
        steps.back().length = int(commit.size());
    else if (end > start || !commit.isEmpty())
        steps.push_back({{start, end}, int(commit.size())});
    cursor = commit.isEmpty() ? std::clamp(after, 0, int(text.size())) : start + int(commit.size());
    if (input)
        anchor = cursor;
    // The input method's own move ends a typing run.
    if (selecting != attributes.end()) {
        cursor = std::clamp(selecting->start + selecting->length, 0, int(text.size()));
        anchor = selecting->length ? std::clamp(selecting->start, 0, int(text.size())) : cursor;
        m_lastEdit.reset();
    }
    m_composing.reset();
    if (preedit.isEmpty()) {
        m_anchor = anchor;
        m_position = cursor;
    } else {
        compose(preedit, attributes, lands ? Snapshot{text, anchor, cursor} : before, anchor, text, steps, cursor);
        // A lone preedit over the selection replaces it whole.
        if (takes && commit.isEmpty() && start == end && cursor == selected.start) {
            steps[steps.size() - 2].runs = false;
            steps.back() = {selected, int(preedit.size()), true, false};
        }
    }
    // Swift replaces marked text whole: its colour carries on.
    if (oldMarked && !takes && start == end && start == oldMarked->start && (preedit.isEmpty() || cursor == start + int(commit.size()))) {
        steps.front().length = int(commit.size() + preedit.size());
        for (size_t index = 1; index < steps.size(); ++index)
            steps[index].runs = false;
    }
    publish(text, steps);
}

// The preedit goes in at the cursor, with Qt's attributes.
void InlineTextEditor::compose(const QString &preedit, const QList<QInputMethodEvent::Attribute> &attributes, const Snapshot &before, int anchor,
                               QString &text, std::vector<Step> &steps, int cursor)
{
    const int length = int(preedit.size());
    Composing composing{.marked = {cursor, cursor + length}, .anchor = anchor, .before = before};
    int caret = length;
    for (const QInputMethodEvent::Attribute &attribute : attributes) {
        const int start = std::clamp(attribute.start, 0, length);
        if (attribute.type == QInputMethodEvent::Cursor) {
            caret = start;
            composing.caretHidden = attribute.length == 0;
            if (attribute.value.typeId() == QMetaType::QColor)
                composing.caretColour = attribute.value.value<QColor>();
        } else if (attribute.type == QInputMethodEvent::TextFormat) {
            // Qt's fields pass over a format that is not one.
            const QTextCharFormat format = qvariant_cast<QTextFormat>(attribute.value).toCharFormat();
            const int end = std::clamp(attribute.start + attribute.length, start, length);
            if (format.isValid())
                composing.formats << QTextLayout::FormatRange{start, end - start, format};
        }
    }
    text.insert(cursor, preedit);
    steps.push_back({{cursor, cursor}, length, true, false});
    m_anchor = m_position = cursor + caret;
    m_composing = composing;
}

// The selection shown: the input method's keeps beside the preedit.
TextRange InlineTextEditor::shownSelection() const
{
    if (!m_composing)
        return selection();
    const TextRange marked = m_composing->marked;
    const int length = marked.end - marked.start;
    const TextRange range = spanning(m_composing->anchor, marked.start);
    return {range.start < marked.start ? range.start : range.start + length, range.end <= marked.start ? range.end : range.end + length};
}

QVariant InlineTextEditor::inputMethodQuery(Qt::InputMethodQuery query) const
{
    const QString &content = style().content;
    const std::optional<TextRange> marked = this->marked();
    const int cursor = marked ? marked->start : m_position, anchor = m_composing ? m_composing->anchor : m_anchor;
    const QString surrounding = marked ? QString(content).remove(marked->start, marked->end - marked->start) : content;
    switch (query) {
    case Qt::ImEnabled:
        return true;
    case Qt::ImHints:
        return int(Qt::ImhMultiLine);
    case Qt::ImFont:
        return TextLayout::font(style());
    case Qt::ImCursorRectangle: {
        const double padding = LayerTextStyle::padding;
        const QRectF caret = m_lines->caret(m_position).value_or(QRectF(0, 0, 1, style().lineHeight())).translated(padding, padding);
        return textTransform().mapRect(caret);
    }
    case Qt::ImSurroundingText:
        return surrounding;
    case Qt::ImCursorPosition:
        return cursor;
    case Qt::ImAnchorPosition:
        return anchor;
    case Qt::ImCurrentSelection:
        return surrounding.mid(spanning(anchor, cursor).start, std::abs(anchor - cursor));
    default:
        return QVariant();
    }
}
