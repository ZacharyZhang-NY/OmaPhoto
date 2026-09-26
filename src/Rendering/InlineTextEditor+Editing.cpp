#include "Document/EditorSession.h"
#include "Rendering/InlineTextEditor.h"
#include "Rendering/TextLayout.h"
#include <QKeyEvent>
#include <QTextBoundaryFinder>
#include <algorithm>
#include <array>

namespace {
bool isArrow(int key)
{
    return key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down;
}

// What a text view types, as Qt's text fields judge.
bool isTyping(const QKeyEvent &event)
{
    const QString text = event.text();
    if (text.isEmpty())
        return false;
    const QChar first = text.at(0);
    if (first.category() == QChar::Other_Format)
        return true;
    // Ctrl alone is a command; AltGr (Ctrl with Alt) types.
    if (event.modifiers() == Qt::ControlModifier || event.modifiers() == (Qt::ShiftModifier | Qt::ControlModifier))
        return false;
    return first.isPrint() || first.category() == QChar::Other_PrivateUse || (first.isHighSurrogate() && text.size() > 1 && text.at(1).isLowSurrogate());
}
}

bool InlineTextEditor::claims(const QKeyEvent &event)
{
    // The text view's shortcuts, and the fills' text meanings.
    constexpr std::array standard{QKeySequence::SelectAll, QKeySequence::Copy, QKeySequence::Cut, QKeySequence::Paste, QKeySequence::Undo, QKeySequence::Redo};
    if (std::any_of(standard.begin(), standard.end(), [&](QKeySequence::StandardKey key) { return event.matches(key); }))
        return true;
    return event.key() == Qt::Key_Backspace;
}

bool InlineTextEditor::keyPress(const QKeyEvent &event)
{
    const Qt::KeyboardModifiers modifiers = event.modifiers();
    const int key = event.key();
    const bool enter = key == Qt::Key_Return || key == Qt::Key_Enter;
    if (key == Qt::Key_Escape) {
        m_session.cancelText();
    } else if (modifiers.testFlag(Qt::AltModifier) && isArrow(key)) {
        // Alt with the arrows sets spacing, as Swift's Option.
        changeSpacing(key, modifiers.testFlag(Qt::ShiftModifier) ? 10 : 1);
    } else if (enter && modifiers.testFlag(Qt::ControlModifier)) {
        m_session.finishText();
    } else if (erase(event)) {
        // Deletions come first: X11 binds Alt+Backspace to Undo.
    } else if (event.matches(QKeySequence::Undo)) {
        undo();
    } else if (event.matches(QKeySequence::Redo)) {
        redo();
    } else if (event.matches(QKeySequence::Cut)) {
        cut();
    } else if (event.matches(QKeySequence::Copy)) {
        copy();
    } else if (event.matches(QKeySequence::Paste)) {
        paste();
    } else if (event.matches(QKeySequence::SelectAll)) {
        selectAll();
    } else if (move(event)) {
        return true;
    } else if (enter) {
        replace(selection(), QStringLiteral("\n"), Edit::typing);
    } else if (key == Qt::Key_Tab && !(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        replace(selection(), QStringLiteral("\t"), Edit::typing);
    } else if (isTyping(event)) {
        replace(selection(), event.text(), Edit::typing);
    } else {
        return false;
    }
    return true;
}

// Deletions: a selection, else what the key reaches.
bool InlineTextEditor::erase(const QKeyEvent &event)
{
    const Qt::KeyboardModifiers modifiers = event.modifiers() & ~(Qt::KeypadModifier | Qt::ShiftModifier);
    const bool back = event.key() == Qt::Key_Backspace, forward = event.key() == Qt::Key_Delete;
    if ((!back && !forward) || (modifiers != Qt::NoModifier && modifiers != Qt::AltModifier && modifiers != Qt::ControlModifier))
        return false;
    // Shift+Delete is X11's Cut.
    if (forward && event.matches(QKeySequence::Cut))
        return false;
    TextRange range = selection();
    if (range.isEmpty()) {
        // Alt reaches a word, Ctrl the line, as Swift's fills.
        const int reach = modifiers == Qt::AltModifier   ? (back ? previousWordStart(m_position) : nextWordEnd(m_position))
                          : modifiers == Qt::ControlModifier ? lineBound(forward)
                          : back                           ? previousCharacter(m_position)
                                                           : nextCharacter(m_position);
        range = {std::min(reach, m_position), std::max(reach, m_position)};
    }
    if (!range.isEmpty())
        replace(range, QString(), Edit::deleting);
    return true;
}

// Qt's movement keys, as the app's text fields take them.
bool InlineTextEditor::move(const QKeyEvent &event)
{
    const TextRange range = selection();
    const auto matches = [&](QKeySequence::StandardKey move, QKeySequence::StandardKey select) {
        return event.matches(move) ? std::optional(false) : event.matches(select) ? std::optional(true) : std::nullopt;
    };
    // Right goes back in right-to-left text, as Qt's fields.
    const bool rtl = rightToLeft();
    const int back = rtl ? nextCharacter(m_position) : previousCharacter(m_position);
    const int on = rtl ? previousCharacter(m_position) : nextCharacter(m_position);
    if (const std::optional<bool> extend = matches(QKeySequence::MoveToNextChar, QKeySequence::SelectNextChar))
        moveTo(!*extend && !range.isEmpty() ? (rtl ? range.start : range.end) : on, *extend);
    else if (const std::optional<bool> extend = matches(QKeySequence::MoveToPreviousChar, QKeySequence::SelectPreviousChar))
        moveTo(!*extend && !range.isEmpty() ? (rtl ? range.end : range.start) : back, *extend);
    else if (const std::optional<bool> extend = matches(QKeySequence::MoveToNextWord, QKeySequence::SelectNextWord))
        moveTo(rtl ? previousWordStart(m_position) : nextWordEnd(m_position), *extend);
    else if (const std::optional<bool> extend = matches(QKeySequence::MoveToPreviousWord, QKeySequence::SelectPreviousWord))
        moveTo(rtl ? nextWordEnd(m_position) : previousWordStart(m_position), *extend);
    else if (const std::optional<bool> extend = matches(QKeySequence::MoveToNextLine, QKeySequence::SelectNextLine))
        moveTo(verticalTarget(1), *extend, true);
    else if (const std::optional<bool> extend = matches(QKeySequence::MoveToPreviousLine, QKeySequence::SelectPreviousLine))
        moveTo(verticalTarget(-1), *extend, true);
    else if (const std::optional<bool> extend = matches(QKeySequence::MoveToStartOfLine, QKeySequence::SelectStartOfLine))
        moveTo(lineBound(false), *extend);
    else if (const std::optional<bool> extend = matches(QKeySequence::MoveToEndOfLine, QKeySequence::SelectEndOfLine))
        moveTo(lineBound(true), *extend);
    else if (const std::optional<bool> extend = matches(QKeySequence::MoveToStartOfDocument, QKeySequence::SelectStartOfDocument))
        moveTo(0, *extend);
    else if (const std::optional<bool> extend = matches(QKeySequence::MoveToEndOfDocument, QKeySequence::SelectEndOfDocument))
        moveTo(int(style().content.size()), *extend);
    else
        return false;
    return true;
}

// Whether the caret's paragraph reads right to left.
bool InlineTextEditor::rightToLeft() const
{
    const QString &content = style().content;
    int start = m_position, end = m_position;
    while (start > 0 && !separates(content.at(start - 1)))
        --start;
    while (end < content.size() && !separates(content.at(end)))
        ++end;
    return QStringView(content).mid(start, end - start).isRightToLeft();
}

// Cocoa's paragraph separators, as `TextLines` parts paragraphs.
bool InlineTextEditor::separates(QChar character)
{
    return character == u'\n' || character == u'\r' || character == QChar::ParagraphSeparator;
}

TextRange InlineTextEditor::spanning(int anchor, int position)
{
    return {std::min(anchor, position), std::max(anchor, position)};
}

TextRange InlineTextEditor::selection() const
{
    return spanning(m_anchor, m_position);
}

void InlineTextEditor::replace(TextRange range, const QString &text, std::optional<Edit> edit)
{
    const QString content = style().content;
    // Swift's shouldChangeTextIn: 100,000 UTF-16 units at most.
    if (content.size() - (range.end - range.start) + text.size() > 100'000)
        return;
    if (!edit || edit != m_lastEdit)
        m_undo.push_back(m_composing ? m_composing->before : Snapshot{content, m_anchor, m_position});
    m_redo.clear();
    m_lastEdit = edit;
    m_composing.reset();
    m_anchor = m_position = range.start + int(text.size());
    publish(QString(content).replace(range.start, range.end - range.start, text));
}

// Swift's textDidChange: the draft takes the text view's string.
void InlineTextEditor::publish(const QString &content)
{
    // An edit ends a drag or resize, like a press.
    release();
    TextDraft draft = m_session.textDraft().value();
    draft.style.content = content;
    m_goalX.reset();
    showCaret();
    m_session.setTextDraft(draft);
}

void InlineTextEditor::moveTo(int position, bool extend, bool keepGoal)
{
    m_position = position;
    if (!extend)
        m_anchor = position;
    if (!keepGoal)
        m_goalX.reset();
    m_lastEdit.reset();
    showCaret();
    updateInputMethod();
}

void InlineTextEditor::restore(std::vector<Snapshot> &from, std::vector<Snapshot> &to)
{
    if (from.empty())
        return;
    to.push_back(m_composing ? m_composing->before : Snapshot{style().content, m_anchor, m_position});
    const Snapshot snapshot = from.back();
    from.pop_back();
    m_lastEdit.reset();
    m_composing.reset();
    m_anchor = snapshot.anchor;
    m_position = snapshot.position;
    publish(snapshot.content);
}

void InlineTextEditor::undo()
{
    restore(m_undo, m_redo);
}

void InlineTextEditor::redo()
{
    restore(m_redo, m_undo);
}

void InlineTextEditor::changeSpacing(int key, double step)
{
    // A resize would write back the style it began with.
    release();
    m_session.changeTextStyle([key, step](LayerTextStyle &style) {
        if (key == Qt::Key_Left || key == Qt::Key_Right)
            style.tracking += key == Qt::Key_Left ? -step : step;
        // Up closes the lines, counting from Auto's height.
        else
            style.leading = key == Qt::Key_Up ? std::max(1.0, style.lineHeight() - step) : style.lineHeight() + step;
    });
}

int InlineTextEditor::nextCharacter(int position) const
{
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, style().content);
    finder.setPosition(position);
    const qsizetype next = finder.toNextBoundary();
    return next < 0 ? int(style().content.size()) : int(next);
}

int InlineTextEditor::previousCharacter(int position) const
{
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, style().content);
    finder.setPosition(position);
    return int(std::max<qsizetype>(finder.toPreviousBoundary(), 0));
}

// NSTextView's word moves: to a word's end, or its start.
int InlineTextEditor::nextWordEnd(int position) const
{
    QTextBoundaryFinder finder(QTextBoundaryFinder::Word, style().content);
    finder.setPosition(position);
    for (qsizetype at = finder.toNextBoundary(); at >= 0; at = finder.toNextBoundary()) {
        if (finder.boundaryReasons().testFlag(QTextBoundaryFinder::EndOfItem))
            return int(at);
    }
    return int(style().content.size());
}

int InlineTextEditor::previousWordStart(int position) const
{
    QTextBoundaryFinder finder(QTextBoundaryFinder::Word, style().content);
    finder.setPosition(position);
    for (qsizetype at = finder.toPreviousBoundary(); at >= 0; at = finder.toPreviousBoundary()) {
        if (finder.boundaryReasons().testFlag(QTextBoundaryFinder::StartOfItem))
            return int(at);
    }
    return 0;
}

// The caret's line's start or end, else the text's.
int InlineTextEditor::lineBound(bool end) const
{
    const std::optional<int> line = m_lines->lineOf(m_position);
    if (!line)
        return end ? int(style().content.size()) : 0;
    return end ? m_lines->lineEnd(*line) : m_lines->lineStart(*line);
}

// Above line one: the start; below the last: the end.
int InlineTextEditor::verticalTarget(int direction)
{
    const std::optional<int> line = m_lines->lineOf(m_position);
    if (!line)
        return m_position;
    const double x = m_goalX.value_or(m_lines->caret(m_position).value().x());
    m_goalX = x;
    const int target = *line + direction;
    if (target < 0)
        return 0;
    if (target >= m_lines->lineCount())
        return int(style().content.size());
    return m_lines->position(target, x);
}
