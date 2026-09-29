#pragma once
#include "IO/PSD/PSDChannelCoder.h"
#include "IO/PSD/PSDTypes.h"
#include <QByteArray>
#include <QImage>
#include <QString>
#include <map>
#include <optional>
#include <vector>

// Swift's PSDReader, split: the layer records and pixels.
namespace PSDRecords {
QString ascii(const QByteArray &bytes);
quint32 u32(const QByteArray &data, qsizetype at);

struct Cursor {
    const QByteArray &data;
    qint64 offset = 0;

    void need(qint64 count) const;
    void skip(qint64 count);
    uchar u8();
    quint16 u16();
    qint16 i16() { return qint16(u16()); }
    quint32 u32();
    qint32 i32() { return qint32(u32()); }
    quint64 u64();
    // A Large Document's lengths take eight bytes.
    qint64 length(bool isPSB);
    QByteArray bytes(qint64 count);
    QString string(qint64 count) { return ascii(bytes(count)); }
};

struct Channel {
    int id;
    qint64 length;
};

struct RawLayer {
    QString name;
    qint64 top = 0, left = 0, bottom = 0, right = 0;
    // The bounds the channels were written at, before any crop.
    qint64 sourceTop = 0, sourceLeft = 0, sourceBottom = 0, sourceRight = 0;
    uchar opacity = 255;
    uchar fill = 255;
    bool clipping = false;
    bool hidden = false;
    QString blendKey = QStringLiteral("norm");
    std::vector<Channel> channels;
    std::map<QString, QByteArray> extra;
    qint64 maskTop = 0, maskLeft = 0, maskBottom = 0, maskRight = 0;
    qint64 sourceMaskTop = 0, sourceMaskLeft = 0, sourceMaskBottom = 0, sourceMaskRight = 0;
    bool maskDisabled = false;
    bool maskLinked = true;
    bool maskFromRender = false;
    bool hasMask = false;
    std::optional<qint64> section;
    std::optional<QImage> image;
    std::optional<QImage> maskImage;
    std::optional<PSDCrop> imageCrop;
    std::optional<PSDCrop> maskCrop;
    bool cropped = false;
    bool has(std::initializer_list<const char *> keys) const
    {
        return std::any_of(keys.begin(), keys.end(), [this](const char *key) { return extra.contains(QString::fromLatin1(key)); });
    }
};

RawLayer readRecord(Cursor &cursor, bool isPSB);
// Every layer within the side limit and the budget.
bool fitsBudget(const std::vector<RawLayer> &layers, qint64 remainingPixels);
// Cuts a layer and mask to the canvas, marking it.
void cropToCanvas(RawLayer &layer, qint64 width, qint64 height);
void decodeChannels(Cursor &cursor, RawLayer &layer, qint64 remainingPixels, bool isPSB);
}
