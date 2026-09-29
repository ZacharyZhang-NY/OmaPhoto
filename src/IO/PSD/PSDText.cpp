#include "IO/PSD/PSDText.h"
#include "Document/EditorSession.h"
#include "IO/PSD/PSDText+Descriptor.h"
#include "IO/PSD/PSDText+Engine.h"
#include "IO/ProjectStore.h"
#include "Rendering/TextLayout.h"
#include <cmath>
#include <numbers>

namespace PSDText {
namespace {
using namespace PSDTextEngine;

// Uniform scale, a turn and a vertical flip; no shear.
struct Placement {
    double pixelScale;
    double rotation;
    bool flipY;
    QTransform map;
};

// The matrix alone maps engine sizes to pixels.
std::optional<Placement> placement(double xx, double xy, double yx, double yy, double tx, double ty)
{
    const double scaleX = std::hypot(xx, yx);
    if (!(scaleX > 1e-6))
        return std::nullopt;
    const double cosR = xx / scaleX, sinR = yx / scaleX;
    const double localX = cosR * xy + sinR * yy;
    const double localY = -sinR * xy + cosR * yy;
    const double scaleY = std::abs(localY);
    if (!(scaleY > 1e-6))
        return std::nullopt;
    const double largest = std::max(scaleX, scaleY);
    if (!(std::abs(localX) <= 0.02 * largest && std::abs(scaleX - scaleY) <= 0.02 * largest))
        return std::nullopt;
    const double ySign = localY < 0 ? -1.0 : 1.0;
    // Swift's exx, eyx, exy and eyy, then the offset.
    const QTransform map(cosR * scaleX, sinR * scaleX, -sinR * scaleX * ySign, cosR * scaleX * ySign, tx, ty);
    return Placement{scaleX, std::atan2(sinR, cosR) * 180 / std::numbers::pi, localY < 0, map};
}

// Swift's `Int(x.rounded())`, with no trap on a wild value.
std::optional<qint64> rounded(double value)
{
    const double whole = std::round(value);
    if (!(std::abs(whole) < 9e18))
        return std::nullopt;
    return qint64(whole);
}

double unit(double value)
{
    return value > 1 ? std::min(255.0, std::max(0.0, value)) / 255 : std::min(1.0, std::max(0.0, value));
}

std::array<double, 3> color(const std::vector<double> &values)
{
    if (values.size() >= 4)
        return {unit(values[1]), unit(values[2]), unit(values[3])};
    if (values.size() == 3)
        return {unit(values[0]), unit(values[1]), unit(values[2])};
    if (!values.empty())
        return {unit(values[0]), unit(values[0]), unit(values[0])};
    return {0, 0, 0};
}

std::vector<double> channels(const Engine *data)
{
    std::vector<double> result;
    for (const Engine &value : array(walk(data, {"FillColor", "Values"})))
        if (const std::optional<double> channel = number(&value))
            result.push_back(*channel);
    return result;
}

struct Signature {
    double font = 0, size = 0, tracking = 0, leading = 0;
    bool autoLeading = true;
    double horizontalScale = 1, verticalScale = 1;
    bool bold = false, italic = false;
    std::array<double, 3> rgb{};
    friend bool operator==(const Signature &, const Signature &) = default;
};

Signature signature(const Engine &run)
{
    const Engine *sheet = walk(&run, {"StyleSheet", "StyleSheetData"});
    const Engine *data = sheet ? sheet : &run;
    return Signature{.font = number(walk(data, {"Font"})).value_or(0),
                     .size = number(walk(data, {"FontSize"})).value_or(0),
                     .tracking = number(walk(data, {"Tracking"})).value_or(0),
                     .leading = number(walk(data, {"Leading"})).value_or(0),
                     .autoLeading = boolean(walk(data, {"AutoLeading"})).value_or(true),
                     .horizontalScale = number(walk(data, {"HorizontalScale"})).value_or(1),
                     .verticalScale = number(walk(data, {"VerticalScale"})).value_or(1),
                     .bold = boolean(walk(data, {"FauxBold"})).value_or(false),
                     .italic = boolean(walk(data, {"FauxItalic"})).value_or(false),
                     .rgb = color(channels(data))};
}

void applyStyle(LayerTextStyle &style, const Engine &engine, double pixelScale, std::vector<QString> &notes)
{
    const Array runs = array(walk(&engine, {"EngineDict", "StyleRun", "RunArray"}));
    const Engine &first = runs.empty() ? engine : runs.front();
    // Swift falls back on the run array, where nothing reads.
    const Engine *sheet = walk(&first, {"StyleSheet", "StyleSheetData"});
    const Engine *data = sheet ? sheet : walk(&engine, {"EngineDict", "StyleRun", "RunArray"});
    if (!data)
        data = &first;
    const double points = number(walk(data, {"FontSize"})).value_or(12);
    if (!(std::isfinite(points) && points > 0))
        return;
    style.fontSize = std::min(2000.0, std::max(1.0, points * pixelScale));
    const Array fonts = array(walk(&engine, {"ResourceDict", "FontSet"}));
    const std::optional<qint64> index = rounded(number(walk(data, {"Font"})).value_or(0));
    if (index && *index >= 0 && *index < qint64(fonts.size())) {
        const QString name = string(walk(&fonts[size_t(*index)], {"Name"})).value_or(QString());
        if (!name.isEmpty())
            style.fontName = name;
    }
    if (!array(walk(data, {"FillColor", "Values"})).empty()) {
        const std::array<double, 3> rgb = color(channels(data));
        style.red = rgb[0];
        style.green = rgb[1];
        style.blue = rgb[2];
    }
    const double tracking = number(walk(data, {"Tracking"})).value_or(0);
    if (std::isfinite(tracking))
        style.tracking = std::min(1000.0, std::max(-100.0, tracking * style.fontSize / 1000));
    const bool automatic = boolean(walk(data, {"AutoLeading"})).value_or(true);
    const std::optional<double> leading = number(walk(data, {"Leading"}));
    if (!automatic && leading && std::isfinite(*leading) && *leading > 0)
        style.leading = std::min(5000.0, std::max(0.0, *leading * pixelScale));
    if (boolean(walk(data, {"FauxBold"})) == true || boolean(walk(data, {"FauxItalic"})) == true)
        notes.push_back(fauxNote);
    const Signature firstSignature = signature(first);
    if (runs.size() > 1 && std::any_of(runs.begin() + 1, runs.end(), [&](const Engine &run) { return signature(run) != firstSignature; }))
        notes.push_back(firstStyleNote);
    const Array paragraphs = array(walk(&engine, {"EngineDict", "ParagraphRun", "RunArray"}));
    const Engine &paragraph = paragraphs.empty() ? engine : paragraphs.front();
    const std::optional<qint64> justification = rounded(number(walk(&paragraph, {"ParagraphSheet", "Properties", "Justification"})).value_or(0));
    if (justification == 1) {
        style.alignment = TextAlignment::right;
    } else if (justification == 2) {
        style.alignment = TextAlignment::center;
    } else {
        style.alignment = TextAlignment::left;
        if (justification != 0)
            notes.push_back(justifyNote);
    }
}

std::optional<QString> cleaned(std::optional<QString> text)
{
    if (!text)
        return std::nullopt;
    while (!text->isEmpty() && (text->front() == QChar(0xFEFF) || text->front() == QChar(0)))
        text->remove(0, 1);
    while (!text->isEmpty() && text->back() == QChar(0))
        text->chop(1);
    text->replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text->replace(QChar('\r'), QChar('\n'));
    return text;
}

double horizontalAnchor(const LayerTextStyle &style, double width)
{
    switch (style.alignment) {
    case TextAlignment::left:
        return LayerTextStyle::padding;
    case TextAlignment::center:
        return width / 2;
    case TextAlignment::right:
        return width - LayerTextStyle::padding;
    }
    Q_UNREACHABLE();
}

// The first line's baseline, laid as `textImage` lays it.
double baseline(const LayerTextStyle &style, QSizeF image)
{
    const double padding = LayerTextStyle::padding;
    const TextLines lines(style, QSizeF(std::max(1.0, image.width() - 2 * padding), std::max(1.0, image.height() - 2 * padding)));
    return padding + lines.baseline(0);
}

// As `pixelToDocument`: flip, then a clockwise turn about the centre.
LayerTransform layerTransform(QSizeF image, QPointF imageAnchor, QPointF documentAnchor, double rotation, bool flipY)
{
    QPointF local(imageAnchor.x() - image.width() / 2, imageAnchor.y() - image.height() / 2);
    if (flipY)
        local.setY(-local.y());
    const double radians = rotation * std::numbers::pi / 180;
    const QPointF turned(local.x() * std::cos(radians) - local.y() * std::sin(radians), local.x() * std::sin(radians) + local.y() * std::cos(radians));
    const QPointF centre = documentAnchor - turned;
    LayerTransform transform{QPointF(centre.x() - image.width() / 2, centre.y() - image.height() / 2), image};
    transform.rotation = rotation;
    transform.flipY = flipY;
    return transform;
}
}

std::optional<QString> missingFontNote(const QString &name)
{
    if (TextLayout::availableFonts().contains(name))
        return std::nullopt;
    return QStringLiteral("The font “%1” isn’t installed, so the text was drawn with a substitute font.").arg(name);
}

std::optional<Source> parse(const std::map<QString, QByteArray> &extra)
{
    const QString key = extra.contains(QStringLiteral("TySh")) ? QStringLiteral("TySh") : QStringLiteral("tySh");
    if (!extra.contains(key) || extra.at(key).size() > 8'000'000)
        return std::nullopt;
    PSDDescriptor::Reader reader{extra.at(key)};
    if (reader.u16() != 1u)
        return std::nullopt;
    std::array<double, 6> matrix{};
    for (double &value : matrix) {
        const std::optional<double> read = reader.f64();
        if (!read || !std::isfinite(*read))
            return std::nullopt;
        value = *read;
    }
    if (reader.u16() != 50u)
        return std::nullopt;
    const std::optional<PSDDescriptor::Items> text = reader.descriptor(true);
    if (!text || PSDDescriptor::enumeration(*text, QStringLiteral("Ornt")) == QStringLiteral("Vrtc"))
        return std::nullopt;
    const std::optional<Placement> placed = placement(matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5]);
    if (!placed)
        return std::nullopt;

    std::vector<QString> notes;
    if (reader.remaining() >= 2 && reader.u16() == 1u) {
        const std::optional<PSDDescriptor::Items> warp = reader.descriptor(true);
        const std::optional<QString> style = warp ? PSDDescriptor::enumeration(*warp, QStringLiteral("warpStyle")) : std::nullopt;
        if (style && *style != QLatin1String("warpNone") && *style != QLatin1String("none"))
            notes.push_back(warpNote);
    }

    const std::optional<QByteArray> engineData = PSDDescriptor::data(*text, QStringLiteral("EngineData"));
    const std::optional<Engine> engine = engineData ? PSDTextEngine::parse(*engineData) : std::nullopt;
    std::optional<QString> typed = PSDDescriptor::string(*text, QStringLiteral("Txt "));
    if (!typed)
        typed = PSDDescriptor::string(*text, QStringLiteral("Txt"));
    std::optional<QString> content = cleaned(typed);
    if (!content && engine)
        content = cleaned(string(walk(&*engine, {"EngineDict", "Editor", "Text"})));
    if (!content || content->isEmpty() || content->size() > 100'000)
        return std::nullopt;

    LayerTextStyle style;
    style.content = *content;
    if (engine)
        applyStyle(style, *engine, placed->pixelScale, notes);
    else
        style.fontSize = std::min(2000.0, std::max(1.0, 12 * placed->pixelScale));

    QPointF anchor(matrix[4], matrix[5]);
    bool anchorIsFrame = false;
    const std::optional<QRectF> bounds = PSDDescriptor::rect(*text, QStringLiteral("bounds"));
    const std::optional<QRectF> glyphs = PSDDescriptor::rect(*text, QStringLiteral("boundingBox"));
    if (bounds && glyphs && bounds->width() > glyphs->width() + 4 && bounds->height() > glyphs->height() + 4) {
        const double pad = LayerTextStyle::padding;
        style.boxSize = QSizeF(bounds->width() * placed->pixelScale + pad * 2, bounds->height() * placed->pixelScale + pad * 2);
        anchor = placed->map.map(bounds->topLeft());
        anchorIsFrame = true;
    }
    if (!style.isValid())
        return std::nullopt;
    return Source{style, notes, anchor, placed->rotation, placed->flipY, anchorIsFrame};
}

Rendered render(const Source &source)
{
    const QImage image = EditorSession::textImage(source.style);
    const QSizeF size = image.size();
    const QPointF anchor = source.anchorIsFrame ? QPointF(LayerTextStyle::padding, LayerTextStyle::padding)
                                                : QPointF(horizontalAnchor(source.style, size.width()), baseline(source.style, size));
    const LayerTransform transform = layerTransform(size, anchor, source.documentAnchor, source.rotation, source.flipY);
    if (!transform.isValid())
        throw ProjectError(ProjectError::Kind::tooLarge);
    return Rendered{image, transform};
}
}
