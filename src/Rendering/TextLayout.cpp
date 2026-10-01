#include "Rendering/TextLayout.h"
#include "Logging.h"
#include <QPainter>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <fontconfig/fontconfig.h>
#include <map>
#include <stdexcept>

namespace {
QString text(FcPattern *pattern, const char *object)
{
    FcChar8 *value = nullptr;
    if (FcPatternGetString(pattern, object, 0, &value) != FcResultMatch)
        return QString();
    return QString::fromUtf8(reinterpret_cast<const char *>(value));
}

// Each installed face by PostScript name: its family and style.
const std::map<QString, std::pair<QString, QString>> &faces()
{
    static const std::map<QString, std::pair<QString, QString>> listed = [] {
        std::map<QString, std::pair<QString, QString>> found;
        FcPattern *pattern = FcPatternCreate();
        FcObjectSet *objects = FcObjectSetBuild(FC_POSTSCRIPT_NAME, FC_FAMILY, FC_STYLE, nullptr);
        FcFontSet *set = FcFontList(nullptr, pattern, objects);
        for (int index = 0; set && index < set->nfont; ++index) {
            const QString name = text(set->fonts[index], FC_POSTSCRIPT_NAME);
            if (!name.isEmpty())
                found.emplace(name, std::pair(text(set->fonts[index], FC_FAMILY), text(set->fonts[index], FC_STYLE)));
        }
        if (set)
            FcFontSetDestroy(set);
        FcObjectSetDestroy(objects);
        FcPatternDestroy(pattern);
        qCInfo(lcRendering) << "fontconfig lists" << int(found.size()) << "faces";
        return found;
    }();
    return listed;
}
}

QFont TextLayout::font(const LayerTextStyle &style)
{
    return font(style, style.fontName);
}

QFont TextLayout::font(const LayerTextStyle &style, const QString &face)
{
    QFont font;
    if (faces().contains(face)) {
        font.setFamily(faces().at(face).first);
        font.setStyleName(faces().at(face).second);
    } else {
        // Not installed: Qt's match, where macOS takes its own.
        font.setFamily(face);
    }
    // A point is a pixel here; Qt's faces come whole.
    font.setPixelSize(qRound(style.fontSize));
    // Swift's kern: no pairs; spacing after every letter.
    font.setKerning(false);
    font.setLetterSpacing(QFont::AbsoluteSpacing, style.tracking);
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
}

QStringList TextLayout::availableFonts()
{
    QStringList names;
    for (const auto &[name, face] : faces())
        names << name;
    return names;
}

TextLines::TextLines(const LayerTextStyle &style, QSizeF container)
    : m_lineHeight(style.lineHeight()), m_width(container.width()), m_laidEnd(int(style.content.size()))
{
    const QFont font = TextLayout::font(style);
    QTextOption option(style.alignment == TextAlignment::left ? Qt::AlignLeft : style.alignment == TextAlignment::center ? Qt::AlignHCenter : Qt::AlignRight);
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    // NSParagraphStyle's twelve stops, 28 points apart, and on.
    option.setTabStopDistance(28);
    // Cocoa's paragraph separators: LF, CR, CRLF and U+2029.
    static const QRegularExpression separator(QStringLiteral("\\r\\n|[\\r\\n\\x{2029}]"));
    std::vector<TextRange> paragraphs;
    int from = 0;
    for (QRegularExpressionMatchIterator match = separator.globalMatch(style.content); match.hasNext();) {
        const QRegularExpressionMatch next = match.next();
        paragraphs.push_back({from, int(next.capturedStart())});
        from = int(next.capturedEnd());
    }
    paragraphs.push_back({from, int(style.content.size())});
    // TextKit's extra line after a final break holds the caret.
    const QChar last = style.content.isEmpty() ? QChar() : style.content.back();
    const bool caretLine = last == u'\n' || last == u'\r' || last == QChar::LineSeparator || last == QChar::ParagraphSeparator;
    for (const TextRange &range : paragraphs) {
        const QString paragraph = style.content.mid(range.start, range.end - range.start);
        auto layout = std::make_unique<QTextLayout>(paragraph, font);
        layout->setTextOption(option);
        // Run letters draw once in their face and colour.
        QList<QTextLayout::FormatRange> runs;
        for (const LayerTextFontRun &run : style.fontRuns.value_or(std::vector<LayerTextFontRun>())) {
            const qint64 start = std::max<qint64>(run.location, range.start), end = std::min<qint64>(run.location + run.length, range.end);
            if (start >= end)
                continue;
            QTextCharFormat format;
            format.setFont(TextLayout::font(style, run.fontName));
            runs << QTextLayout::FormatRange{int(start - range.start), int(end - start), format};
        }
        for (const LayerTextColorRun &run : style.colorRuns.value_or(std::vector<LayerTextColorRun>())) {
            const qint64 start = std::max<qint64>(run.location, range.start), end = std::min<qint64>(run.location + run.length, range.end);
            if (start >= end)
                continue;
            QTextCharFormat format;
            format.setForeground(QColor::fromRgbF(float(run.red), float(run.green), float(run.blue)));
            runs << QTextLayout::FormatRange{int(start - range.start), int(end - start), format};
        }
        layout->setFormats(runs);
        layout->beginLayout();
        const auto laid = [&] {
            const int count = layout->lineCount();
            return count == 0 ? 0 : layout->lineAt(count - 1).textStart() + layout->lineAt(count - 1).textLength();
        };
        // Empty paragraphs take a line, as do final U+2028s.
        const auto pending = [&] {
            return layout->lineCount() == 0 || laid() < paragraph.length()
                || (layout->lineAt(layout->lineCount() - 1).textLength() > 0 && paragraph.endsWith(QChar::LineSeparator));
        };
        while (pending()) {
            // Past the bottom no line is laid, but the caret's.
            const bool caret = caretLine && &range == &paragraphs.back() && laid() == paragraph.length();
            if ((lineCount() + 1) * m_lineHeight > container.height() && !caret) {
                // TextKit's overflow: some character left unlaid.
                m_laidEnd = range.start + laid();
                m_overflows = m_laidEnd < style.content.size();
                break;
            }
            QTextLine line = layout->createLine();
            if (!line.isValid())
                throw std::logic_error("Qt laid out no line for pending text");
            line.setLineWidth(container.width());
            // The fixed height's extra space sits above the text.
            line.setPosition(QPointF(0, (lineCount() + 1) * m_lineHeight - line.descent() - line.ascent()));
            m_lineList.push_back({m_paragraphs.size(), line.lineNumber()});
        }
        layout->endLayout();
        m_paragraphs.push_back(std::move(layout));
        m_starts.push_back(range.start);
        if (m_overflows)
            break;
    }
    m_caretLine = caretLine && !m_overflows;
}

QSizeF TextLines::usedSize() const
{
    double width = 0;
    for (const std::unique_ptr<QTextLayout> &paragraph : m_paragraphs) {
        for (int index = 0; index < paragraph->lineCount(); ++index)
            width = std::max(width, paragraph->lineAt(index).naturalTextWidth());
    }
    // Measures leave the caret's line out, as TextKit's do.
    return QSizeF(width, (lineCount() - (m_caretLine ? 1 : 0)) * m_lineHeight);
}

void TextLines::draw(QPainter &painter, QPointF origin, TextRange selection, const QList<QTextLayout::FormatRange> &formats, const QColor &highlight) const
{
    for (int line = 0; line < lineCount() && !selection.isEmpty(); ++line)
        drawSelection(painter, origin, line, selection, highlight);
    for (size_t index = 0; index < m_paragraphs.size(); ++index) {
        const int offset = m_starts[index], length = int(m_paragraphs[index]->text().size());
        QList<QTextLayout::FormatRange> local;
        for (const QTextLayout::FormatRange &format : formats) {
            const int start = std::max(format.start - offset, 0), end = std::min(format.start + format.length - offset, length);
            if (start < end)
                local << QTextLayout::FormatRange{start, end - start, format.format};
        }
        m_paragraphs[index]->draw(&painter, origin, local);
    }
}

// TextKit's highlight: visual runs, the whole line, to its edge.
void TextLines::drawSelection(QPainter &painter, QPointF origin, int line, TextRange selection, const QColor &highlight) const
{
    const Line &laid = m_lineList[size_t(line)];
    const QTextLayout &paragraph = *m_paragraphs[laid.paragraph];
    const QTextLine text = paragraph.lineAt(laid.index);
    const int offset = m_starts[laid.paragraph], first = offset + text.textStart(), last = first + text.textLength();
    // The paragraph's separator ends its last line.
    const bool separated = laid.index == paragraph.lineCount() - 1;
    const bool before = selection.start < first && selection.end > first;
    const bool after = selection.end > last && (separated ? selection.start <= last : selection.start < last);
    const int start = std::max(selection.start, first), end = std::min(selection.end, last);
    const QRectF band(origin.x(), origin.y() + line * m_lineHeight, m_width, m_lineHeight);
    painter.save();
    painter.setClipRect(band, Qt::IntersectClip);
    if (start < end) {
        QTextCharFormat format;
        format.setBackground(highlight);
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.translate(0, band.top());
        painter.scale(1, m_lineHeight / text.height());
        painter.translate(0, -(origin.y() + text.y()));
        const QRectF own(origin.x(), origin.y() + text.y(), m_width, text.height());
        paragraph.draw(&painter, origin, {QTextLayout::FormatRange{start - offset, end - start, format}}, own);
        painter.restore();
    }
    const bool rtl = paragraph.text().isRightToLeft();
    const double head = origin.x() + text.cursorToX(first - offset), tail = origin.x() + text.cursorToX(last - offset);
    if (before)
        painter.fillRect(rtl ? QRectF(QPointF(head, band.top()), band.bottomRight()) : QRectF(band.topLeft(), QPointF(head, band.bottom())), highlight);
    if (after)
        painter.fillRect(rtl ? QRectF(band.topLeft(), QPointF(tail, band.bottom())) : QRectF(QPointF(tail, band.top()), band.bottomRight()), highlight);
    painter.restore();
}

std::optional<int> TextLines::lineOf(int position) const
{
    if (position > m_laidEnd)
        return std::nullopt;
    // The last laid paragraph starting at or before the position.
    const size_t paragraph = size_t(std::upper_bound(m_starts.begin(), m_starts.end(), position) - m_starts.begin() - 1);
    const int local = std::min(position - m_starts[paragraph], int(m_paragraphs[paragraph]->text().size()));
    const QTextLine line = m_paragraphs[paragraph]->lineForTextPosition(local);
    if (!line.isValid())
        return std::nullopt;
    for (size_t index = 0; index < m_lineList.size(); ++index) {
        if (m_lineList[index].paragraph == paragraph && m_lineList[index].index == line.lineNumber())
            return int(index);
    }
    throw std::logic_error("a laid line is missing from the list");
}

std::optional<QRectF> TextLines::caret(int position) const
{
    const std::optional<int> line = lineOf(position);
    if (!line)
        return std::nullopt;
    const Line &laid = m_lineList[size_t(*line)];
    const int local = std::min(position - m_starts[laid.paragraph], int(m_paragraphs[laid.paragraph]->text().size()));
    const double x = m_paragraphs[laid.paragraph]->lineAt(laid.index).cursorToX(local);
    return QRectF(x, *line * m_lineHeight, 1, m_lineHeight);
}

double TextLines::baseline(int line) const
{
    const Line &laid = m_lineList[size_t(line)];
    const QTextLine text = m_paragraphs[laid.paragraph]->lineAt(laid.index);
    return text.position().y() + text.ascent();
}

int TextLines::lineStart(int line) const
{
    const Line &laid = m_lineList[size_t(line)];
    return m_starts[laid.paragraph] + m_paragraphs[laid.paragraph]->lineAt(laid.index).textStart();
}

int TextLines::lineEnd(int line) const
{
    const Line &laid = m_lineList[size_t(line)];
    const QTextLayout &paragraph = *m_paragraphs[laid.paragraph];
    const QTextLine text = paragraph.lineAt(laid.index);
    const int end = text.textStart() + text.textLength();
    // A line ends before the space or separator breaking it.
    const bool wraps = (end < paragraph.text().size() || laid.index < paragraph.lineCount() - 1) && paragraph.text().at(end - 1).isSpace();
    return m_starts[laid.paragraph] + end - (wraps ? 1 : 0);
}

int TextLines::position(int line, double x) const
{
    const Line &laid = m_lineList[size_t(line)];
    return m_starts[laid.paragraph] + m_paragraphs[laid.paragraph]->lineAt(laid.index).xToCursor(x);
}

int TextLines::character(int line, double x) const
{
    const Line &laid = m_lineList[size_t(line)];
    return m_starts[laid.paragraph] + m_paragraphs[laid.paragraph]->lineAt(laid.index).xToCursor(x, QTextLine::CursorOnCharacter);
}
