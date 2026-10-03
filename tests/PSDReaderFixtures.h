#pragma once
#include "PSDFixture.h"

// Raw Photoshop records built byte by byte for edges.
namespace PSDReaderFixtures {
struct Extra {
    const char *signature;
    const char *key;
    QByteArray payload;
    // An 8B64 block's upper length word.
    quint32 high = 0;
};

struct Record {
    QRect bounds;
    std::vector<PSDFixture::Channel> channels;
    const char *signature = "8BIM";
    const char *blendKey = "norm";
    uchar opacity = 255;
    bool clipping = false;
    uchar flags = 0;
    std::optional<QRect> mask;
    uchar maskFlags = 0;
    uchar maskDefault = 255;
    QByteArray name = "Layer";
    std::vector<Extra> extras;
};

inline QByteArray solidChannels(const QRect &bounds, std::vector<PSDFixture::Channel> &channels)
{
    const QByteArray plane(bounds.width() * bounds.height(), '\x80');
    for (const qint16 id : {-1, 0, 1, 2})
        channels.push_back({id, QByteArray("\0\0", 2) + plane});
    return plane;
}

inline QByteArray header(quint32 width, quint32 height)
{
    PSDFixture::Buffer data;
    data.string("8BPS");
    data.u16(1);
    data.bytes(QByteArray(6, '\0'));
    data.u16(3);
    data.u32(height);
    data.u32(width);
    data.u16(8);
    data.u16(3);
    data.u32(0);
    return data.data;
}

inline QByteArray file(const std::vector<Record> &records, const QByteArray &resources = QByteArray(), quint32 width = 8, quint32 height = 8,
                       std::optional<qint16> count = std::nullopt)
{
    PSDFixture::Buffer data;
    data.bytes(header(width, height));
    data.u32(quint32(resources.size()));
    data.bytes(resources);
    PSDFixture::Buffer body, payloads;
    body.i16(count.value_or(qint16(records.size())));
    for (const Record &record : records) {
        body.i32(record.bounds.top());
        body.i32(record.bounds.left());
        body.i32(record.bounds.top() + record.bounds.height());
        body.i32(record.bounds.left() + record.bounds.width());
        body.u16(quint16(record.channels.size()));
        for (const PSDFixture::Channel &channel : record.channels) {
            body.i16(channel.id);
            body.u32(quint32(channel.payload.size()));
            payloads.bytes(channel.payload);
        }
        body.string(record.signature);
        body.string(record.blendKey);
        body.u8(record.opacity);
        body.u8(record.clipping ? 1 : 0);
        body.u8(record.flags);
        body.u8(0);
        PSDFixture::Buffer extra;
        if (record.mask) {
            extra.u32(20);
            extra.i32(record.mask.value().top());
            extra.i32(record.mask.value().left());
            extra.i32(record.mask.value().top() + record.mask.value().height());
            extra.i32(record.mask.value().left() + record.mask.value().width());
            extra.u8(record.maskDefault);
            extra.u8(record.maskFlags);
            extra.u16(0);
        } else {
            extra.u32(0);
        }
        extra.u32(0);
        extra.u8(uchar(record.name.size()));
        extra.bytes(record.name);
        extra.bytes(QByteArray((4 - ((record.name.size() + 1) % 4)) % 4, '\0'));
        for (const Extra &block : record.extras) {
            extra.string(block.signature);
            extra.string(block.key);
            if (QByteArray(block.signature) == "8B64")
                extra.u32(block.high);
            extra.u32(quint32(block.payload.size()));
            extra.bytes(block.payload);
            if (block.payload.size() % 2 == 1)
                extra.u8(0);
        }
        body.u32(quint32(extra.data.size()));
        body.bytes(extra.data);
    }
    PSDFixture::Buffer info;
    info.u32(quint32(body.data.size() + payloads.data.size()));
    info.bytes(body.data);
    info.bytes(payloads.data);
    info.u32(0);
    data.u32(quint32(info.data.size()));
    data.bytes(info.data);
    return data.data;
}

inline QByteArray section(quint32 type, const char *blend = "norm")
{
    PSDFixture::Buffer payload;
    payload.u32(type);
    payload.string("8BIM");
    payload.string(blend);
    return payload.data;
}

inline QByteArray unicode(const QString &name)
{
    return PSDFixture::luni(name);
}
}
