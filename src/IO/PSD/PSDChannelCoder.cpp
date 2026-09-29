#include "IO/PSD/PSDChannelCoder.h"
#include "Document/BrushStroke.h"
#include "IO/PSD/PSDTypes.h"
#include <QColorSpace>
#include <span>

namespace {
struct Reader {
    const QByteArray &data;
    qsizetype offset = 0;
    uchar next()
    {
        if (offset >= data.size())
            throw PSDError(PSDError::Kind::truncated);
        return uchar(data[offset++]);
    }
    // Two bytes a row, or four in a Large Document.
    qsizetype count(bool largeDocument)
    {
        qsizetype value = 0;
        for (int index = 0; index < (largeDocument ? 4 : 2); ++index)
            value = value << 8 | next();
        return value;
    }
};

// The rows' counts, first checked to fit in the data.
std::vector<qsizetype> counts(Reader &reader, qint64 height, bool largeDocument)
{
    if (height > (reader.data.size() - reader.offset) / (largeDocument ? 4 : 2))
        throw PSDError(PSDError::Kind::truncated);
    std::vector<qsizetype> result(static_cast<size_t>(height));
    for (qsizetype &count : result)
        count = reader.count(largeDocument);
    return result;
}

// Only the crop's columns kept: no source-wide row.
std::vector<uchar> unpackRows(Reader &reader, const std::vector<qsizetype> &counts, qint64 width, const PSDCrop &crop)
{
    std::vector<uchar> plane(static_cast<size_t>(crop.width * crop.height));
    for (size_t index = 0; index < counts.size(); ++index) {
        const qint64 y = qint64(index);
        const qsizetype end = reader.offset + counts[index];
        if (end > reader.data.size())
            throw PSDError(PSDError::Kind::truncated);
        if (y < crop.y || y >= crop.y + crop.height) {
            reader.offset = end;
            continue;
        }
        // Checked: a row past the crop would overrun.
        const std::span<uchar> target = std::span(plane).subspan(static_cast<size_t>((y - crop.y) * crop.width), static_cast<size_t>(crop.width));
        // What of a run lies in the crop's columns.
        const auto keep = [&](qint64 from, qint64 count, const auto &put) {
            const qint64 first = std::max(from, crop.x), last = std::min(from + count, crop.x + crop.width);
            for (qint64 x = first; x < last; ++x)
                target[static_cast<size_t>(x - crop.x)] = put(x - from);
        };
        qint64 written = 0;
        while (written < width) {
            if (reader.offset >= end)
                throw PSDError(PSDError::Kind::truncated);
            const int n = qint8(reader.data[reader.offset++]);
            if (n >= 0) {
                const qint64 count = n + 1;
                if (written + count > width || reader.offset + count > end)
                    throw PSDError(PSDError::Kind::truncated);
                const qsizetype at = reader.offset;
                keep(written, count, [&](qint64 i) { return uchar(reader.data[at + i]); });
                reader.offset += count;
                written += count;
            } else if (n != -128) {
                const qint64 count = 1 - n;
                if (written + count > width || reader.offset >= end)
                    throw PSDError(PSDError::Kind::truncated);
                const uchar value = uchar(reader.data[reader.offset++]);
                keep(written, count, [&](qint64) { return value; });
                written += count;
            }
        }
        reader.offset = end;
    }
    return plane;
}

std::vector<uchar> cropRaw(qint64 width, qint64 height, const QByteArray &data, qsizetype start, const PSDCrop &crop)
{
    if (data.size() - start < width * height)
        throw PSDError(PSDError::Kind::truncated);
    std::vector<uchar> plane(static_cast<size_t>(crop.width * crop.height));
    for (qint64 row = 0; row < crop.height; ++row)
        std::copy_n(data.constData() + start + (crop.y + row) * width + crop.x, crop.width, plane.data() + row * crop.width);
    return plane;
}
}

std::vector<uchar> PSDChannelCoder::decode(int compression, qint64 width, qint64 height, const QByteArray &data, bool largeDocument,
                                           const std::optional<PSDCrop> &crop)
{
    if (width <= 0 || height <= 0)
        return {};
    const PSDCrop part = crop.value_or(PSDCrop{0, 0, width, height});
    if (part.x < 0 || part.y < 0 || part.width < 0 || part.height < 0 || part.x + part.width > width || part.y + part.height > height)
        throw PSDError(PSDError::Kind::truncated);
    if (part.width == 0 || part.height == 0)
        return {};
    switch (compression) {
    case 0:
        return cropRaw(width, height, data, 0, part);
    case 1: {
        Reader reader{data};
        return unpackRows(reader, counts(reader, height, largeDocument), width, part);
    }
    default:
        throw PSDError(PSDError::Kind::unsupportedCompression);
    }
}

std::vector<std::vector<uchar>> PSDChannelCoder::mergedPlanes(int compression, int width, int height, int channels, int wanted, const QByteArray &data,
                                                             bool largeDocument)
{
    const PSDCrop whole{0, 0, width, height};
    std::vector<std::vector<uchar>> planes;
    switch (compression) {
    case 0:
        for (int channel = 0; channel < wanted; ++channel)
            planes.push_back(cropRaw(width, height, data, qsizetype(channel) * width * height, whole));
        return planes;
    case 1: {
        // Every channel's counts come first; only the wanted are unpacked.
        Reader reader{data};
        std::vector<std::vector<qsizetype>> table;
        for (int channel = 0; channel < channels; ++channel)
            table.push_back(counts(reader, height, largeDocument));
        for (int channel = 0; channel < wanted; ++channel)
            planes.push_back(unpackRows(reader, table[size_t(channel)], width, whole));
        return planes;
    }
    default:
        throw PSDError(PSDError::Kind::unsupportedCompression);
    }
}

QImage PSDChannelCoder::rgbaImage(int width, int height, const std::vector<uchar> &red, const std::vector<uchar> &green,
                                  const std::vector<uchar> &blue, const std::vector<uchar> &alpha)
{
    QImage image = BrushRaster::context(width, height, false);
    image.setColorSpace(QColorSpace::SRgb);
    for (int y = 0; y < height; ++y) {
        uchar *row = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const size_t i = size_t(y) * size_t(width) + size_t(x);
            const unsigned a = alpha[i];
            row[x * 4] = uchar((red[i] * a + 127) / 255);
            row[x * 4 + 1] = uchar((green[i] * a + 127) / 255);
            row[x * 4 + 2] = uchar((blue[i] * a + 127) / 255);
            row[x * 4 + 3] = uchar(a);
        }
    }
    return image;
}

QImage PSDChannelCoder::maskImage(int width, int height, const std::vector<uchar> &gray)
{
    QImage image = BrushRaster::context(width, height, true);
    for (int y = 0; y < height; ++y)
        std::copy_n(gray.data() + size_t(y) * size_t(width), width, image.scanLine(y));
    return image;
}
