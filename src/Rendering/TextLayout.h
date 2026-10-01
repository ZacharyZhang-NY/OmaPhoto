#pragma once
#include "Document/TypeTool.h"
#include <QFont>
#include <QStringList>
#include <QTextLayout>
#include <memory>
#include <optional>
#include <vector>

class QPainter;

// Where Swift has TextKit: a style's font and lines.
namespace TextLayout {
// The face a PostScript name names, or Qt's match.
QFont font(const LayerTextStyle &style);
// The style's size and spacing in another face.
QFont font(const LayerTextStyle &style, const QString &face);
// Every installed face's PostScript name, sorted.
QStringList availableFonts();
}

// Content positions from `start` up to `end`, as NSRange.
struct TextRange {
    int start = 0;
    int end = 0;
    bool isEmpty() const { return start == end; }
    friend bool operator==(const TextRange &, const TextRange &) = default;
};

// A style's paragraphs in a container, as TextKit lays them.
class TextLines {
public:
    TextLines(const LayerTextStyle &style, QSizeF container);
    TextLines(const TextLines &) = delete;
    TextLines &operator=(const TextLines &) = delete;
    // The lines' widest width and their heights together.
    QSizeF usedSize() const;
    // Whether text fell past the container's bottom.
    bool overflows() const { return m_overflows; }
    // A selection shows behind the text; formats in content positions.
    void draw(QPainter &painter, QPointF origin, TextRange selection = {}, const QList<QTextLayout::FormatRange> &formats = {},
              const QColor &highlight = QColor()) const;
    int lineCount() const { return int(m_lineList.size()); }
    // The laid line holding a content position, if any.
    std::optional<int> lineOf(int position) const;
    // The caret before a position: a unit wide.
    std::optional<QRectF> caret(int position) const;
    // A laid line's baseline from the container's top.
    double baseline(int line) const;
    // A line's first position; its last before any wrap.
    int lineStart(int line) const;
    int lineEnd(int line) const;
    // The position nearest `x` on a laid line.
    int position(int line, double x) const;
    // The character under `x` on a laid line.
    int character(int line, double x) const;

private:
    struct Line {
        size_t paragraph;
        int index;
    };
    std::vector<std::unique_ptr<QTextLayout>> m_paragraphs;
    // Each laid paragraph's first content position.
    std::vector<int> m_starts;
    void drawSelection(QPainter &painter, QPointF origin, int line, TextRange selection, const QColor &highlight) const;
    std::vector<Line> m_lineList;
    double m_lineHeight;
    double m_width;
    // Where the laid text ends: past it, no line.
    int m_laidEnd;
    bool m_overflows = false;
    // TextKit's line after a final newline, the caret's alone.
    bool m_caretLine = false;
};
