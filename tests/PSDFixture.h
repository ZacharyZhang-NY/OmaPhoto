#pragma once
#include "Document/BrushStroke.h"
#include "IO/PSD/PSDTypes.h"
#include <QPainter>

// Swift's PSDFixture: tiny Photoshop files; the app writes none.
namespace PSDFixture {
struct Buffer {
    QByteArray data;
    void u8(uchar value) { data.append(char(value)); }
    void u16(quint16 value)
    {
        u8(uchar(value >> 8));
        u8(uchar(value));
    }
    void i16(qint16 value) { u16(quint16(value)); }
    void u32(quint32 value)
    {
        u16(quint16(value >> 16));
        u16(quint16(value));
    }
    void i32(qint32 value) { u32(quint32(value)); }
    void u64(quint64 value)
    {
        u32(quint32(value >> 32));
        u32(quint32(value));
    }
    // A Large Document's lengths are eight bytes.
    void length(quint64 value, bool large) { large ? u64(value) : u32(quint32(value)); }
    void bytes(const QByteArray &value) { data.append(value); }
    void string(const char *value) { data.append(value); }
};

struct Planes {
    std::vector<uchar> red, green, blue, alpha;
};

struct Channel {
    qint16 id;
    QByteArray payload;
};

struct Prepared {
    PSDRecord record;
    bool isDivider = false;
    std::vector<Channel> channels;
    int top = 0, left = 0, bottom = 0, right = 0;
    int maskTop = 0, maskLeft = 0, maskBottom = 0, maskRight = 0;
};

// Straight colour from premultiplied pixels, rounded as Swift rounds.
inline Planes planes(const QImage &image)
{
    const QImage pixels = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    Planes result;
    for (int y = 0; y < pixels.height(); ++y) {
        const uchar *row = pixels.constScanLine(y);
        for (int x = 0; x < pixels.width(); ++x) {
            const int r = row[x * 4], g = row[x * 4 + 1], b = row[x * 4 + 2], a = row[x * 4 + 3];
            result.alpha.push_back(uchar(a));
            const auto straight = [a](int value) { return a == 0 ? uchar(0) : uchar(std::min(255, (value * 255 + a / 2) / a)); };
            result.red.push_back(straight(r));
            result.green.push_back(straight(g));
            result.blue.push_back(straight(b));
        }
    }
    return result;
}

inline std::vector<uchar> grayPlane(const QImage &image)
{
    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    std::vector<uchar> plane;
    for (int y = 0; y < gray.height(); ++y)
        plane.insert(plane.end(), gray.constScanLine(y), gray.constScanLine(y) + gray.width());
    return plane;
}

inline QByteArray packBits(const std::vector<uchar> &row)
{
    QByteArray output;
    size_t i = 0;
    while (i < row.size()) {
        if (i + 1 < row.size() && row[i] == row[i + 1]) {
            size_t run = 2;
            while (i + run < row.size() && row[i + run] == row[i] && run < 128)
                ++run;
            output.append(char(qint8(1 - int(run))));
            output.append(char(row[i]));
            i += run;
        } else {
            const size_t start = i++;
            while (i < row.size() && i - start < 128) {
                if (i + 1 < row.size() && row[i] == row[i + 1])
                    break;
                ++i;
            }
            output.append(char(i - start - 1));
            output.append(reinterpret_cast<const char *>(row.data() + start), qsizetype(i - start));
        }
    }
    return output;
}

// PackBits by row: the counts, then the rows.
inline QByteArray encode(const std::vector<uchar> &plane, int width, int height, bool large = false)
{
    Buffer counts;
    QByteArray packed;
    for (int row = 0; row < height; ++row) {
        const QByteArray line = packBits(std::vector<uchar>(plane.begin() + row * width, plane.begin() + (row + 1) * width));
        large ? counts.u32(quint32(line.size())) : counts.u16(quint16(line.size()));
        packed.append(line);
    }
    return counts.data + packed;
}

inline QByteArray channelPayload(const std::vector<uchar> &plane, int width, int height, bool large = false)
{
    return QByteArray("\0\1", 2) + encode(plane, width, height, large);
}

inline std::vector<Channel> emptyChannels()
{
    const QByteArray raw("\0\0", 2);
    return {{-1, raw}, {0, raw}, {1, raw}, {2, raw}};
}

inline Prepared layer(const PSDRecord &record, bool large = false)
{
    const int width = record.image ? record.image->width() : 0, height = record.image ? record.image->height() : 0;
    const int left = int(std::round(record.bounds.left())), top = int(std::round(record.bounds.top()));
    Prepared prepared;
    prepared.record = record;
    if (record.image && width > 0 && height > 0) {
        const Planes split = planes(*record.image);
        for (const auto &[id, plane] : {std::pair<qint16, const std::vector<uchar> &>(-1, split.alpha), {0, split.red}, {1, split.green}, {2, split.blue}})
            prepared.channels.push_back({id, channelPayload(plane, width, height, large)});
    } else {
        prepared.channels = emptyChannels();
    }
    if (record.mask)
        prepared.channels.push_back({-2, channelPayload(grayPlane(*record.mask), record.mask->width(), record.mask->height(), large)});
    prepared.top = top;
    prepared.left = left;
    prepared.bottom = top + height;
    prepared.right = left + width;
    prepared.maskTop = top;
    prepared.maskLeft = left;
    prepared.maskBottom = top + (record.mask ? record.mask->height() : 0);
    prepared.maskRight = left + (record.mask ? record.mask->width() : 0);
    return prepared;
}

inline Prepared emptyLayer(const QString &name, const QString &blendKey, int section, std::optional<QUuid> parent, const PSDRecord *group, bool large)
{
    PSDRecord record;
    record.id = group ? group->id : QUuid::createUuid();
    record.parentID = parent;
    record.name = name;
    record.isGroup = section != 3;
    record.isVisible = group ? group->isVisible : true;
    record.opacity = group ? group->opacity : 1;
    record.blendKey = blendKey;
    record.mask = group ? group->mask : std::nullopt;
    record.maskEnabled = group ? group->maskEnabled : true;
    record.kind = PSDLayerKind::group;
    Prepared prepared;
    prepared.record = record;
    prepared.isDivider = section == 3;
    prepared.channels = emptyChannels();
    if (record.mask) {
        prepared.channels.push_back({-2, channelPayload(grayPlane(*record.mask), record.mask->width(), record.mask->height(), large)});
        prepared.maskBottom = record.mask->height();
        prepared.maskRight = record.mask->width();
    }
    return prepared;
}

// Swift's AdditionalLayerInfo: one more block, before the name's.
struct AdditionalLayerInfo {
    const char *key;
    QByteArray payload;
};

inline void additional(Buffer &buffer, const char *key, const QByteArray &payload, bool large = false)
{
    static const QStringList largeKeys{"LMsk", "Lr16", "Lr32", "Layr", "Mt16", "Mt32", "Mtrn", "Alph", "FMsk", "lnk2", "FEid", "FXid", "PxSD"};
    buffer.string("8BIM");
    buffer.string(key);
    buffer.length(quint64(payload.size()), large && largeKeys.contains(QString::fromLatin1(key)));
    buffer.bytes(payload);
    if (payload.size() % 2 == 1)
        buffer.u8(0);
}

inline QByteArray luni(const QString &name)
{
    Buffer data;
    data.u32(quint32(name.size()));
    for (const QChar unit : name)
        data.u16(unit.unicode());
    return data.data;
}

inline QByteArray extraData(const Prepared &item, bool large, const std::vector<AdditionalLayerInfo> &info)
{
    Buffer extra;
    if (item.record.mask && item.maskRight > item.maskLeft && item.maskBottom > item.maskTop) {
        extra.u32(20);
        extra.i32(item.maskTop);
        extra.i32(item.maskLeft);
        extra.i32(item.maskBottom);
        extra.i32(item.maskRight);
        extra.u8(255);
        extra.u8(uchar((item.record.maskLinked ? 0 : 1) | (item.record.maskEnabled ? 0 : 2)));
        extra.u16(0);
    } else {
        extra.u32(0);
    }
    extra.u32(0);
    const QByteArray pascal = item.record.name.toUtf8().left(255);
    extra.u8(uchar(pascal.size()));
    extra.bytes(pascal);
    extra.bytes(QByteArray((4 - ((1 + pascal.size()) % 4)) % 4, '\0'));
    for (const AdditionalLayerInfo &block : info)
        additional(extra, block.key, block.payload, large);
    additional(extra, "luni", luni(item.record.name), large);
    if (item.record.isGroup || item.isDivider) {
        Buffer payload;
        payload.u32(item.isDivider ? 3 : 1);
        payload.string("8BIM");
        payload.bytes((item.isDivider ? QStringLiteral("norm") : item.record.blendKey + QStringLiteral("    ")).left(4).toLatin1());
        additional(extra, "lsct", payload.data, large);
    }
    return extra.data;
}

inline void writeRecord(Buffer &buffer, const Prepared &item, bool large, const std::vector<AdditionalLayerInfo> &info)
{
    buffer.i32(item.top);
    buffer.i32(item.left);
    buffer.i32(item.bottom);
    buffer.i32(item.right);
    buffer.u16(quint16(item.channels.size()));
    for (const Channel &channel : item.channels) {
        buffer.i16(channel.id);
        buffer.length(quint64(channel.payload.size()), large);
    }
    buffer.string("8BIM");
    buffer.bytes((item.record.blendKey + QStringLiteral("    ")).left(4).toLatin1());
    buffer.u8(uchar(std::clamp(int(std::round(item.record.opacity * 255)), 0, 255)));
    buffer.u8(item.record.clipping ? 1 : 0);
    buffer.u8(item.record.isVisible ? 0 : 2);
    buffer.u8(0);
    const QByteArray extra = extraData(item, large, info);
    buffer.u32(quint32(extra.size()));
    buffer.bytes(extra);
}

inline void emitLayers(const PSDDocument &document, std::optional<QUuid> parent, std::vector<Prepared> &prepared, bool large)
{
    // File order runs bottom to top: divider, children, folder.
    for (const PSDRecord &record : document.layers) {
        if (record.parentID != parent)
            continue;
        if (record.isGroup) {
            prepared.push_back(emptyLayer(QStringLiteral("</Layer group>"), QStringLiteral("norm"), 3, parent, nullptr, large));
            emitLayers(document, record.id, prepared, large);
            prepared.push_back(emptyLayer(record.name, record.blendKey, 1, record.parentID, &record, large));
        } else {
            prepared.push_back(layer(record, large));
        }
    }
}

inline QByteArray layerSection(const PSDDocument &document, bool large, const std::vector<AdditionalLayerInfo> &info)
{
    std::vector<Prepared> prepared;
    emitLayers(document, std::nullopt, prepared, large);
    Buffer records, payloads;
    records.i16(qint16(prepared.size()));
    for (const Prepared &item : prepared) {
        writeRecord(records, item, large, info);
        for (const Channel &channel : item.channels)
            payloads.bytes(channel.payload);
    }
    Buffer section;
    section.length(0, large);
    section.bytes(records.data);
    section.bytes(payloads.data);
    if (section.data.size() % 2 == 1)
        section.u8(0);
    const int field = large ? 8 : 4;
    Buffer length;
    length.length(quint64(section.data.size() - field), large);
    section.data.replace(0, field, length.data);
    section.u32(0);
    return section.data;
}

inline QByteArray resolutionResource(double resolution)
{
    Buffer resource;
    resource.string("8BIM");
    resource.u16(1005);
    resource.u8(0);
    resource.u8(0);
    resource.u32(16);
    const quint32 fixed = quint32(std::round(std::clamp(resolution, 1.0, 9600.0) * 65536));
    for (int axis = 0; axis < 2; ++axis) {
        resource.u32(fixed);
        resource.u16(1);
        resource.u16(1);
    }
    return resource.data;
}

inline QByteArray data(const PSDDocument &document, const QImage &composite, bool large = false, const std::vector<AdditionalLayerInfo> &info = {})
{
    Buffer file;
    file.string("8BPS");
    file.u16(large ? 2 : 1);
    file.bytes(QByteArray(6, '\0'));
    file.u16(4);
    file.u32(quint32(document.height));
    file.u32(quint32(document.width));
    file.u16(8);
    file.u16(3);
    file.u32(0);
    const QByteArray resources = resolutionResource(document.resolution);
    file.u32(quint32(resources.size()));
    file.bytes(resources);
    const QByteArray layers = layerSection(document, large, info);
    file.length(quint64(layers.size()), large);
    file.bytes(layers);
    // The composite, PackBits by channel.
    QImage flattened = BrushRaster::context(document.width, document.height, false);
    QPainter painter(&flattened);
    BrushRaster::draw(composite, QRectF(0, 0, document.width, document.height), painter);
    painter.end();
    const Planes split = planes(flattened);
    file.u16(1);
    QByteArray counts, packed;
    for (const std::vector<uchar> *plane : {&split.red, &split.green, &split.blue, &split.alpha}) {
        const QByteArray encoded = encode(*plane, document.width, document.height, large);
        const int countBytes = document.height * (large ? 4 : 2);
        counts.append(encoded.left(countBytes));
        packed.append(encoded.mid(countBytes));
    }
    file.bytes(counts);
    file.bytes(packed);
    return file.data;
}

inline QImage colorImage(int width, int height, double red, double green, double blue, double alpha = 1)
{
    QImage image = BrushRaster::context(width, height, false);
    image.fill(QColor::fromRgbF(float(red), float(green), float(blue), float(alpha)));
    return image;
}

inline QImage grayImage(int width, int height, int value)
{
    QImage image = BrushRaster::context(width, height, true);
    image.fill(value);
    return image;
}

inline PSDRecord record(const QString &name, std::optional<QImage> image = std::nullopt, QRectF bounds = QRectF())
{
    PSDRecord result;
    result.id = QUuid::createUuid();
    result.name = name;
    result.image = image;
    result.bounds = bounds;
    return result;
}
}
