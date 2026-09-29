#include "IO/PSD/PSDText+Descriptor.h"
#include <QStringDecoder>
#include <QtEndian>
#include <bit>
#include <cmath>

namespace PSDDescriptor {
namespace {
// Nesting past this reads as broken, where Swift's stack overflows.
constexpr int maxDepth = 512;

// Swift's ASCII decoding: a high byte fails.
std::optional<QString> ascii(const QByteArray &raw)
{
    for (const char byte : raw)
        if (uchar(byte) >= 0x80)
            return std::nullopt;
    return QString::fromLatin1(raw);
}

template <typename T>
const T *find(const Items &items, const QString &key)
{
    return items.contains(key) ? std::get_if<T>(&items.at(key).value) : nullptr;
}

// Swift's `??`: the key present decides, then its type.
std::optional<double> side(const Items &items, const QString &name)
{
    const double *found = find<double>(items, items.contains(name) ? name : name.trimmed());
    return found ? std::optional(*found) : std::nullopt;
}
}

std::optional<QString> string(const Items &items, const QString &key)
{
    const QString *found = find<QString>(items, key);
    return found ? std::optional(*found) : std::nullopt;
}

std::optional<QString> enumeration(const Items &items, const QString &key)
{
    const Enumeration *found = find<Enumeration>(items, key);
    return found ? std::optional(found->name) : std::nullopt;
}

std::optional<QByteArray> data(const Items &items, const QString &key)
{
    const QByteArray *found = find<QByteArray>(items, key);
    return found ? std::optional(*found) : std::nullopt;
}

QString utf16(const QByteArray &raw)
{
    QString text = QStringDecoder(QStringConverter::Utf16BE, QStringConverter::Flag::ConvertInitialBom).decode(raw);
    for (qsizetype index = 0; index < text.size(); ++index) {
        if (text[index].isHighSurrogate() && index + 1 < text.size() && text[index + 1].isLowSurrogate())
            ++index;
        else if (text[index].isSurrogate())
            text[index] = QChar::ReplacementCharacter;
    }
    return text;
}

std::optional<QRectF> rect(const Items &items, const QString &key)
{
    const Items *box = find<Items>(items, key);
    if (!box)
        return std::nullopt;
    const std::optional<double> left = side(*box, QStringLiteral("Left")), top = side(*box, QStringLiteral("Top ")),
                                right = side(*box, QStringLiteral("Rght")), bottom = side(*box, QStringLiteral("Btom"));
    if (!left || !top || !right || !bottom)
        return std::nullopt;
    if (!std::isfinite(*left) || !std::isfinite(*top) || !std::isfinite(*right) || !std::isfinite(*bottom))
        return std::nullopt;
    return QRectF(QPointF(*left, *top), QPointF(*right, *bottom)).normalized();
}

std::optional<Items> Reader::descriptor(bool versioned)
{
    if (versioned && u32() != 16u)
        return std::nullopt;
    if (!unicode() || !identifier())
        return std::nullopt;
    const std::optional<quint32> count = u32();
    if (!count || *count > 10'000)
        return std::nullopt;
    Items result;
    for (quint32 index = 0; index < *count; ++index) {
        const std::optional<QString> key = identifier();
        const std::optional<QString> type = key ? fourCC() : std::nullopt;
        std::optional<Value> item = type ? value(*type) : std::nullopt;
        if (!item)
            return std::nullopt;
        result.insert_or_assign(*key, std::move(*item));
    }
    return result;
}

std::optional<Value> Reader::value(const QString &type)
{
    if (depth >= maxDepth)
        return std::nullopt;
    ++depth;
    std::optional<Value> result = read(type);
    --depth;
    return result;
}

std::optional<Value> Reader::read(const QString &type)
{
    if (type == QLatin1String("doub")) {
        const std::optional<double> number = f64();
        return number ? std::optional(Value{*number}) : std::nullopt;
    }
    if (type == QLatin1String("UntF")) {
        const std::optional<double> number = fourCC() ? f64() : std::nullopt;
        return number ? std::optional(Value{*number}) : std::nullopt;
    }
    if (type == QLatin1String("long")) {
        const std::optional<qint32> number = i32();
        return number ? std::optional(Value{double(*number)}) : std::nullopt;
    }
    if (type == QLatin1String("comp")) {
        const std::optional<QByteArray> raw = bytes(8);
        return raw ? std::optional(Value{double(qFromBigEndian<qint64>(raw->constData()))}) : std::nullopt;
    }
    if (type == QLatin1String("bool"))
        return u8() ? std::optional(Value{0.0}) : std::nullopt;
    if (type == QLatin1String("TEXT")) {
        const std::optional<QString> text = unicode();
        return text ? std::optional(Value{*text}) : std::nullopt;
    }
    if (type == QLatin1String("enum")) {
        const std::optional<QString> name = identifier() ? identifier() : std::nullopt;
        return name ? std::optional(Value{Enumeration{*name}}) : std::nullopt;
    }
    if (type == QLatin1String("tdta")) {
        const std::optional<quint32> length = u32();
        const std::optional<QByteArray> raw = length && *length <= 8'000'000 ? bytes(*length) : std::nullopt;
        return raw ? std::optional(Value{*raw}) : std::nullopt;
    }
    if (type == QLatin1String("Objc") || type == QLatin1String("GlbO")) {
        std::optional<Items> nested = descriptor(false);
        return nested ? std::optional(Value{std::move(*nested)}) : std::nullopt;
    }
    if (type == QLatin1String("VlLs")) {
        const std::optional<quint32> count = u32();
        if (!count || *count > 10'000)
            return std::nullopt;
        std::vector<Value> list;
        for (quint32 index = 0; index < *count; ++index) {
            const std::optional<QString> itemType = fourCC();
            std::optional<Value> item = itemType ? value(*itemType) : std::nullopt;
            if (!item)
                return std::nullopt;
            list.push_back(std::move(*item));
        }
        return Value{std::move(list)};
    }
    if (type == QLatin1String("alis")) {
        const std::optional<quint32> length = u32();
        return length && *length <= 8'000'000 && bytes(*length) ? std::optional(Value{0.0}) : std::nullopt;
    }
    if (type == QLatin1String("obj "))
        return reference() ? std::optional(Value{0.0}) : std::nullopt;
    if (type == QLatin1String("type") || type == QLatin1String("GlbC"))
        return unicode() && identifier() ? std::optional(Value{0.0}) : std::nullopt;
    return std::nullopt;
}

bool Reader::reference()
{
    const std::optional<quint32> count = u32();
    if (!count || *count > 10'000)
        return false;
    for (quint32 index = 0; index < *count; ++index) {
        const std::optional<QString> form = fourCC();
        if (!form)
            return false;
        bool read = false;
        if (*form == QLatin1String("prop"))
            read = unicode() && identifier() && identifier();
        else if (*form == QLatin1String("Clss"))
            read = unicode() && identifier();
        else if (*form == QLatin1String("Enmr"))
            read = unicode() && identifier() && identifier() && identifier();
        else if (*form == QLatin1String("rele"))
            read = unicode() && identifier() && i32();
        else if (*form == QLatin1String("Idnt") || *form == QLatin1String("indx"))
            read = bool(i32());
        else if (*form == QLatin1String("name"))
            read = bool(unicode());
        if (!read)
            return false;
    }
    return true;
}

std::optional<QString> Reader::unicode()
{
    const std::optional<quint32> count = u32();
    const std::optional<QByteArray> raw = count && *count <= 1'000'000 ? bytes(qint64(*count) * 2) : std::nullopt;
    if (!raw)
        return std::nullopt;
    return utf16(*raw);
}

std::optional<QString> Reader::identifier()
{
    const std::optional<quint32> length = u32();
    if (!length)
        return std::nullopt;
    if (*length == 0)
        return fourCC();
    const std::optional<QByteArray> raw = *length <= 10'000 ? bytes(*length) : std::nullopt;
    return raw ? ascii(*raw) : std::nullopt;
}

std::optional<QString> Reader::fourCC()
{
    const std::optional<QByteArray> raw = bytes(4);
    return raw ? ascii(*raw) : std::nullopt;
}

std::optional<QByteArray> Reader::bytes(qint64 count)
{
    if (count < 0 || count > remaining())
        return std::nullopt;
    const QByteArray slice = data.mid(offset, count);
    offset += count;
    return slice;
}

std::optional<uchar> Reader::u8()
{
    const std::optional<QByteArray> raw = bytes(1);
    return raw ? std::optional(uchar(raw->at(0))) : std::nullopt;
}

std::optional<quint16> Reader::u16()
{
    const std::optional<QByteArray> raw = bytes(2);
    return raw ? std::optional(qFromBigEndian<quint16>(raw->constData())) : std::nullopt;
}

std::optional<quint32> Reader::u32()
{
    const std::optional<QByteArray> raw = bytes(4);
    return raw ? std::optional(qFromBigEndian<quint32>(raw->constData())) : std::nullopt;
}

std::optional<qint32> Reader::i32()
{
    const std::optional<quint32> value = u32();
    return value ? std::optional(qint32(*value)) : std::nullopt;
}

std::optional<double> Reader::f64()
{
    const std::optional<QByteArray> raw = bytes(8);
    return raw ? std::optional(std::bit_cast<double>(qFromBigEndian<quint64>(raw->constData()))) : std::nullopt;
}
}
