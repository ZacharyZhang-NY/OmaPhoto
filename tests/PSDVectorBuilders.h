#pragma once
#include "PSDFixture.h"
#include <bit>

// Photoshop's vector extras built byte by byte, as Swift's.
namespace PSDVectorBuilders {
inline void appendDouble(QByteArray &data, double value)
{
    PSDFixture::Buffer bits;
    const quint64 raw = std::bit_cast<quint64>(value);
    bits.u32(quint32(raw >> 32));
    bits.u32(quint32(raw));
    data.append(bits.data);
}

inline void appendUnit(QByteArray &data, const char *key, double value)
{
    data.append(key);
    data.append("UntF#Pxl");
    appendDouble(data, value);
}

inline QByteArray originationData(quint32 type, const QRectF &rect, const std::vector<double> &radii = {})
{
    QByteArray data("keyOriginTypelong");
    PSDFixture::Buffer kind;
    kind.u32(type);
    data.append(kind.data);
    data.append("keyOriginShapeBBox");
    appendUnit(data, "Left", rect.left());
    appendUnit(data, "Top ", rect.top());
    appendUnit(data, "Rght", rect.left() + rect.width());
    appendUnit(data, "Btom", rect.top() + rect.height());
    if (radii.size() == 4) {
        data.append("keyOriginRRectRadii");
        const char *keys[] = {"topLeft", "topRight", "bottomRight", "bottomLeft"};
        for (size_t index = 0; index < 4; ++index)
            appendUnit(data, keys[index], radii[index]);
    }
    return data;
}

struct Subpath {
    std::vector<QPointF> corners;
    bool closed = true;
    // The knot count the record declares, else the corners'.
    std::optional<int> count = std::nullopt;
    // Handles this far either side of each anchor.
    QPointF handle = QPointF();
};

// Subpaths of knots, in Photoshop's fractions of the canvas.
inline QByteArray vectorMask(QSizeF canvas, const std::vector<Subpath> &subpaths)
{
    PSDFixture::Buffer data;
    data.u32(3);
    data.u32(0);
    data.i16(6);
    data.bytes(QByteArray(24, '\0'));
    data.i16(8);
    data.bytes(QByteArray(24, '\0'));
    const auto point = [&](QPointF at) {
        data.u32(quint32(qint32(at.y() / canvas.height() * 0x1000000)));
        data.u32(quint32(qint32(at.x() / canvas.width() * 0x1000000)));
    };
    for (const Subpath &subpath : subpaths) {
        data.i16(subpath.closed ? 0 : 3);
        data.i16(qint16(subpath.count.value_or(int(subpath.corners.size()))));
        data.bytes(QByteArray(22, '\0'));
        for (const QPointF corner : subpath.corners) {
            data.i16(1);
            point(corner - subpath.handle);
            point(corner);
            point(corner + subpath.handle);
        }
    }
    return data.data;
}

inline QByteArray vectorMask(QSizeF canvas, const std::vector<QPointF> &corners)
{
    return vectorMask(canvas, {Subpath{corners}});
}

inline QByteArray colorDescriptor(double red, double green, double blue)
{
    QByteArray data("RGBC");
    for (const auto &[key, value] : {std::pair("Rd  ", red), {"Grn ", green}, {"Bl  ", blue}}) {
        data.append(key);
        data.append("doub");
        appendDouble(data, value);
    }
    return data;
}

inline QByteArray strokeStyle(bool fill, bool stroke, double width, double red, double green, double blue)
{
    QByteArray data;
    for (const auto &[key, value] : {std::pair("strokeEnabled", stroke), {"fillEnabled", fill}}) {
        data.append(key);
        data.append("bool");
        data.append(char(value ? 1 : 0));
    }
    data.append("strokeStyleLineWidthUntF#Pxl");
    appendDouble(data, width);
    data.append(colorDescriptor(red, green, blue));
    return data;
}

}
