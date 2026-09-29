#pragma once
#include <QByteArray>
#include <QImage>
#include <optional>
#include <vector>

// What a channel keeps when cut; wide, as sources are.
struct PSDCrop {
    qint64 x;
    qint64 y;
    qint64 width;
    qint64 height;
};

// Photoshop's layer channels: raw and PackBits (Adobe's 2019 specification).
namespace PSDChannelCoder {
// A Large Document counts PackBits rows in four bytes.
std::vector<uchar> decode(int compression, qint64 width, qint64 height, const QByteArray &data, bool largeDocument = false,
                          const std::optional<PSDCrop> &crop = std::nullopt);
// The merged image's first `wanted` of its `channels` planes.
std::vector<std::vector<uchar>> mergedPlanes(int compression, int width, int height, int channels, int wanted, const QByteArray &data,
                                             bool largeDocument);
// Straight planes into premultiplied RGBA, sRGB.
QImage rgbaImage(int width, int height, const std::vector<uchar> &red, const std::vector<uchar> &green,
                 const std::vector<uchar> &blue, const std::vector<uchar> &alpha);
QImage maskImage(int width, int height, const std::vector<uchar> &gray);
}
