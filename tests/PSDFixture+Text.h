#pragma once
#include "PSDFixture.h"
#include <bit>

// Swift's `PSDFixture.tySh`: a Photoshop 6 type block.
namespace PSDFixture {
struct TypeBlock {
    QString text;
    QString font = QStringLiteral("Helvetica");
    double fontSize = 24;
    double red = 0, green = 0, blue = 0;
    QByteArray justification = "0";
    QByteArray fontIndex = "0";
    // Without it the words come from the engine alone.
    bool typed = true;
    // Swift's other forms: bare runs, no engine at all.
    bool sheet = true;
    bool withEngine = true;
    QByteArray tracking = "0";
    std::optional<double> leading;
    bool fauxBold = false, fauxItalic = false, vertical = false, warp = false;
    std::optional<double> secondSize, secondLeading, secondHorizontalScale, secondVerticalScale;
    // Keys the second run's sheet repeats, later winning.
    QByteArray secondOverride;
    // The fill's values as written, else ARGB from the channels.
    std::optional<QByteArray> values;
    // The engine's own words, else `text`.
    std::optional<QString> engineText;
    // A second face in the set; the words' own key.
    std::optional<QString> secondFont;
    QByteArray wordsKey = "Txt ";
    double tx = 40, ty = 50;
    double xx = 1, xy = 0, yx = 0, yy = 1;
    std::optional<std::array<double, 4>> bounds, glyphBounds;
};

inline void f64(Buffer &buffer, double value)
{
    const quint64 bits = std::bit_cast<quint64>(value);
    buffer.u32(quint32(bits >> 32));
    buffer.u32(quint32(bits));
}

// A four-letter key takes a zero length.
inline void id(Buffer &buffer, const QByteArray &value)
{
    buffer.u32(value.size() == 4 ? 0 : quint32(value.size()));
    buffer.bytes(value);
}

inline void descriptor(Buffer &buffer, const QByteArray &classID, const std::vector<std::pair<QByteArray, QByteArray>> &items)
{
    buffer.u32(16);
    buffer.u32(0);
    id(buffer, classID);
    buffer.u32(quint32(items.size()));
    for (const auto &[key, value] : items) {
        id(buffer, key);
        buffer.bytes(value);
    }
}

inline QByteArray textItem(const QString &text)
{
    Buffer item;
    item.string("TEXT");
    item.u32(quint32(text.size()));
    for (const QChar unit : text)
        item.u16(unit.unicode());
    return item.data;
}

inline QByteArray enumItem(const QByteArray &type, const QByteArray &value)
{
    Buffer item;
    item.string("enum");
    id(item, type);
    id(item, value);
    return item.data;
}

inline QByteArray rawItem(const QByteArray &payload)
{
    Buffer item;
    item.string("tdta");
    item.u32(quint32(payload.size()));
    item.bytes(payload);
    return item.data;
}

inline QByteArray rectItem(const std::array<double, 4> &box)
{
    Buffer item;
    item.string("Objc");
    item.u32(0);
    id(item, "Rctn");
    item.u32(4);
    const std::array<QByteArray, 4> keys{"Left", "Top ", "Rght", "Btom"};
    for (size_t index = 0; index < keys.size(); ++index) {
        id(item, keys[index]);
        item.string("UntF");
        item.string("#Pnt");
        f64(item, box[index]);
    }
    return item.data;
}

// Swift's `parenthesized`: backslashes before the three specials.
inline QByteArray parenthesized(const QString &text)
{
    QByteArray encoded("(");
    for (const char byte : text.toUtf8()) {
        if (byte == '\\' || byte == '(' || byte == ')')
            encoded.append('\\');
        encoded.append(byte);
    }
    return encoded + ')';
}

inline QByteArray number(double value)
{
    return QByteArray::number(value, 'g', 17);
}

inline QByteArray flag(bool value)
{
    return value ? "true" : "false";
}

inline QByteArray run(const TypeBlock &block, double size, std::optional<double> leading, double horizontal, double vertical,
                      const QByteArray &override = {})
{
    const QByteArray open = block.sheet ? "<<\n/StyleSheet\n<<\n/StyleSheetData\n<<\n" : "<<\n";
    const QByteArray close = block.sheet ? " ]\n>>\n>>\n>>\n>>" : " ]\n>>\n>>";
    return open + "/Font " + block.fontIndex + "\n/FontSize " + number(size) + "\n/FauxBold " + flag(block.fauxBold)
        + "\n/FauxItalic " + flag(block.fauxItalic) + "\n/AutoLeading " + flag(!leading) + "\n/Leading " + number(leading.value_or(size * 1.2))
        + "\n/Tracking " + block.tracking + "\n/HorizontalScale " + number(horizontal) + "\n/VerticalScale " + number(vertical)
        + "\n/FillColor\n<<\n/Type 1\n/Values [ "
        + block.values.value_or("1.0 " + number(block.red) + " " + number(block.green) + " " + number(block.blue))
        + " ]\n>>\n" + override + close.mid(6);
}

inline QByteArray engine(const TypeBlock &block)
{
    QByteArray runs = run(block, block.fontSize, block.leading, 1, 1);
    if (block.secondSize || block.secondLeading || block.secondHorizontalScale || block.secondVerticalScale || !block.secondOverride.isEmpty())
        runs += "\n" + run(block, block.secondSize.value_or(block.fontSize), block.secondLeading ? block.secondLeading : block.leading,
                           block.secondHorizontalScale.value_or(1), block.secondVerticalScale.value_or(1), block.secondOverride);
    return "<<\n/EngineDict\n<<\n/Editor\n<<\n/Text " + parenthesized(block.engineText.value_or(block.text)) + "\n>>\n/ParagraphRun\n<<\n/RunArray\n[\n<<\n"
        + "/ParagraphSheet\n<<\n/Properties\n<<\n/Justification " + block.justification + "\n>>\n>>\n>>\n]\n>>\n"
        + "/StyleRun\n<<\n/RunArray\n[\n" + runs + "\n]\n>>\n>>\n/ResourceDict\n<<\n/FontSet\n[\n<<\n/Name " + parenthesized(block.font)
        + "\n>>\n" + (block.secondFont ? "<<\n/Name " + parenthesized(*block.secondFont) + "\n>>\n" : QByteArray()) + "]\n>>\n>>";
}

inline QByteArray tySh(const TypeBlock &block)
{
    Buffer data;
    data.u16(1);
    for (const double value : {block.xx, block.xy, block.yx, block.yy, block.tx, block.ty})
        f64(data, value);
    data.u16(50);
    std::vector<std::pair<QByteArray, QByteArray>> items;
    if (block.typed)
        items.emplace_back(block.wordsKey, textItem(block.text));
    items.emplace_back("Ornt", enumItem("Ornt", block.vertical ? "Vrtc" : "Hrzn"));
    if (block.bounds)
        items.emplace_back("bounds", rectItem(*block.bounds));
    if (block.glyphBounds)
        items.emplace_back("boundingBox", rectItem(*block.glyphBounds));
    if (block.withEngine)
        items.emplace_back("EngineData", rawItem(engine(block)));
    descriptor(data, "TxLr", items);
    data.u16(1);
    descriptor(data, "warp", {{"warpStyle", enumItem("warpStyle", block.warp ? "warpArc" : "warpNone")}});
    return data.data;
}

inline std::map<QString, QByteArray> typeExtra(const TypeBlock &block)
{
    return {{QStringLiteral("TySh"), tySh(block)}};
}
}
