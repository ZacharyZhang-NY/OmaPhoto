#pragma once
#include <QByteArray>
#include <QSize>
#include <QFile>
#include <QString>
#include <QtEndian>
#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <stdexcept>
#include <vector>

// Tiny DNG files: an RGGB mosaic, a linear sRGB camera.
namespace DNGFixture {
struct Options {
    int width = 64;
    int height = 48;
    // A photosite's value: 0 red, 1 green, 2 blue.
    std::function<quint16(int channel, int x, int y)> value = [](int, int, int) { return quint16(20000); };
    std::array<double, 3> asShotNeutral{1, 1, 1};
    quint16 orientation = 1;
    // A header's size past the pixels written, for budgets.
    std::optional<QSize> claimed;
    // Row-major: the camera's channels from sRGB's.
    std::array<double, 9> mix{1, 0, 0, 0, 1, 0, 0, 0, 1};
    // A fourth plane, the second green, as CMYG sensors have.
    bool fourColours = false;
};

// XYZ (D65) to linear sRGB: the camera sees sRGB.
inline constexpr std::array<double, 9> colorMatrix{3.2404542, -1.5371385, -0.4985314, -0.9692660, 1.8760108, 0.0415560, 0.0556434, -0.2040259, 1.0572252};

struct Entry {
    quint16 tag;
    quint16 type;
    quint32 count;
    QByteArray data;
};

inline QByteArray le16(quint16 value)
{
    QByteArray bytes(2, '\0');
    qToLittleEndian(value, bytes.data());
    return bytes;
}

inline QByteArray le32(quint32 value)
{
    QByteArray bytes(4, '\0');
    qToLittleEndian(value, bytes.data());
    return bytes;
}

// Ten-thousandths; a signed numerator keeps its bits.
inline QByteArray rational(double value)
{
    return le32(quint32(qint32(std::lround(value * 10000)))) + le32(10000);
}

inline QByteArray data(const Options &options)
{
    QByteArray pixels;
    for (int y = 0; y < options.height; ++y)
        for (int x = 0; x < options.width; ++x) {
            // RGGB: red on even rows and columns.
            const int channel = y % 2 == 0 ? (x % 2 == 0 ? 0 : 1) : (x % 2 == 0 ? 1 : 2);
            pixels += le16(options.value(channel, x, y));
        }
    QByteArray matrix, neutral;
    const int planes = options.fourColours ? 4 : 3;
    for (int row = 0; row < planes; ++row)
        for (size_t column = 0; column < 3; ++column) {
            const size_t from = size_t(row == 3 ? 1 : row) * 3;
            matrix += rational(options.mix[from] * colorMatrix[column] + options.mix[from + 1] * colorMatrix[3 + column] + options.mix[from + 2] * colorMatrix[6 + column]);
        }
    for (int plane = 0; plane < planes; ++plane)
        neutral += rational(options.asShotNeutral[size_t(plane == 3 ? 1 : plane)]);
    const QByteArray model("OmaPhoto Fixture", 17);
    std::vector<Entry> entries{
        {254, 4, 1, le32(0)},
        {256, 4, 1, le32(quint32(options.claimed.value_or(QSize(options.width, options.height)).width()))},
        {257, 4, 1, le32(quint32(options.claimed.value_or(QSize(options.width, options.height)).height()))},
        {258, 3, 1, le16(16) + le16(0)},
        {259, 3, 1, le16(1) + le16(0)},
        {262, 3, 1, le16(32803) + le16(0)},
        {271, 2, 9, QByteArray("OmaPhoto", 9)},
        {272, 2, 8, QByteArray("Fixture", 8)},
        {273, 4, 1, QByteArray()},
        {274, 3, 1, le16(options.orientation) + le16(0)},
        {277, 3, 1, le16(1) + le16(0)},
        {278, 4, 1, le32(quint32(options.height))},
        {279, 4, 1, le32(quint32(pixels.size()))},
        {284, 3, 1, le16(1) + le16(0)},
        {33421, 3, 2, le16(2) + le16(2)},
        {33422, 1, 4, options.fourColours ? QByteArray("\0\1\3\2", 4) : QByteArray("\0\1\1\2", 4)},
        {50706, 1, 4, QByteArray("\1\4\0\0", 4)},
        {50707, 1, 4, QByteArray("\1\1\0\0", 4)},
        {50708, 2, quint32(model.size()), model},
        {50710, 1, quint32(planes), QByteArray("\0\1\2\3", planes)},
        {50714, 4, 1, le32(0)},
        {50717, 4, 1, le32(65535)},
        {50721, 10, quint32(planes * 3), matrix},
        {50728, 5, quint32(planes), neutral},
        {50778, 3, 1, le16(21) + le16(0)},
    };
    // Header, then the directory, its outside values, then the pixels.
    const quint32 directory = 8, outside = directory + 2 + quint32(entries.size()) * 12 + 4;
    QByteArray values;
    for (Entry &entry : entries)
        if (entry.data.size() > 4) {
            if (values.size() % 2)
                values += '\0';
            const QByteArray stored = entry.data;
            entry.data = le32(outside + quint32(values.size()));
            values += stored;
        }
    if (values.size() % 2)
        values += '\0';
    const quint32 strip = outside + quint32(values.size());
    QByteArray file = QByteArray("II*\0", 4) + le32(directory) + le16(quint16(entries.size()));
    for (const Entry &entry : entries) {
        const QByteArray inline_ = entry.tag == 273 ? le32(strip) : entry.data.leftJustified(4, '\0');
        file += le16(entry.tag) + le16(entry.type) + le32(entry.count) + inline_;
    }
    file += le32(0) + values + pixels;
    return file;
}

inline QString write(const QString &path, const Options &options = {})
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data(options)) <= 0)
        throw std::runtime_error("could not write a DNG");
    return path;
}
}
