#pragma once
#include <QByteArray>
#include <QImage>
#include <vector>

// Photoshop's layer channels: raw and PackBits (Adobe's 2019 specification).
namespace PSDChannelCoder {
std::vector<uchar> decode(int compression, int width, int height, const QByteArray &data);
// Straight planes into premultiplied RGBA, sRGB.
QImage rgbaImage(int width, int height, const std::vector<uchar> &red, const std::vector<uchar> &green,
                 const std::vector<uchar> &blue, const std::vector<uchar> &alpha);
QImage maskImage(int width, int height, const std::vector<uchar> &gray);
}
