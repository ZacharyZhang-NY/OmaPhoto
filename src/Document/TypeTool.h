#pragma once
#include "Document/LayerTransform.h"
#include "IO/ImageImporter.h"
#include <QSizeF>
#include <QString>
#include <QUuid>
#include <array>
#include <optional>
#include <vector>

enum class TextAlignment { left, center, right };
inline constexpr std::array allTextAlignments{TextAlignment::left, TextAlignment::center, TextAlignment::right};
QString rawValue(TextAlignment alignment);
std::optional<TextAlignment> textAlignment(const QString &text);

struct PaletteColor;

// Swift's NSRange: UTF-16 units into a text's content.
struct TextSpan {
    qint64 location = 0;
    qint64 length = 0;
    friend bool operator==(const TextSpan &, const TextSpan &) = default;
};

// Letters in a colour of their own, as Swift's LayerTextColorRun.
struct LayerTextColorRun {
    qint64 location = 0;
    qint64 length = 0;
    double red = 0;
    double green = 0;
    double blue = 0;
    friend bool operator==(const LayerTextColorRun &, const LayerTextColorRun &) = default;
};

// What a text layer says and how, in layer pixels.
struct LayerTextStyle {
    QString content = QStringLiteral("Text");
    // A PostScript name, as macOS names faces.
    QString fontName = QStringLiteral("Helvetica");
    double fontSize = 72;
    double red = 0;
    double green = 0;
    double blue = 0;
    TextAlignment alignment = TextAlignment::left;
    double tracking = 0;
    // Baseline to baseline; 0 is Auto, 120% of the size.
    double leading = 0;
    // A paragraph's fixed box; none makes point text.
    std::optional<QSizeF> boxSize = std::nullopt;
    // Sorted, apart, within the content; none is one colour.
    std::optional<std::vector<LayerTextColorRun>> colorRuns = std::nullopt;
    // The gap between the text and its box, either kind.
    static constexpr double padding = 12;
    double autoLeading() const { return fontSize * 1.2; }
    double lineHeight() const { return leading > 0 ? leading : autoLeading(); }
    bool boxIsValid() const;
    bool isValid() const;
    // The colour of the UTF-16 unit at `index`.
    PaletteColor color(qint64 index) const;
    // Paints `span`; empty or whole, it recolours all the text.
    void setColor(const PaletteColor &color, TextSpan span);
    // Moves the colours as `span` becomes `length` units.
    void replaceCharacters(TextSpan span, qint64 length);
    friend bool operator==(const LayerTextStyle &, const LayerTextStyle &) = default;

private:
    bool colorRunsAreValid() const;
    std::vector<PaletteColor> unitColors() const;
    void setUnitColors(const std::vector<PaletteColor> &colors);
};

// A text layer: its style and the image it drew.
struct LayerText {
    LayerTextStyle style;
    ImageIdentity image;
    static std::optional<LayerText> loaded(const std::optional<LayerTextStyle> &style, const std::optional<ImportedImage> &image);
    friend bool operator==(const LayerText &, const LayerText &) = default;
};

// Text being typed, before it becomes or changes a layer.
struct TextDraft {
    QUuid id = QUuid::createUuid();
    QUuid documentID;
    std::optional<QUuid> layerID;
    QPointF origin;
    std::optional<LayerTransform> transform = std::nullopt;
    LayerTextStyle style;
    // What the editor selects; colour applies to it alone.
    TextSpan selection;
    friend bool operator==(const TextDraft &, const TextDraft &) = default;
};
