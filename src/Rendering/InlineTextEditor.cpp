#include "Rendering/InlineTextEditor.h"
#include "Document/EditorSession.h"
#include "Rendering/EditorCanvas.h"
#include <QApplication>
#include <QGuiApplication>
#include <QInputMethod>
#include <QLineEdit>
#include <QPainter>
#include <QPointer>
#include <QStyleHints>
#include <cmath>

InlineTextEditor::InlineTextEditor(EditorSession &session, QWidget &canvas) : m_session(session), m_canvas(canvas)
{
    // NSTextView's insertion point blinks at the system's pace.
    m_blink.setObjectName(QStringLiteral("caretBlink"));
    m_blink.setParent(&canvas);
    m_blink.setInterval(QGuiApplication::styleHints()->cursorFlashTime() / 2);
    QObject::connect(&m_blink, &QTimer::timeout, &m_blink, [this] {
        m_caretShown = !m_caretShown;
        repaint();
    });
}

void InlineTextEditor::synchronize(const TextDraft &draft)
{
    // A draft needs a document, and holds it open.
    const CanvasDocument &document = m_session.document().value();
    const bool fresh = m_draftID != draft.id;
    m_draftID = draft.id;
    // Another draft starts afresh: no steps, nothing composing.
    if (fresh) {
        m_undo.clear();
        m_redo.clear();
        m_lastEdit.reset();
        m_composing.reset();
    }
    const LayerTextStyle &style = draft.style;
    // Point text has no box: it is what is typed.
    m_logicalSize = EditorSession::textBoxSize(style);
    LayerTransform transform = draft.transform.value_or(LayerTransform{.origin = draft.origin, .size = m_logicalSize});
    // Point text grows at its layer's scale, corner kept.
    const int index = draft.layerID ? indexOf(document.layers, *draft.layerID) : -1;
    if (!style.boxSize && draft.transform && index >= 0 && document.layers[size_t(index)].asset) {
        const double factor = transform.size.width() / document.layers[size_t(index)].asset->size().width();
        const QPointF anchor = transform.point(QPointF(0, 0));
        transform.size = m_logicalSize * factor;
        transform.origin += anchor - transform.point(QPointF(0, 0));
    }
    m_shownTransform = transform;
    const double scale = m_session.viewport.pointsPerPixel();
    m_handleSize = std::max(2.0, 6 / std::max(0.01, scale * transform.size.width() / m_logicalSize.width()));
    // Content from elsewhere clamps the selection, as Swift's.
    if (m_shownStyle != style && !m_composing) {
        m_anchor = std::min(m_anchor, int(style.content.size()));
        m_position = std::min(m_position, int(style.content.size()));
        // Swift sets the selection anew: a click's unit goes.
        m_tracking.reset();
        m_selecting = false;
    }
    m_shownStyle = style;
    const double padding = LayerTextStyle::padding;
    m_lines = std::make_unique<TextLines>(
        style, QSizeF(std::max(1.0, m_logicalSize.width() - 2 * padding), std::max(1.0, m_logicalSize.height() - 2 * padding)));
    updateInputMethod();
    if (!fresh)
        return;
    // The text view takes the keys, sparing a text field.
    QMetaObject::invokeMethod(
        &m_canvas,
        [canvas = QPointer<QWidget>(&m_canvas), session = &m_session, id = draft.id] {
            if (!canvas || !session->textDraft() || session->textDraft().value().id != id || qobject_cast<QLineEdit *>(QApplication::focusWidget()))
                return;
            canvas->setFocus(Qt::OtherFocusReason);
        },
        Qt::QueuedConnection);
}

QTransform InlineTextEditor::boxTransform() const
{
    const QPointF origin = m_session.viewport.documentRect(m_session.document().value().size()).topLeft();
    const double scale = m_session.viewport.pointsPerPixel();
    const QPointF centre = m_shownTransform.center();
    const QSizeF size = m_shownTransform.size;
    // Unit square to document as LayerTransform::point, then view.
    const QTransform layer = QTransform::fromTranslate(centre.x(), centre.y())
                                 .rotate(m_shownTransform.rotation)
                                 .translate(-size.width() / 2, -size.height() / 2)
                                 .scale(size.width() / m_logicalSize.width(), size.height() / m_logicalSize.height());
    return layer * QTransform::fromTranslate(origin.x(), origin.y()).scale(scale, scale);
}

QTransform InlineTextEditor::textTransform() const
{
    // Swift mirrors the text surface about its middle.
    const QPointF middle(m_logicalSize.width() / 2, m_logicalSize.height() / 2);
    const QTransform mirror = QTransform::fromTranslate(middle.x(), middle.y())
                                  .scale(m_shownTransform.flipX ? -1 : 1, m_shownTransform.flipY ? -1 : 1)
                                  .translate(-middle.x(), -middle.y());
    return mirror * boxTransform();
}

void InlineTextEditor::draw(QPainter &painter) const
{
    if (!m_lines)
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette palette = m_canvas.palette();
    // The frame and handles first: Swift's text view draws above.
    painter.setTransform(boxTransform());
    const QColor accent = palette.color(QPalette::Highlight);
    const double width = m_logicalSize.width(), height = m_logicalSize.height(), inset = m_handleSize / 12;
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(accent, m_handleSize / 6, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
    painter.drawRect(QRectF(0, 0, width, height).adjusted(inset, inset, -inset, -inset));
    painter.setPen(QPen(accent, 1, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
    for (const QPointF &unit : LayerTransform::handles) {
        const QRectF handle(unit.x() * width - m_handleSize / 2, unit.y() * height - m_handleSize / 2, m_handleSize, m_handleSize);
        painter.fillRect(handle, Qt::white);
        painter.drawRect(handle);
    }
    // Text that does not fit: a plus in a corner.
    if (m_lines->overflows()) {
        const QPointF centre(LayerTransform::handles[4].x() * width, LayerTransform::handles[4].y() * height);
        const double arm = m_handleSize * 0.42;
        painter.setPen(QPen(Qt::black, m_handleSize / 6, Qt::SolidLine, Qt::FlatCap));
        painter.drawLine(centre - QPointF(arm, 0), centre + QPointF(arm, 0));
        painter.drawLine(centre - QPointF(0, arm), centre + QPointF(0, arm));
    }
    const double padding = LayerTextStyle::padding;
    painter.setTransform(textTransform());
    const QColor ink = QColor::fromRgbF(float(style().red), float(style().green), float(style().blue));
    painter.setPen(ink);
    const QColor highlight = palette.color(m_focused ? QPalette::Active : QPalette::Inactive, QPalette::Highlight);
    // The input method's formats, else NSTextView's underline.
    QList<QTextLayout::FormatRange> formats;
    if (m_composing) {
        for (QTextLayout::FormatRange format : m_composing->formats) {
            format.start += m_composing->marked.start;
            formats << format;
        }
        if (formats.isEmpty()) {
            QTextCharFormat underline;
            underline.setFontUnderline(true);
            formats << QTextLayout::FormatRange{m_composing->marked.start, m_composing->marked.end - m_composing->marked.start, underline};
        }
    }
    m_lines->draw(painter, QPointF(padding, padding), shownSelection(), formats, highlight);
    // The insertion point shows where nothing is selected.
    const bool hidden = m_composing && m_composing->caretHidden;
    if (const std::optional<QRectF> caret = m_lines->caret(m_position); caret && caretShown() && selection().isEmpty() && !hidden)
        painter.fillRect(caret->translated(padding, padding), m_composing ? m_composing->caretColour.value_or(ink) : ink);
    painter.restore();
}

QRectF InlineTextEditor::drawnRect() const
{
    if (!m_lines)
        return QRectF();
    // Handles overhang; tight lines rise, the caret's line falls.
    const double margin = m_handleSize + std::max(style().fontSize, style().lineHeight());
    const QRectF box = QRectF(QPointF(0, 0), m_logicalSize).adjusted(-margin, -margin, margin, margin);
    return boxTransform().mapRect(box).adjusted(-2, -2, 2, 2);
}

void InlineTextEditor::setFocused(bool focused)
{
    m_focused = focused;
    showCaret();
}

void InlineTextEditor::showCaret()
{
    m_caretShown = true;
    if (m_focused && m_blink.interval() > 0)
        m_blink.start();
    else
        m_blink.stop();
    repaint();
}

void InlineTextEditor::repaint() const
{
    m_canvas.update(drawnRect().toAlignedRect());
}

// Qt's updateMicroFocus: input methods follow the caret.
void InlineTextEditor::updateInputMethod() const
{
    if (QGuiApplication::focusObject() == &m_canvas)
        QGuiApplication::inputMethod()->update(Qt::ImQueryInput);
}

// Swift's synchronizeInlineText: the editor follows the draft.
void CanvasView::synchronizeInlineText()
{
    const std::optional<TextDraft> &draft = m_session.textDraft();
    if (!draft) {
        // Its layer shows whole again, as Swift's needsDisplay.
        if (m_inlineTextEditor) {
            update();
            m_inlineTextEditor.reset();
            setAttribute(Qt::WA_InputMethodEnabled, false);
        }
        return;
    }
    // A new draft repaints the canvas, as Swift's needsDisplay.
    if (!m_inlineTextEditor || m_inlineTextEditor->draftID() != draft->id)
        update();
    const bool fresh = !m_inlineTextEditor;
    if (fresh)
        m_inlineTextEditor = std::make_unique<InlineTextEditor>(m_session, *this);
    const QRectF before = m_inlineTextEditor->drawnRect();
    m_inlineTextEditor->synchronize(*draft);
    update((before | m_inlineTextEditor->drawnRect()).toAlignedRect());
    if (!fresh)
        return;
    m_inlineTextEditor->setFocused(hasFocus());
    // Input methods compose into the draft, as a text view.
    setAttribute(Qt::WA_InputMethodEnabled, true);
}

void CanvasView::inputMethodEvent(QInputMethodEvent *event)
{
    if (!m_inlineTextEditor) {
        QWidget::inputMethodEvent(event);
        return;
    }
    m_inlineTextEditor->inputMethod(*event);
    event->accept();
}

QVariant CanvasView::inputMethodQuery(Qt::InputMethodQuery query) const
{
    return m_inlineTextEditor ? m_inlineTextEditor->inputMethodQuery(query) : QWidget::inputMethodQuery(query);
}
