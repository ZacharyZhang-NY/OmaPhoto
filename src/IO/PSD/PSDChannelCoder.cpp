#include "IO/PSD/PSDChannelCoder.h"
#include "Document/BrushStroke.h"
#include "IO/PSD/PSDTypes.h"
#include <QColorSpace>

namespace {
std::vector<uchar> unpackRLE(int width, int height, const QByteArray &data)
{
    qsizetype offset = 0;
    const auto next = [&]() -> uchar {
        if (offset >= data.size())
            throw PSDError(PSDError::Kind::truncated);
        return uchar(data[offset++]);
    };
    std::vector<qsizetype> counts(static_cast<size_t>(height));
    for (int row = 0; row < height; ++row) {
        const uchar hi = next(), lo = next();
        counts[size_t(row)] = qsizetype(hi) << 8 | lo;
    }
    std::vector<uchar> plane(size_t(width) * size_t(height));
    for (int row = 0; row < height; ++row) {
        const qsizetype end = offset + counts[size_t(row)];
        if (end > data.size())
            throw PSDError(PSDError::Kind::truncated);
        int written = 0;
        uchar *line = plane.data() + size_t(row) * size_t(width);
        while (written < width) {
            if (offset >= end)
                throw PSDError(PSDError::Kind::truncated);
            const int n = qint8(data[offset++]);
            if (n >= 0) {
                const int count = n + 1;
                if (written + count > width || offset + count > end)
                    throw PSDError(PSDError::Kind::truncated);
                std::copy_n(data.constData() + offset, count, line + written);
                offset += count;
                written += count;
            } else if (n != -128) {
                const int count = 1 - n;
                if (written + count > width || offset >= end)
                    throw PSDError(PSDError::Kind::truncated);
                std::fill_n(line + written, count, uchar(data[offset++]));
                written += count;
            }
        }
        offset = end;
    }
    return plane;
}
}

std::vector<uchar> PSDChannelCoder::decode(int compression, int width, int height, const QByteArray &data)
{
    if (width <= 0 || height <= 0)
        return {};
    const qsizetype expected = qsizetype(width) * height;
    switch (compression) {
    case 0:
        if (data.size() < expected)
            throw PSDError(PSDError::Kind::truncated);
        return std::vector<uchar>(data.constData(), data.constData() + expected);
    case 1:
        return unpackRLE(width, height, data);
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
