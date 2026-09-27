#include "IO/PSD/PSDReader.h"
#include "IO/PSD/PSDChannelCoder.h"
#include "IO/PSD/PSDVector.h"
#include "Logging.h"
#include <QFile>

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

// Swift's `.ascii` decoding: any byte past 127 reads as nothing.
QString ascii(const QByteArray &bytes)
{
    for (const char byte : bytes)
        if (uchar(byte) > 127)
            return QString();
    return QString::fromLatin1(bytes);
}

struct Cursor {
    const QByteArray &data;
    qint64 offset = 0;

    void need(qint64 count) const
    {
        if (offset < 0 || offset > data.size() || count > data.size() - offset)
            throw PSDError(PSDError::Kind::truncated);
    }
    void skip(qint64 count)
    {
        if (count < 0)
            throw PSDError(PSDError::Kind::truncated);
        need(count);
        offset += count;
    }
    uchar u8()
    {
        need(1);
        return uchar(data[offset++]);
    }
    quint16 u16()
    {
        const quint16 high = u8();
        return quint16(high << 8 | u8());
    }
    qint16 i16() { return qint16(u16()); }
    quint32 u32()
    {
        const quint32 high = u16();
        return high << 16 | u16();
    }
    qint32 i32() { return qint32(u32()); }
    QByteArray bytes(qint64 count)
    {
        need(count);
        offset += count;
        return data.mid(offset - count, count);
    }
    QString string(qint64 count) { return ascii(bytes(count)); }
};

quint32 u32(const QByteArray &data, qsizetype at)
{
    return quint32(uchar(data[at])) << 24 | quint32(uchar(data[at + 1])) << 16 | quint32(uchar(data[at + 2])) << 8 | uchar(data[at + 3]);
}

struct Channel {
    int id;
    qint64 length;
};

struct RawLayer {
    QString name;
    qint64 top = 0, left = 0, bottom = 0, right = 0;
    uchar opacity = 255;
    uchar fill = 255;
    bool clipping = false;
    bool hidden = false;
    QString blendKey = QStringLiteral("norm");
    std::vector<Channel> channels;
    std::map<QString, QByteArray> extra;
    qint64 maskTop = 0, maskLeft = 0, maskBottom = 0, maskRight = 0;
    bool maskDisabled = false;
    bool maskLinked = true;
    bool maskFromRender = false;
    bool hasMask = false;
    std::optional<qint64> section;
    std::optional<QImage> image;
    std::optional<QImage> maskImage;
    bool has(std::initializer_list<const char *> keys) const
    {
        return std::any_of(keys.begin(), keys.end(), [this](const char *key) { return extra.contains(QString::fromLatin1(key)); });
    }
};

std::optional<QString> unicodeName(const QByteArray &data)
{
    if (data.size() < 4)
        return std::nullopt;
    const qint64 count = u32(data, 0);
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

RawLayer readRecord(Cursor &cursor)
{
    RawLayer layer;
    layer.top = cursor.i32();
    layer.left = cursor.i32();
    layer.bottom = cursor.i32();
    layer.right = cursor.i32();
    const int channelCount = cursor.u16();
    if (channelCount > 56)
        throw ImageImportError(ImageImportError::Kind::tooLarge);
    for (int index = 0; index < channelCount; ++index) {
        const int id = cursor.i16();
        layer.channels.push_back({id, cursor.u32()});
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
        if (signature == QLatin1String("8B64")) {
            if (cursor.offset + 8 > extraEnd)
                break;
            const quint64 raw = quint64(cursor.u32()) << 32 | cursor.u32();
            if (raw > quint64(std::numeric_limits<qint64>::max()))
                throw ImageImportError(ImageImportError::Kind::tooLarge);
            length = qint64(raw);
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
}

namespace {
// Transparency, red, green, blue and the user mask only.
bool unpacked(int id)
{
    return id == -1 || id == 0 || id == 1 || id == 2 || id == -2;
}

void decodeChannels(Cursor &cursor, RawLayer &layer, qint64 remainingPixels)
{
    std::map<int, std::vector<uchar>> planes;
    const qint64 width = std::max<qint64>(0, layer.right - layer.left), height = std::max<qint64>(0, layer.bottom - layer.top);
    const qint64 maskWidth = std::max<qint64>(0, layer.maskRight - layer.maskLeft), maskHeight = std::max<qint64>(0, layer.maskBottom - layer.maskTop);
    const qint64 budget = std::max<qint64>(0, remainingPixels);
    if (width > 0 && height > 0 && (width > 30'000 || height > 30'000 || width * height > budget))
        throw ImageImportError(ImageImportError::Kind::tooLarge);
    if (layer.hasMask && maskWidth > 0 && maskHeight > 0 && (maskWidth > 30'000 || maskHeight > 30'000 || maskWidth * maskHeight > budget))
        throw ImageImportError(ImageImportError::Kind::tooLarge);
    for (const Channel &channel : layer.channels) {
        const qint64 start = cursor.offset;
        if (unpacked(channel.id) && channel.length >= 2) {
            const int compression = cursor.u16();
            const QByteArray payload = cursor.bytes(channel.length - 2);
            const bool isMask = channel.id == -2;
            const qint64 w = isMask ? maskWidth : width, h = isMask ? maskHeight : height;
            if (w > 0 && h > 0)
                planes[channel.id] = PSDChannelCoder::decode(compression, int(w), int(h), payload);
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

PSDLayerKind kindOf(const RawLayer &layer, bool isGroup)
{
    if (isGroup)
        return PSDLayerKind::group;
    if (layer.has({"TySh", "tySh", "txt2"}))
        return PSDLayerKind::text;
    if (layer.has({"vmsk", "vsms", "vogk"}))
        return PSDLayerKind::vector;
    if (layer.has({"SoLd", "SoLE"}))
        return PSDLayerKind::smartObject;
    if (layer.has({"lfx2", "lrFX", "lmfx"}))
        return PSDLayerKind::effects;
    for (const QString &key : PSDReader::adjustmentKeys)
        if (layer.extra.contains(key))
            return PSDLayerKind::adjustment;
    return PSDLayerKind::raster;
}

std::vector<PSDRecord> assemble(const std::vector<RawLayer> &raw, QSizeF canvas, qint64 remainingPixels)
{
    std::vector<PSDRecord> result;
    std::vector<QUuid> groups;
    qint64 remaining = std::max<qint64>(0, remainingPixels);
    for (const RawLayer &layer : raw) {
        // Groups run bottom to top: divider, children, then the folder.
        if (layer.section == 3) {
            groups.push_back(QUuid::createUuid());
            continue;
        }
        const bool isGroup = layer.section == 1 || layer.section == 2;
        QUuid id = QUuid::createUuid();
        if (isGroup && !groups.empty()) {
            id = groups.back();
            groups.pop_back();
        }
        PSDRecord record;
        record.id = id;
        record.name = layer.name.isEmpty() ? QStringLiteral("Layer") : layer.name;
        if (!groups.empty())
            record.parentID = groups.back();
        record.isGroup = isGroup;
        record.isVisible = !layer.hidden;
        const bool passes = layer.blendKey == QLatin1String("pass") || layer.blendKey == QLatin1String("norm");
        record.blendKey = isGroup && passes ? QStringLiteral("pass") : layer.blendKey;
        record.clipping = layer.clipping;
        record.kind = kindOf(layer, isGroup);
        const bool hasEffects = record.kind == PSDLayerKind::effects || layer.has({"lfx2", "lrFX", "lmfx"});
        record.opacity = hasEffects && layer.fill != 255 ? layer.opacity / 255.0 : (layer.opacity / 255.0) * (layer.fill / 255.0);
        record.bounds = isGroup ? QRectF(QPointF(0, 0), canvas) : QRectF(layer.left, layer.top, std::max<qint64>(0, layer.right - layer.left), std::max<qint64>(0, layer.bottom - layer.top));
        record.image = isGroup ? std::nullopt : layer.image;
        std::optional<PSDVector::Live> live = isGroup ? std::nullopt : PSDVector::live(layer.extra, canvas, remaining);
        if (live) {
            record.image = live->image;
            record.bounds = live->bounds;
            record.shape = live->style;
            record.shapeNotes = live->notes;
            record.kind = PSDLayerKind::vector;
            remaining = std::max<qint64>(0, remaining - qint64(live->image.width()) * live->image.height());
        } else if (!record.image && !isGroup) {
            if (const std::optional<PSDVector::Raster> raster = PSDVector::raster(layer.extra, canvas, remaining)) {
                record.image = raster->image;
                record.bounds = raster->bounds;
                record.kind = PSDLayerKind::vector;
                remaining = std::max<qint64>(0, remaining - qint64(raster->image.width()) * raster->image.height());
            }
        }
        record.mask = layer.maskFromRender ? std::nullopt : layer.maskImage;
        record.maskEnabled = !layer.maskDisabled;
        record.maskLinked = layer.maskLinked;
        if (!isGroup)
            record.adjustment = PSDAdjustments::parse(layer.extra);
        if (record.adjustment)
            record.kind = PSDLayerKind::adjustment;
        result.push_back(std::move(record));
    }
    if (!groups.empty())
        throw PSDError(PSDError::Kind::truncated);
    return result;
}

PSDDocument parse(const QByteArray &data, qint64 remainingPixels)
{
    Cursor cursor{data};
    if (cursor.string(4) != QLatin1String("8BPS"))
        throw ImageImportError(ImageImportError::Kind::unreadable);
    if (cursor.u16() != 1)
        throw PSDError(PSDError::Kind::unsupportedVersion);
    cursor.skip(6);
    cursor.u16();
    const qint64 canvasHeight = cursor.u32(), canvasWidth = cursor.u32();
    const int depth = cursor.u16(), mode = cursor.u16();
    if (canvasWidth < 1 || canvasWidth > 30'000 || canvasHeight < 1 || canvasHeight > 30'000 || canvasWidth * canvasHeight > 100'000'000)
        throw ImageImportError(ImageImportError::Kind::tooLarge);
    if (depth != 8)
        throw PSDError(PSDError::Kind::unsupportedDepth);
    if (mode != 3)
        throw PSDError(PSDError::Kind::unsupportedColorMode);
    cursor.skip(cursor.u32());
    const qint64 resourcesLength = cursor.u32();
    const qint64 resourcesEnd = cursor.offset + resourcesLength;
    double resolution = 72;
    while (cursor.offset + 12 <= resourcesEnd) {
        if (cursor.string(4) != QLatin1String("8BIM"))
            break;
        const int id = cursor.u16();
        const int nameLength = cursor.u8();
        cursor.skip(nameLength);
        if ((nameLength + 1) % 2 == 1)
            cursor.skip(1);
        const qint64 length = cursor.u32();
        const qint64 dataStart = cursor.offset;
        if (id == 1005 && length >= 4) {
            resolution = cursor.u32() / 65536.0;
            // A fixed-point number is finite; below one reads as 72.
            resolution = std::min(9600.0, resolution < 1 ? 72 : resolution);
        }
        cursor.offset = dataStart + length;
        if (length % 2 == 1)
            cursor.skip(1);
    }
    cursor.offset = resourcesEnd;
    const qint64 layerSection = cursor.u32();
    const PSDDocument empty{int(canvasWidth), int(canvasHeight), resolution, {}};
    if (layerSection < 4)
        return empty;
    // The layer info length is skipped, as in Swift.
    cursor.u32();
    const qint64 count = std::abs(qint64(cursor.i16()));
    if (count > 10'000)
        throw ImageImportError(ImageImportError::Kind::tooLarge);
    std::vector<RawLayer> raw;
    raw.reserve(size_t(count));
    for (qint64 index = 0; index < count; ++index)
        raw.push_back(readRecord(cursor));
    qint64 usedPixels = 0;
    for (RawLayer &layer : raw) {
        decodeChannels(cursor, layer, remainingPixels - usedPixels);
        if (layer.image)
            usedPixels += qint64(layer.image->width()) * layer.image->height();
    }
    return PSDDocument{int(canvasWidth), int(canvasHeight), resolution, assemble(raw, QSizeF(canvasWidth, canvasHeight), remainingPixels - usedPixels)};
}
}

const std::set<QString> PSDReader::adjustmentKeys{
    QStringLiteral("levl"), QStringLiteral("curv"), QStringLiteral("hue2"), QStringLiteral("hue "), QStringLiteral("expA"), QStringLiteral("grdm"),
    QStringLiteral("brit"), QStringLiteral("blnc"), QStringLiteral("nvrt"), QStringLiteral("thrs"), QStringLiteral("post"), QStringLiteral("mixr"),
    QStringLiteral("selc"), QStringLiteral("blwh"), QStringLiteral("phfl"), QStringLiteral("vibA"),
};

bool PSDReader::matches(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && file.read(4) == "8BPS";
}

bool PSDReader::matches(const QByteArray &data)
{
    return data.startsWith("8BPS");
}

PSDDocument PSDReader::read(const QString &path, qint64 remainingPixels)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcIO).noquote() << "cannot open the Photoshop file" << path << file.errorString();
        throw ImageImportError(ImageImportError::Kind::unreadable);
    }
    // Swift's `mappedIfSafe`: mapped where it can be, else read.
    if (uchar *mapped = file.map(0, file.size()))
        return read(QByteArray::fromRawData(reinterpret_cast<const char *>(mapped), file.size()), remainingPixels);
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        qCWarning(lcIO).noquote() << "cannot read the Photoshop file" << path << file.errorString();
        throw ImageImportError(ImageImportError::Kind::unreadable);
    }
    return read(bytes, remainingPixels);
}

PSDDocument PSDReader::read(const QByteArray &data, qint64 remainingPixels)
{
    try {
        return parse(data, remainingPixels);
    } catch (const std::runtime_error &error) {
        qCWarning(lcIO) << "a Photoshop file was refused:" << error.what();
        throw;
    }
}
