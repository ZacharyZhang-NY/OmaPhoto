#include "IO/PSD/PSDText+Engine.h"
#include "IO/PSD/PSDText+Descriptor.h"
#include <clocale>
#include <cstdlib>
#include <locale.h>

namespace PSDTextEngine {
namespace {
// Nesting past this reads as broken, where Swift's stack overflows.
constexpr int maxDepth = 512;

bool isDelimiter(uchar byte)
{
    return byte <= 0x20 || byte == '/' || byte == '<' || byte == '>' || byte == '[' || byte == ']' || byte == '(' || byte == ')';
}

bool isDigit(uchar byte)
{
    return byte >= '0' && byte <= '9';
}

std::optional<uchar> hex(uchar byte)
{
    if (isDigit(byte))
        return uchar(byte - '0');
    if (byte >= 'a' && byte <= 'f')
        return uchar(byte - 'a' + 10);
    if (byte >= 'A' && byte <= 'F')
        return uchar(byte - 'A' + 10);
    return std::nullopt;
}

// Swift's `Double(text)`: whole text, C numbers, overflow infinite.
std::optional<double> toDouble(const QByteArray &text)
{
    static const locale_t c = newlocale(LC_ALL_MASK, "C", locale_t(nullptr));
    char *end = nullptr;
    const double value = strtod_l(text.constData(), &end, c);
    if (end != text.constData() + text.size())
        return std::nullopt;
    return value;
}

QString decoded(const QByteArray &raw)
{
    if (raw.size() >= 2 && uchar(raw[0]) == 0xFE && uchar(raw[1]) == 0xFF) {
        // Foundation reads no odd count of UTF-16 bytes.
        if (raw.size() % 2 == 1)
            return QString();
        return PSDDescriptor::utf16(raw.mid(2));
    }
    return QString::fromLatin1(raw);
}

struct Cursor {
    const QByteArray &bytes;
    qsizetype index = 0;
    int depth = 0;

    std::optional<uchar> peek(qsizetype ahead = 0) const
    {
        const qsizetype at = index + ahead;
        return at < bytes.size() ? std::optional(uchar(bytes[at])) : std::nullopt;
    }

    bool take(const char *token)
    {
        const QByteArray encoded(token);
        if (bytes.mid(index, encoded.size()) != encoded)
            return false;
        index += encoded.size();
        return true;
    }

    bool takeWord(const char *word)
    {
        const QByteArray encoded(word);
        if (bytes.mid(index, encoded.size()) != encoded)
            return false;
        const qsizetype after = index + encoded.size();
        if (after < bytes.size() && !isDelimiter(uchar(bytes[after])))
            return false;
        index = after;
        return true;
    }

    void skipWhitespace()
    {
        while (peek() && (*peek() <= 0x20 || *peek() == '%')) {
            if (*peek() == '%') {
                while (peek() && *peek() != '\n' && *peek() != '\r')
                    ++index;
            } else {
                ++index;
            }
        }
    }

    // Swift's ASCII decoding: a high byte leaves the name empty.
    QString readToken()
    {
        const qsizetype start = index;
        while (peek() && !isDelimiter(*peek()))
            ++index;
        const QByteArray token = bytes.mid(start, index - start);
        for (const char byte : token)
            if (uchar(byte) >= 0x80)
                return QString();
        return QString::fromLatin1(token);
    }

    std::optional<Engine> value()
    {
        if (depth >= maxDepth)
            return std::nullopt;
        ++depth;
        std::optional<Engine> result = parseValue();
        --depth;
        return result;
    }

    std::optional<Engine> parseValue()
    {
        skipWhitespace();
        const std::optional<uchar> byte = peek();
        if (!byte)
            return std::nullopt;
        if (*byte == '<')
            return peek(1) == uchar('<') ? parseDictionary() : parseHex();
        if (*byte == '[')
            return parseArray();
        if (*byte == '(')
            return parseString();
        if (*byte == '/') {
            ++index;
            return Engine{readToken()};
        }
        if (*byte == '-' || *byte == '+' || *byte == '.' || isDigit(*byte)) {
            const std::optional<double> parsed = parseNumber();
            return parsed ? std::optional(Engine{*parsed}) : std::nullopt;
        }
        if (takeWord("true"))
            return Engine{true};
        if (takeWord("false"))
            return Engine{false};
        if (takeWord("null"))
            return Engine{QString()};
        return std::nullopt;
    }

    std::optional<Engine> parseDictionary()
    {
        take("<<");
        Dictionary items;
        while (true) {
            skipWhitespace();
            if (!peek() || *peek() == '>')
                break;
            if (*peek() != '/')
                return std::nullopt;
            ++index;
            const QString key = readToken();
            std::optional<Engine> item = value();
            if (!item)
                return std::nullopt;
            items.insert_or_assign(key, std::move(*item));
        }
        if (!take(">>"))
            return std::nullopt;
        return Engine{std::move(items)};
    }

    std::optional<Engine> parseArray()
    {
        take("[");
        Array items;
        while (true) {
            skipWhitespace();
            if (!peek() || *peek() == ']')
                break;
            std::optional<Engine> item = value();
            if (!item)
                return std::nullopt;
            items.push_back(std::move(*item));
        }
        if (!take("]"))
            return std::nullopt;
        return Engine{std::move(items)};
    }

    void digits()
    {
        while (peek() && isDigit(*peek()))
            ++index;
    }

    std::optional<double> parseNumber()
    {
        const qsizetype start = index;
        if (peek() == uchar('+') || peek() == uchar('-'))
            ++index;
        digits();
        if (peek() == uchar('.')) {
            ++index;
            digits();
        }
        if (peek() == uchar('e') || peek() == uchar('E')) {
            ++index;
            if (peek() == uchar('+') || peek() == uchar('-'))
                ++index;
            digits();
        }
        return toDouble(bytes.mid(start, index - start));
    }

    std::optional<Engine> parseString()
    {
        take("(");
        QByteArray raw;
        while (peek()) {
            const uchar byte = *peek();
            ++index;
            if (byte == ')')
                break;
            if (byte != '\\') {
                raw.append(char(byte));
                continue;
            }
            const std::optional<uchar> escaped = peek();
            if (!escaped)
                return std::nullopt;
            ++index;
            if (*escaped == 'n') {
                raw.append('\n');
            } else if (*escaped == 'r') {
                raw.append('\r');
            } else if (*escaped == 't') {
                raw.append('\t');
            } else if (*escaped >= '0' && *escaped <= '7') {
                int octal = *escaped - '0';
                for (int count = 0; count < 2 && peek() && *peek() >= '0' && *peek() <= '7'; ++count)
                    octal = octal * 8 + (bytes[index++] - '0');
                raw.append(char(octal & 0xFF));
            } else if (*escaped != '\n' && *escaped != '\r') {
                raw.append(char(*escaped));
            }
        }
        return Engine{decoded(raw)};
    }

    std::optional<Engine> parseHex()
    {
        take("<");
        std::vector<uchar> nibbles;
        while (peek() && *peek() != '>') {
            if (const std::optional<uchar> nibble = hex(*peek()))
                nibbles.push_back(*nibble);
            ++index;
        }
        if (!take(">"))
            return std::nullopt;
        QByteArray raw;
        for (size_t at = 0; at + 1 < nibbles.size(); at += 2)
            raw.append(char(nibbles[at] << 4 | nibbles[at + 1]));
        return Engine{decoded(raw)};
    }
};

std::optional<Engine> dictionary(const QByteArray &data, qsizetype start)
{
    Cursor cursor{data, start};
    std::optional<Engine> value = cursor.value();
    if (!value || !std::holds_alternative<Dictionary>(value->value))
        return std::nullopt;
    return value;
}
}

std::optional<Engine> parse(const QByteArray &data)
{
    if (std::optional<Engine> found = dictionary(data, 0))
        return found;
    const qsizetype start = data.indexOf("<<");
    if (start <= 0)
        return std::nullopt;
    return dictionary(data, start);
}

const Engine *walk(const Engine *value, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const Dictionary *items = value ? std::get_if<Dictionary>(&value->value) : nullptr;
        if (!items || !items->contains(QString::fromLatin1(key)))
            return nullptr;
        value = &items->at(QString::fromLatin1(key));
    }
    return value;
}

std::optional<double> number(const Engine *value)
{
    const double *found = value ? std::get_if<double>(&value->value) : nullptr;
    return found ? std::optional(*found) : std::nullopt;
}

std::optional<bool> boolean(const Engine *value)
{
    const bool *found = value ? std::get_if<bool>(&value->value) : nullptr;
    return found ? std::optional(*found) : std::nullopt;
}

std::optional<QString> string(const Engine *value)
{
    const QString *found = value ? std::get_if<QString>(&value->value) : nullptr;
    return found ? std::optional(*found) : std::nullopt;
}

Array array(const Engine *value)
{
    const Array *found = value ? std::get_if<Array>(&value->value) : nullptr;
    return found ? *found : Array();
}
}
