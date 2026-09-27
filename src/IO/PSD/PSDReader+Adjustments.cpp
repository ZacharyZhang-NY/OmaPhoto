#include "IO/PSD/PSDReader.h"
#include <QtEndian>
#include <algorithm>

namespace {
quint16 u16(const QByteArray &data, qsizetype at)
{
    return qFromBigEndian<quint16>(data.constData() + at);
}

qint16 i16(const QByteArray &data, qsizetype at)
{
    return qFromBigEndian<qint16>(data.constData() + at);
}

std::optional<LayerAdjustment> levels(const QByteArray &data)
{
    if (data.size() < 292)
        return std::nullopt;
    LevelsSettings settings;
    for (int channel = 0; channel < 4; ++channel) {
        const qsizetype base = 2 + channel * 10;
        settings.ranges[size_t(channel)] = LevelRange{double(u16(data, base)), double(u16(data, base + 8)) / 256, double(u16(data, base + 2)),
                                                      double(u16(data, base + 4)), double(u16(data, base + 6))}
                                               .normalized();
    }
    return LayerAdjustment{.kind = AdjustmentKind::levels, .levels = settings};
}

std::optional<LayerAdjustment> curves(const QByteArray &data)
{
    if (data.size() < 5)
        return std::nullopt;
    qsizetype at = data[0] == 0 ? 1 : 0;
    const quint16 version = u16(data, at);
    at += 2;
    if (version != 1 && version != 4)
        return std::nullopt;
    // Five bytes at least: the version and count always fit.
    const int count = u16(data, at);
    at += 2;
    CurvesSettings settings;
    for (int channel = 0; channel < std::min(4, count); ++channel) {
        if (at + 2 > data.size())
            return std::nullopt;
        const int points = u16(data, at);
        at += 2;
        std::vector<CurvePoint> curve;
        for (int point = 0; point < points; ++point) {
            if (at + 4 > data.size())
                return std::nullopt;
            const double output = u16(data, at), input = u16(data, at + 2);
            at += 4;
            curve.push_back(CurvePoint{std::min(255.0, input), std::min(255.0, output)});
        }
        if (curve.size() >= 2) {
            std::stable_sort(curve.begin(), curve.end(), [](const CurvePoint &lhs, const CurvePoint &rhs) { return lhs.x < rhs.x; });
            if (curve.front().x != 0)
                curve.insert(curve.begin(), CurvePoint{0, curve.front().y});
            if (curve.back().x != 255)
                curve.push_back(CurvePoint{255, curve.back().y});
            settings.channels[size_t(channel)] = curve;
        }
    }
    if (!settings.isValid())
        return std::nullopt;
    return LayerAdjustment{.kind = AdjustmentKind::curves, .curves = settings};
}

std::optional<LayerAdjustment> hue(const QByteArray &data)
{
    if (data.size() < 4)
        return std::nullopt;
    HueSaturationSettings settings(0, 0, 0, data[2] != 0);
    qsizetype at = 4;
    for (const ColorRange range : allColorRanges) {
        if (at + 6 > data.size())
            break;
        settings.adjustments[range] = RangeAdjustment{double(i16(data, at)), double(i16(data, at + 2)), double(i16(data, at + 4))};
        at += 6;
    }
    return LayerAdjustment{.kind = AdjustmentKind::hsv, .hsvSettings = settings};
}
}

std::optional<LayerAdjustment> PSDAdjustments::parse(const std::map<QString, QByteArray> &extra)
{
    if (extra.contains(QStringLiteral("levl")))
        return levels(extra.at(QStringLiteral("levl")));
    if (extra.contains(QStringLiteral("curv")))
        return curves(extra.at(QStringLiteral("curv")));
    if (extra.contains(QStringLiteral("hue2")))
        return hue(extra.at(QStringLiteral("hue2")));
    if (extra.contains(QStringLiteral("hue ")))
        return hue(extra.at(QStringLiteral("hue ")));
    return std::nullopt;
}
