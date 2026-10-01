#include "Document/ColorPalette.h"
#include "Document/TypeTool.h"
#include <QTextBoundaryFinder>
#include <algorithm>
#include <limits>

// LayerTextStyle's letters in colours and faces of their own.
bool LayerTextStyle::colorRunsAreValid() const
{
    if (!colorRuns)
        return true;
    qint64 end = 0;
    for (const LayerTextColorRun &run : *colorRuns) {
        // Each after the last, not empty, its end within Int.
        if (run.location < end || run.length <= 0 || run.location > std::numeric_limits<qint64>::max() - run.length)
            return false;
        for (const double channel : {run.red, run.green, run.blue}) {
            if (!(channel >= 0 && channel <= 1))
                return false;
        }
        end = run.location + run.length;
    }
    return !colorRuns->empty() && end <= content.size();
}

namespace {
// Swift's guard: a name, 200 characters at most, no newline.
bool usableFontName(const QString &name)
{
    if (name.isEmpty())
        return false;
    for (const QChar unit : name) {
        if (unit == u'\n' || unit == u'\r' || unit == u'\v' || unit == u'\f' || unit == QChar(0x85) || unit == QChar::LineSeparator || unit == QChar::ParagraphSeparator)
            return false;
    }
    QTextBoundaryFinder characters(QTextBoundaryFinder::Grapheme, name);
    int count = 0;
    while (characters.toNextBoundary() >= 0)
        ++count;
    return count <= 200;
}
}

bool LayerTextStyle::fontRunsAreValid() const
{
    if (!fontRuns)
        return true;
    qint64 end = 0;
    for (const LayerTextFontRun &run : *fontRuns) {
        if (run.location < end || run.length <= 0 || run.location > std::numeric_limits<qint64>::max() - run.length || !usableFontName(run.fontName))
            return false;
        end = run.location + run.length;
    }
    return !fontRuns->empty() && end <= content.size();
}

PaletteColor LayerTextStyle::color(qint64 index) const
{
    for (const LayerTextColorRun &run : colorRuns.value_or(std::vector<LayerTextColorRun>())) {
        if (run.location <= index && index < run.location + run.length)
            return {run.red, run.green, run.blue};
    }
    return {red, green, blue};
}

void LayerTextStyle::setColor(const PaletteColor &color, TextSpan span)
{
    const qint64 count = content.size();
    const qint64 start = std::clamp<qint64>(span.location, 0, count);
    const qint64 end = std::max(start, std::min(span.location + span.length, count));
    if (start == end || (start == 0 && end == count)) {
        red = color.red;
        green = color.green;
        blue = color.blue;
        colorRuns = std::nullopt;
        return;
    }
    std::vector<PaletteColor> colors = unitColors();
    std::fill(colors.begin() + start, colors.begin() + end, color);
    setUnitColors(colors);
}

QString LayerTextStyle::fontNameAt(qint64 index) const
{
    for (const LayerTextFontRun &run : fontRuns.value_or(std::vector<LayerTextFontRun>())) {
        if (run.location <= index && index < run.location + run.length)
            return run.fontName;
    }
    return fontName;
}

std::optional<QString> LayerTextStyle::uniformFontName(TextSpan span) const
{
    const qint64 count = content.size();
    const qint64 start = std::clamp<qint64>(span.location, 0, count);
    const qint64 end = std::max(start, std::min(span.location + span.length, count));
    if (end <= start)
        return std::nullopt;
    const QString face = fontNameAt(start);
    qint64 index = start;
    for (const LayerTextFontRun &run : fontRuns.value_or(std::vector<LayerTextFontRun>())) {
        if (run.location >= end || run.location + run.length <= index)
            continue;
        // A gap in the base face, or another run face.
        if ((run.location > index && fontName != face) || run.fontName != face)
            return std::nullopt;
        index = std::min(end, std::max(index, run.location + run.length));
    }
    if (index < end && fontName != face)
        return std::nullopt;
    return face;
}

void LayerTextStyle::setFont(const QString &name, TextSpan span)
{
    if (!usableFontName(name))
        return;
    const qint64 count = content.size();
    const qint64 start = std::clamp<qint64>(span.location, 0, count);
    const qint64 end = std::max(start, std::min(span.location + span.length, count));
    if (start == end || (start == 0 && end == count)) {
        fontName = name;
        fontRuns = std::nullopt;
        return;
    }
    std::vector<QString> fonts = unitFonts();
    std::fill(fonts.begin() + start, fonts.begin() + end, name);
    setUnitFonts(fonts);
}

void LayerTextStyle::replaceCharacters(TextSpan span, qint64 length)
{
    const qint64 count = content.size();
    const qint64 start = std::clamp<qint64>(span.location, 0, count);
    const qint64 end = std::max(start, std::min(span.location + span.length, count));
    // New letters take the colour and face before them.
    if (colorRuns) {
        std::vector<PaletteColor> colors = unitColors();
        const PaletteColor inherited = start > 0 ? colors[size_t(start - 1)] : end > start ? colors[size_t(start)] : !colors.empty() ? colors.front() : PaletteColor{red, green, blue};
        colors.erase(colors.begin() + start, colors.begin() + end);
        colors.insert(colors.begin() + start, size_t(std::max<qint64>(0, length)), inherited);
        setUnitColors(colors);
    }
    if (fontRuns) {
        std::vector<QString> fonts = unitFonts();
        const QString inherited = start > 0 ? fonts[size_t(start - 1)] : end > start ? fonts[size_t(start)] : !fonts.empty() ? fonts.front() : fontName;
        fonts.erase(fonts.begin() + start, fonts.begin() + end);
        fonts.insert(fonts.begin() + start, size_t(std::max<qint64>(0, length)), inherited);
        setUnitFonts(fonts);
    }
}

std::vector<PaletteColor> LayerTextStyle::unitColors() const
{
    std::vector<PaletteColor> colors(size_t(content.size()), PaletteColor{red, green, blue});
    for (const LayerTextColorRun &run : colorRuns.value_or(std::vector<LayerTextColorRun>())) {
        const qint64 from = std::max<qint64>(0, run.location), to = std::min<qint64>(qint64(colors.size()), run.location + run.length);
        for (qint64 index = from; index < to; ++index)
            colors[size_t(index)] = {run.red, run.green, run.blue};
    }
    return colors;
}

void LayerTextStyle::setUnitColors(const std::vector<PaletteColor> &colors)
{
    const PaletteColor base{red, green, blue};
    std::vector<LayerTextColorRun> runs;
    for (size_t index = 0; index < colors.size(); ++index) {
        const PaletteColor &color = colors[index];
        if (color == base)
            continue;
        if (!runs.empty() && size_t(runs.back().location + runs.back().length) == index
            && PaletteColor{runs.back().red, runs.back().green, runs.back().blue} == color)
            runs.back().length += 1;
        else
            runs.push_back({qint64(index), 1, color.red, color.green, color.blue});
    }
    colorRuns = runs.empty() ? std::nullopt : std::optional(runs);
}

std::vector<QString> LayerTextStyle::unitFonts() const
{
    std::vector<QString> fonts(size_t(content.size()), fontName);
    for (const LayerTextFontRun &run : fontRuns.value_or(std::vector<LayerTextFontRun>())) {
        const qint64 from = std::max<qint64>(0, run.location), to = std::min<qint64>(qint64(fonts.size()), run.location + run.length);
        for (qint64 index = from; index < to; ++index)
            fonts[size_t(index)] = run.fontName;
    }
    return fonts;
}

void LayerTextStyle::setUnitFonts(const std::vector<QString> &fonts)
{
    // One face throughout becomes the text's own.
    if (!fonts.empty() && std::all_of(fonts.begin(), fonts.end(), [&](const QString &name) { return name == fonts.front(); })) {
        fontName = fonts.front();
        fontRuns = std::nullopt;
        return;
    }
    std::vector<LayerTextFontRun> runs;
    for (size_t index = 0; index < fonts.size(); ++index) {
        if (fonts[index] == fontName)
            continue;
        if (!runs.empty() && size_t(runs.back().location + runs.back().length) == index && runs.back().fontName == fonts[index])
            runs.back().length += 1;
        else
            runs.push_back({qint64(index), 1, fonts[index]});
    }
    fontRuns = runs.empty() ? std::nullopt : std::optional(runs);
}
