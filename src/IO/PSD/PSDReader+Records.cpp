#include "IO/PSD/PSDReader+Records.h"
#include "Document/DocumentLimits.h"
#include "IO/ImageImporter.h"
#include <set>

namespace {
// Bytes 0x80–0xFF in Mac OS Roman, as Python's `mac_roman`.
constexpr char16_t macRoman[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2, 0x00E4, 0x00E3,
    0x00E5, 0x00E7, 0x00E9, 0x00E8, 0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
    0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC, 0x2020, 0x00B0, 0x00A2, 0x00A3,
    0x00A7, 0x2022, 0x00B6, 0x00DF, 0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211, 0x220F, 0x03C0, 0x222B, 0x00AA,
    0x00BA, 0x03A9, 0x00E6, 0x00F8, 0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
    0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153, 0x2013, 0x2014, 0x201C, 0x201D,
    0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1, 0x00CB, 0x00C8, 0x00CD, 0x00CE,
    0x00CF, 0x00CC, 0x00D3, 0x00D4, 0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
    0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7,
};

QString fromMacRoman(const QByteArray &bytes)
{
    QString text;
    text.reserve(bytes.size());
    for (const char byte : bytes)
        text += uchar(byte) < 0x80 ? QChar(uchar(byte)) : QChar(macRoman[uchar(byte) - 0x80]);
    return text;
}

std::optional<QString> unicodeName(const QByteArray &data)
{
    if (data.size() < 4)
        return std::nullopt;
    const qint64 count = PSDRecords::u32(data, 0);
    if (count <= 0 || data.size() < 4 + count * 2)
        return std::nullopt;
    QString name(qsizetype(count), Qt::Uninitialized);
    for (qint64 i = 0; i < count; ++i)
        name[qsizetype(i)] = QChar(quint16(uchar(data[4 + i * 2]) << 8 | uchar(data[5 + i * 2])));
    // Swift trims NUL from both ends.
    qsizetype from = 0, to = name.size();
    while (from < to && name[from] == QChar(0))
        ++from;
    while (to > from && name[to - 1] == QChar(0))
        --to;
    return name.mid(from, to - from);
}

// Keys whose lengths a Large Document writes in eight bytes.
const std::set<QString> psbLargeKeys{
    QStringLiteral("LMsk"), QStringLiteral("Lr16"), QStringLiteral("Lr32"), QStringLiteral("Layr"), QStringLiteral("Mt16"),
    QStringLiteral("Mt32"),  QStringLiteral("Mtrn"), QStringLiteral("Alph"), QStringLiteral("FMsk"), QStringLiteral("lnk2"),
    QStringLiteral("FEid"),  QStringLiteral("FXid"), QStringLiteral("PxSD"),
};

// Transparency, red, green, blue and the user mask only.
bool unpacked(int id)
{
    return id == -1 || id == 0 || id == 1 || id == 2 || id == -2;
}

// Pixels and mask each within the side and budget.
bool fits(qint64 width, qint64 height, qint64 maskWidth, qint64 maskHeight, bool hasMask, qint64 remainingPixels)
{
    const qint64 budget = std::max<qint64>(0, remainingPixels);
    if (width > 0 && height > 0 && (width > DocumentLimits::maxSide || height > DocumentLimits::maxSide || width * height > budget))
        return false;
    return !(hasMask && maskWidth > 0 && maskHeight > 0
             && (maskWidth > DocumentLimits::maxSide || maskHeight > DocumentLimits::maxSide || maskWidth * maskHeight > budget));
}

// A box's part on the canvas, from its own corner.
PSDCrop crop(qint64 left, qint64 top, qint64 right, qint64 bottom, qint64 canvasWidth, qint64 canvasHeight)
{
    const qint64 croppedLeft = std::min(canvasWidth, std::max<qint64>(0, left)), croppedTop = std::min(canvasHeight, std::max<qint64>(0, top));
    const qint64 croppedRight = std::max(croppedLeft, std::min(canvasWidth, right)), croppedBottom = std::max(croppedTop, std::min(canvasHeight, bottom));
    return PSDCrop{croppedLeft - left, croppedTop - top, croppedRight - croppedLeft, croppedBottom - croppedTop};
}
}

// Swift's `.ascii` decoding: any byte past 127 reads as nothing.
QString PSDRecords::ascii(const QByteArray &bytes)
{
    for (const char byte : bytes)
        if (uchar(byte) > 127)
            return QString();
    return QString::fromLatin1(bytes);
}

quint32 PSDRecords::u32(const QByteArray &data, qsizetype at)
{
    return quint32(uchar(data[at])) << 24 | quint32(uchar(data[at + 1])) << 16 | quint32(uchar(data[at + 2])) << 8 | uchar(data[at + 3]);
}

void PSDRecords::Cursor::need(qint64 count) const
{
    if (offset < 0 || offset > data.size() || count > data.size() - offset)
        throw PSDError(PSDError::Kind::truncated);
}

void PSDRecords::Cursor::skip(qint64 count)
{
    if (count < 0)
        throw PSDError(PSDError::Kind::truncated);
    need(count);
    offset += count;
}

uchar PSDRecords::Cursor::u8()
{
    need(1);
    return uchar(data[offset++]);
}

quint16 PSDRecords::Cursor::u16()
{
    const quint16 high = u8();
    return quint16(high << 8 | u8());
}

quint32 PSDRecords::Cursor::u32()
{
    const quint32 high = u16();
    return high << 16 | u16();
}

quint64 PSDRecords::Cursor::u64()
{
    const quint64 high = u32();
    return high << 32 | u32();
}

qint64 PSDRecords::Cursor::length(bool isPSB)
{
    const quint64 value = isPSB ? u64() : u32();
    if (value > quint64(std::numeric_limits<qint64>::max()))
        throw ImageImportError(ImageImportError::Kind::tooLarge);
    return qint64(value);
}

QByteArray PSDRecords::Cursor::bytes(qint64 count)
{
    need(count);
    offset += count;
    return data.mid(offset - count, count);
}

PSDRecords::RawLayer PSDRecords::readRecord(Cursor &cursor, bool isPSB)
{
    RawLayer layer;
    layer.top = cursor.i32();
    layer.left = cursor.i32();
    layer.bottom = cursor.i32();
    layer.right = cursor.i32();
    layer.sourceTop = layer.top, layer.sourceLeft = layer.left, layer.sourceBottom = layer.bottom, layer.sourceRight = layer.right;
    const int channelCount = cursor.u16();
    if (channelCount > 56)
        throw ImageImportError(ImageImportError::Kind::tooLarge);
    for (int index = 0; index < channelCount; ++index) {
        const int id = cursor.i16();
        layer.channels.push_back({id, cursor.length(isPSB)});
    }
    if (cursor.string(4) != QLatin1String("8BIM"))
        throw PSDError(PSDError::Kind::truncated);
    layer.blendKey = cursor.string(4);
    layer.opacity = cursor.u8();
    layer.clipping = cursor.u8() != 0;
    layer.hidden = (cursor.u8() & 2) != 0;
    cursor.skip(1);
    const qint64 extraLength = cursor.u32();
    const qint64 extraEnd = cursor.offset + extraLength;
    const qint64 maskLength = cursor.u32();
    const qint64 maskEnd = cursor.offset + maskLength;
    if (maskLength >= 20) {
        layer.hasMask = true;
        layer.maskTop = cursor.i32();
        layer.maskLeft = cursor.i32();
        layer.maskBottom = cursor.i32();
        layer.maskRight = cursor.i32();
        layer.sourceMaskTop = layer.maskTop, layer.sourceMaskLeft = layer.maskLeft;
        layer.sourceMaskBottom = layer.maskBottom, layer.sourceMaskRight = layer.maskRight;
        // The mask's default colour is skipped, as Swift ignores it.
        cursor.skip(1);
        const uchar flags = cursor.u8();
        layer.maskDisabled = (flags & 2) != 0;
        layer.maskLinked = (flags & 1) == 0;
        layer.maskFromRender = (flags & 8) != 0;
    }
    cursor.offset = maskEnd;
    cursor.skip(cursor.u32());
    const int nameCount = cursor.u8();
    layer.name = fromMacRoman(cursor.bytes(nameCount));
    cursor.skip((4 - ((nameCount + 1) % 4)) % 4);
    while (cursor.offset + 12 <= extraEnd) {
        const QString signature = cursor.string(4);
        if (signature != QLatin1String("8BIM") && signature != QLatin1String("8B64"))
            break;
        const QString key = cursor.string(4);
        qint64 length;
        if (signature == QLatin1String("8B64") || (isPSB && psbLargeKeys.contains(key))) {
            if (cursor.offset + 8 > extraEnd)
                break;
            length = cursor.length(true);
        } else {
            length = cursor.u32();
        }
        const QByteArray payload = cursor.bytes(length);
        if (length % 2 == 1)
            cursor.skip(1);
        layer.extra[key] = payload;
        if (key == QLatin1String("luni"))
            if (const std::optional<QString> unicode = unicodeName(payload))
                layer.name = *unicode;
        if (key == QLatin1String("iOpa") && !payload.isEmpty())
            layer.fill = uchar(payload[0]);
        if ((key == QLatin1String("lsct") || key == QLatin1String("lsdk")) && payload.size() >= 4)
            layer.section = u32(payload, 0);
    }
    cursor.offset = extraEnd;
    return layer;
}

bool PSDRecords::fitsBudget(const std::vector<RawLayer> &layers, qint64 remainingPixels)
{
    qint64 usedPixels = 0;
    for (const RawLayer &layer : layers) {
        const qint64 width = std::max<qint64>(0, layer.right - layer.left), height = std::max<qint64>(0, layer.bottom - layer.top);
        const qint64 maskWidth = std::max<qint64>(0, layer.maskRight - layer.maskLeft), maskHeight = std::max<qint64>(0, layer.maskBottom - layer.maskTop);
        if (!fits(width, height, maskWidth, maskHeight, layer.hasMask, remainingPixels - usedPixels))
            return false;
        if (width > 0 && height > 0)
            usedPixels += width * height;
    }
    return true;
}

void PSDRecords::cropToCanvas(RawLayer &layer, qint64 width, qint64 height)
{
    const PSDCrop image = crop(layer.left, layer.top, layer.right, layer.bottom, width, height);
    if (image.x != 0 || image.y != 0 || image.width != layer.right - layer.left || image.height != layer.bottom - layer.top) {
        layer.left += image.x;
        layer.top += image.y;
        layer.right = layer.left + image.width;
        layer.bottom = layer.top + image.height;
        layer.imageCrop = image;
        layer.cropped = true;
    }
    if (!layer.hasMask)
        return;
    const PSDCrop mask = crop(layer.maskLeft, layer.maskTop, layer.maskRight, layer.maskBottom, width, height);
    if (mask.x != 0 || mask.y != 0 || mask.width != layer.maskRight - layer.maskLeft || mask.height != layer.maskBottom - layer.maskTop) {
        layer.maskLeft += mask.x;
        layer.maskTop += mask.y;
        layer.maskRight = layer.maskLeft + mask.width;
        layer.maskBottom = layer.maskTop + mask.height;
        layer.maskCrop = mask;
        layer.cropped = true;
    }
}

void PSDRecords::decodeChannels(Cursor &cursor, RawLayer &layer, qint64 remainingPixels, bool isPSB)
{
    std::map<int, std::vector<uchar>> planes;
    const qint64 width = std::max<qint64>(0, layer.right - layer.left), height = std::max<qint64>(0, layer.bottom - layer.top);
    const qint64 maskWidth = std::max<qint64>(0, layer.maskRight - layer.maskLeft), maskHeight = std::max<qint64>(0, layer.maskBottom - layer.maskTop);
    if (!fits(width, height, maskWidth, maskHeight, layer.hasMask, remainingPixels))
        throw ImageImportError(ImageImportError::Kind::tooLarge);
    const qint64 sourceWidth = std::max<qint64>(0, layer.sourceRight - layer.sourceLeft), sourceHeight = std::max<qint64>(0, layer.sourceBottom - layer.sourceTop);
    const qint64 sourceMaskWidth = std::max<qint64>(0, layer.sourceMaskRight - layer.sourceMaskLeft);
    const qint64 sourceMaskHeight = std::max<qint64>(0, layer.sourceMaskBottom - layer.sourceMaskTop);
    for (const Channel &channel : layer.channels) {
        const qint64 start = cursor.offset;
        if (unpacked(channel.id) && channel.length >= 2) {
            const int compression = cursor.u16();
            const QByteArray payload = cursor.bytes(channel.length - 2);
            const bool isMask = channel.id == -2;
            // Written at the source bounds; a cut decodes part.
            const qint64 w = isMask ? maskWidth : width, h = isMask ? maskHeight : height;
            if (w > 0 && h > 0)
                planes[channel.id] = PSDChannelCoder::decode(compression, isMask ? sourceMaskWidth : sourceWidth, isMask ? sourceMaskHeight : sourceHeight,
                                                             payload, isPSB, isMask ? layer.maskCrop : layer.imageCrop);
        }
        cursor.offset = start + std::max<qint64>(0, channel.length);
    }
    // Each plane decodes to exactly its size: no count check.
    if (layer.hasMask && maskWidth > 0 && maskHeight > 0 && planes.contains(-2))
        layer.maskImage = PSDChannelCoder::maskImage(int(maskWidth), int(maskHeight), planes.at(-2));
    if (width <= 0 || height <= 0)
        return;
    const size_t count = size_t(width * height);
    const auto plane = [&](int id, uchar fill) { return planes.contains(id) ? planes.at(id) : std::vector<uchar>(count, fill); };
    layer.image = PSDChannelCoder::rgbaImage(int(width), int(height), plane(0, 0), plane(1, 0), plane(2, 0), plane(-1, 255));
}
