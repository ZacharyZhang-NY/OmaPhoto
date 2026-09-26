#include "Document/EditorSession.h"
#include "Document/Selection.h"
#include "Rendering/EditorCanvas.h"
#include "Rendering/InlineTextEditor.h"
#include <QGuiApplication>
#include <QInputMethod>
#include <QPainter>
#include <QTextBoundaryFinder>
#include <algorithm>
#include <array>
#include <cmath>

// Swift's hitTest: the handles' bands, then the box.
bool InlineTextEditor::press(QPointF point, Qt::KeyboardModifiers modifiers, int clicks)
{
    // A press starts afresh: Qt can lose a release.
    release();
    const std::optional<int> handle = handleAt(point);
    const QRectF box(QPointF(0, 0), m_logicalSize);
    const QPointF local = boxTransform().inverted().map(point);
    if (!handle && !box.contains(local))
        return false;
    if (handle) {
        land();
        beginResize(*handle, point);
        return true;
    }
    // A click in the padding, the editor's own, does nothing.
    const double padding = LayerTextStyle::padding;
    if (box.adjusted(padding, padding, -padding, -padding).contains(local))
        pressText(point, modifiers, clicks);
    return true;
}

// NSTextView's clicks: a caret, a word, a paragraph; Shift extends.
void InlineTextEditor::pressText(QPointF point, Qt::KeyboardModifiers modifiers, int clicks)
{
    land();
    m_selecting = true;
    // Shift extends by the last click's unit while that stands.
    if (clicks == 1 && modifiers.testFlag(Qt::ShiftModifier)) {
        if (!m_tracking || m_tracking->anchor != m_anchor || m_tracking->position != m_position)
            m_tracking = Tracking{{m_anchor, m_anchor}, 1, m_anchor, m_position};
        drag(point);
        return;
    }
    const int position = positionAt(point, false);
    const TextRange unit = clicks == 1 ? TextRange{position, position} : unitAt(positionAt(point, true), clicks);
    m_anchor = unit.start;
    moveTo(unit.end, true);
    m_tracking = Tracking{unit, clicks, m_anchor, m_position};
}

// A drag selects by its click's unit from its start.
bool InlineTextEditor::drag(QPointF point)
{
    if (m_resize) {
        resizeTo(point);
        return true;
    }
    if (!m_selecting)
        return false;
    Tracking &tracking = m_tracking.value();
    const int position = positionAt(point, false);
    const TextRange unit = tracking.clicks == 1 ? TextRange{position, position} : unitAt(positionAt(point, true), tracking.clicks);
    if (unit.start < tracking.origin.start) {
        m_anchor = tracking.origin.end;
        moveTo(unit.start, true);
    } else {
        m_anchor = tracking.origin.start;
        moveTo(unit.end, true);
    }
    tracking.anchor = m_anchor;
    tracking.position = m_position;
    return true;
}

// The unit stays for Shift; the press itself ends.
void InlineTextEditor::release()
{
    m_resize.reset();
    m_selecting = false;
}

std::optional<QCursor> InlineTextEditor::cursorAt(QPointF point) const
{
    const std::optional<int> handle = m_resize ? std::optional(m_resize->handle) : handleAt(point);
    if (!handle)
        return std::nullopt;
    // Swift's frame positions, turned with the box by eighths.
    static constexpr std::array<int, 8> positions{0, 1, 2, 3, 0, 1, 2, 3};
    static constexpr std::array shapes{Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor};
    const std::optional<TextDraft> &draft = m_session.textDraft();
    const double rotation = draft && draft->transform ? draft->transform->rotation : 0;
    // Reduced first: Swift's Int holds turns an int cannot.
    const int turns = (int(std::round(std::fmod(rotation, 360) / 45)) % 8 + 8) % 8;
    return QCursor(shapes[size_t((positions[size_t(*handle)] + turns) % 4)]);
}

// Swift's handle(at:): a band along each edge, in box units.
std::optional<int> InlineTextEditor::handleAt(QPointF point) const
{
    const QPointF local = boxTransform().inverted().map(point);
    const double width = m_logicalSize.width(), height = m_logicalSize.height();
    // Ten points to a six-point handle, as the Move box.
    const double reach = std::min(m_handleSize * 10 / 6, std::min(width, height) / 3);
    if (local.x() < -reach || local.x() > width + reach || local.y() < -reach || local.y() > height + reach)
        return std::nullopt;
    const bool left = local.x() <= reach, right = local.x() >= width - reach;
    const bool top = local.y() <= reach, bottom = local.y() >= height - reach;
    if (left && top)
        return 0;
    if (right && top)
        return 2;
    if (right && bottom)
        return 4;
    if (left && bottom)
        return 6;
    if (top)
        return 1;
    if (right)
        return 3;
    if (bottom)
        return 5;
    if (left)
        return 7;
    return std::nullopt;
}

// The text position under a point, through the text's mirror.
int InlineTextEditor::positionAt(QPointF point, bool onCharacter) const
{
    if (m_lines->lineCount() == 0)
        return 0;
    const double padding = LayerTextStyle::padding;
    const QPointF text = textTransform().inverted().map(point) - QPointF(padding, padding);
    // Above the lines is the start, below them the end.
    if (text.y() < 0)
        return 0;
    // A double first: a far drag passes int's range.
    const double line = std::floor(text.y() / style().lineHeight());
    if (line >= m_lines->lineCount())
        return m_lines->lineEnd(m_lines->lineCount() - 1);
    return onCharacter ? m_lines->character(int(line), text.x()) : m_lines->position(int(line), text.x());
}

// A double click's word; a triple click's paragraph and break.
TextRange InlineTextEditor::unitAt(int position, int clicks) const
{
    const QString &content = style().content;
    if (clicks == 2) {
        // Past the text, its last word; after a break, none.
        const QChar last = content.isEmpty() ? QChar() : content.back();
        if (content.isEmpty() || (position == content.size() && (separates(last) || last == QChar::LineSeparator)))
            return {position, position};
        QTextBoundaryFinder finder(QTextBoundaryFinder::Word, content);
        finder.setPosition(std::min(position, int(content.size()) - 1));
        if (!finder.isAtBoundary())
            finder.toPreviousBoundary();
        const int start = int(finder.position());
        return {start, int(finder.toNextBoundary())};
    }
    int start = position, end = position;
    while (start > 0 && !separates(content.at(start - 1)))
        --start;
    while (end < content.size() && !separates(content.at(end)))
        ++end;
    if (end < content.size())
        end += content.mid(end, 2) == QStringLiteral("\r\n") ? 2 : 1;
    return {start, end};
}

// Clicks commit a preedit, as Qt asks, or land it.
void InlineTextEditor::land()
{
    if (!m_composing)
        return;
    QGuiApplication::inputMethod()->commit();
    if (!m_composing)
        return;
    QInputMethodEvent commit;
    commit.setCommitString(style().content.mid(m_composing->marked.start, m_composing->marked.end - m_composing->marked.start));
    inputMethod(commit);
}

// Swift's handle press: point text becomes a box, as sized.
void InlineTextEditor::beginResize(int handle, QPointF point)
{
    TextDraft draft = m_session.textDraft().value();
    if (!draft.style.boxSize) {
        draft.style.boxSize = m_logicalSize;
        draft.transform = m_shownTransform;
        m_session.setTextDraft(draft);
    }
    m_resize = Resize{handle, draft, m_shownTransform, m_session.viewport.documentPoint(point, m_session.document().value().size())};
}

// Swift's mouseDragged: the box in its frame, 16 pixels least.
void InlineTextEditor::resizeTo(QPointF point)
{
    const Resize &resize = m_resize.value();
    const LayerTransform &old = resize.transform;
    const QPointF pixel = m_session.viewport.documentPoint(point, m_session.document().value().size());
    const double dx = pixel.x() - resize.start.x(), dy = pixel.y() - resize.start.y();
    const double localX = dx * std::cos(old.radians()) + dy * std::sin(old.radians());
    const double localY = -dx * std::sin(old.radians()) + dy * std::cos(old.radians());
    const QPointF unit = LayerTransform::handles[size_t(resize.handle)];
    const QSizeF source = resize.draft.style.boxSize.value();
    const double minWidth = 16 * old.size.width() / source.width(), minHeight = 16 * old.size.height() / source.height();
    double left = 0, top = 0, right = old.size.width(), bottom = old.size.height();
    if (unit.x() == 0)
        left = std::min(localX, right - minWidth);
    if (unit.x() == 1)
        right = std::max(left + minWidth, right + localX);
    if (unit.y() == 0)
        top = std::min(localY, bottom - minHeight);
    if (unit.y() == 1)
        bottom = std::max(top + minHeight, bottom + localY);
    TextDraft draft = resize.draft;
    draft.style.boxSize = QSizeF(std::round((right - left) * source.width() / old.size.width()), std::round((bottom - top) * source.height() / old.size.height()));
    if (!draft.style.boxIsValid())
        return;
    LayerTransform transform = old;
    transform.size = QSizeF(draft.style.boxSize->width() * old.size.width() / source.width(), draft.style.boxSize->height() * old.size.height() / source.height());
    // The edge not dragged stays put, however the box turns.
    transform.origin += old.point(QPointF(left / old.size.width(), top / old.size.height())) - transform.point(QPointF(0, 0));
    if (!transform.isValid())
        return;
    draft.origin = transform.origin;
    draft.transform = transform;
    m_session.setTextDraft(draft);
}

void CanvasView::pressTextTool(QPointF point, Qt::KeyboardModifiers modifiers, int clicks)
{
    // A box whose release was lost goes first.
    if (m_textBoxAnchor)
        endTextGesture();
    if (m_inlineTextEditor && m_inlineTextEditor->press(point, modifiers, clicks)) {
        updateCursor();
        return;
    }
    beginTextGesture(point, modifiers, clicks);
}

// Swift's beginTextGesture: open the text there, else draw a box.
void CanvasView::beginTextGesture(QPointF point, Qt::KeyboardModifiers modifiers, int clicks)
{
    if (!m_session.finishText())
        return;
    const CanvasDocument &document = m_session.document().value();
    const QPointF pixel = m_session.viewport.documentPoint(point, document.size());
    const QSet<QUuid> visible = document.effectiveVisibleIDs();
    for (auto layer = document.layers.rbegin(); layer != document.layers.rend(); ++layer) {
        if (!visible.contains(layer->id) || !layer->liveText() || !layer->transform.contains(pixel))
            continue;
        m_session.selectLayer(QUuid(layer->id));
        m_session.editActiveText();
        if (m_inlineTextEditor)
            m_inlineTextEditor->pressText(point, modifiers, clicks);
        return;
    }
    m_textBoxAnchor = pixel;
    m_textBoxRect = QRectF(pixel, QSizeF(0, 0));
}

void CanvasView::dragTextGesture(QPointF point)
{
    // Swift's guard: a document undone mid-drag takes nothing.
    if (!m_session.document())
        return;
    const QRectF before = textBoxDraftRect();
    m_textBoxRect = DragBox::rect(m_textBoxAnchor.value(), m_session.viewport.documentPoint(point, m_session.document().value().size()), false, false);
    update((before | textBoxDraftRect()).adjusted(-2, -2, 2, 2).toAlignedRect());
}

// Under four pixels a drag is a click: point text.
void CanvasView::finishTextGesture()
{
    const QRectF rect = m_textBoxRect.value();
    endTextGesture();
    if (rect.width() < 4 && rect.height() < 4)
        m_session.beginText(rect.topLeft(), true);
    else
        m_session.beginText(rect);
}

// The box being drawn goes, repainted away.
void CanvasView::endTextGesture()
{
    update(textBoxDraftRect().adjusted(-2, -2, 2, 2).toAlignedRect());
    m_textBoxAnchor.reset();
    m_textBoxRect.reset();
}

// Where the box being drawn shows, in view points.
QRectF CanvasView::textBoxDraftRect() const
{
    if (!m_textBoxRect || !m_session.document())
        return QRectF();
    const QPointF origin = m_session.viewport.viewPoint(m_textBoxRect->topLeft(), m_session.document().value().size());
    return QRectF(origin, m_textBoxRect->size() * m_session.viewport.pointsPerPixel());
}

void CanvasView::drawTextBoxDraft(QPainter &painter) const
{
    if (!m_textBoxRect)
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(palette().color(QPalette::Highlight), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(textBoxDraftRect());
    painter.restore();
}
