#include "IO/ProjectStore+Json.h"
#include <QJsonArray>
#include <limits>

// LayerAdjustment's synthesized Codable: every key but the optionals.
namespace {
using namespace ManifestJson;

QJsonArray array(const QJsonValue &value)
{
    if (!value.isArray())
        refuse();
    return value.toArray();
}

template <typename Kind, size_t count> Kind kindNamed(const QJsonValue &value, const std::array<Kind, count> &kinds)
{
    const QString text = string(value);
    for (const Kind kind : kinds) {
        if (rawValue(kind) == text)
            return kind;
    }
    refuse();
}

// ColorRange-keyed: key, value in turn; a missing value refuses.
template <typename Value, typename Decode> std::map<ColorRange, Value> keyed(const QJsonValue &value, Decode decode)
{
    const QJsonArray pairs = array(value);
    std::map<ColorRange, Value> result;
    for (qsizetype index = 0; index < pairs.size(); index += 2)
        result.insert_or_assign(kindNamed(pairs.at(index), allColorRanges), decode(pairs.at(index + 1)));
    return result;
}

template <typename Value, typename Encode> QJsonArray keyed(const std::map<ColorRange, Value> &values, Encode encode)
{
    QJsonArray pairs;
    for (const auto &[range, value] : values) {
        pairs.append(rawValue(range));
        pairs.append(encode(value));
    }
    return pairs;
}

RangeAdjustment rangeAdjustment(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    return {number(object.value("hue")), number(object.value("saturation")), number(object.value("lightness"))};
}

HueBand band(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    return {number(object.value("falloffStart")), number(object.value("rangeStart")), number(object.value("rangeEnd")),
            number(object.value("falloffEnd"))};
}

HueSaturationSettings hueSaturation(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    HueSaturationSettings settings;
    settings.range = kindNamed(object.value("range"), allColorRanges);
    settings.colorize = boolean(object.value("colorize"));
    settings.invertRange = boolean(object.value("invertRange"));
    settings.adjustments = keyed<RangeAdjustment>(object.value("adjustments"), rangeAdjustment);
    settings.bands = keyed<HueBand>(object.value("bands"), band);
    return settings;
}

LevelsSettings levels(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    const QJsonArray ranges = array(object.value("ranges"));
    // Swift decodes any count; isValid refuses all but four.
    if (ranges.size() != 4)
        refuse();
    LevelsSettings settings;
    settings.channel = kindNamed(object.value("channel"), allLevelsChannels);
    for (qsizetype index = 0; index < 4; ++index) {
        const QJsonObject range = ranges.at(index).toObject();
        settings.ranges[size_t(index)] = {number(range.value("black")), number(range.value("gamma")), number(range.value("white")),
                                          number(range.value("outputBlack")), number(range.value("outputWhite"))};
    }
    return settings;
}

CurvesSettings curves(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    CurvesSettings settings;
    settings.channel = kindNamed(object.value("channel"), allLevelsChannels);
    settings.channels.clear();
    for (const QJsonValue &channel : array(object.value("channels"))) {
        std::vector<CurvePoint> points;
        for (const QJsonValue &point : array(channel))
            points.push_back({number(point.toObject().value("x")), number(point.toObject().value("y"))});
        settings.channels.push_back(points);
    }
    return settings;
}

AdjustmentColor colour(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    return {number(object.value("red")), number(object.value("green")), number(object.value("blue"))};
}

ExposureSettings exposure(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    return {.exposure = number(object.value("exposure")), .offset = number(object.value("offset")), .gamma = number(object.value("gamma"))};
}

GradientMapSettings gradientMap(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    return {colour(object.value("shadows")), colour(object.value("highlights")), boolean(object.value("reversed"))};
}

GrainSettings grain(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    // Swift's UInt32 refuses what it cannot hold.
    const qint64 seed = integer(object.value("seed"));
    if (seed < 0 || seed > std::numeric_limits<quint32>::max())
        refuse();
    return {.amount = number(object.value("amount")), .size = number(object.value("size")), .roughness = number(object.value("roughness")),
            .seed = quint32(seed)};
}

QJsonObject encoded(const AdjustmentColor &colour)
{
    return {{"red", colour.red}, {"green", colour.green}, {"blue", colour.blue}};
}

QJsonObject encoded(const HueSaturationSettings &settings)
{
    return {{"range", rawValue(settings.range)},
            {"colorize", settings.colorize},
            {"invertRange", settings.invertRange},
            {"adjustments", keyed(settings.adjustments,
                                  [](const RangeAdjustment &values) {
                                      return QJsonObject{{"hue", values.hue}, {"saturation", values.saturation}, {"lightness", values.lightness}};
                                  })},
            {"bands", keyed(settings.bands, [](const HueBand &band) {
                 return QJsonObject{{"falloffStart", band.falloffStart}, {"rangeStart", band.rangeStart}, {"rangeEnd", band.rangeEnd},
                                    {"falloffEnd", band.falloffEnd}};
             })}};
}

QJsonObject encoded(const LevelsSettings &settings)
{
    QJsonArray ranges;
    for (const LevelRange &range : settings.ranges)
        ranges.append(QJsonObject{{"black", range.black}, {"gamma", range.gamma}, {"white", range.white}, {"outputBlack", range.outputBlack},
                                  {"outputWhite", range.outputWhite}});
    return {{"channel", rawValue(settings.channel)}, {"ranges", ranges}};
}

QJsonObject encoded(const CurvesSettings &settings)
{
    QJsonArray channels;
    for (const std::vector<CurvePoint> &points : settings.channels) {
        QJsonArray channel;
        for (const CurvePoint &point : points)
            channel.append(QJsonObject{{"x", point.x}, {"y", point.y}});
        channels.append(channel);
    }
    return {{"channel", rawValue(settings.channel)}, {"channels", channels}};
}
}

LayerAdjustment ManifestJson::adjustment(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    return {.kind = kindNamed(object.value("kind"), allAdjustmentKinds),
            .hue = number(object.value("hue")),
            .saturation = number(object.value("saturation")),
            .lightness = number(object.value("lightness")),
            .colorize = boolean(object.value("colorize")),
            .hsvSettings = optional(object, "hsvSettings", hueSaturation),
            .levels = levels(object.value("levels")),
            .curves = curves(object.value("curves")),
            .exposureSettings = optional(object, "exposureSettings", exposure),
            .gradientMapSettings = optional(object, "gradientMapSettings", gradientMap),
            .grainSettings = optional(object, "grainSettings", grain)};
}

QJsonObject ManifestJson::encoded(const LayerAdjustment &adjustment)
{
    QJsonObject object{{"kind", rawValue(adjustment.kind)},
                       {"hue", adjustment.hue},
                       {"saturation", adjustment.saturation},
                       {"lightness", adjustment.lightness},
                       {"colorize", adjustment.colorize},
                       {"levels", ::encoded(adjustment.levels)},
                       {"curves", ::encoded(adjustment.curves)}};
    if (adjustment.hsvSettings)
        object.insert("hsvSettings", ::encoded(*adjustment.hsvSettings));
    if (adjustment.exposureSettings) {
        const ExposureSettings &exposure = *adjustment.exposureSettings;
        object.insert("exposureSettings", QJsonObject{{"exposure", exposure.exposure}, {"offset", exposure.offset}, {"gamma", exposure.gamma}});
    }
    if (adjustment.gradientMapSettings) {
        const GradientMapSettings &map = *adjustment.gradientMapSettings;
        object.insert("gradientMapSettings", QJsonObject{{"shadows", ::encoded(map.shadows)}, {"highlights", ::encoded(map.highlights)},
                                                         {"reversed", map.reversed}});
    }
    if (adjustment.grainSettings) {
        const GrainSettings &grain = *adjustment.grainSettings;
        object.insert("grainSettings", QJsonObject{{"amount", grain.amount}, {"size", grain.size}, {"roughness", grain.roughness},
                                                   {"seed", qint64(grain.seed)}});
    }
    return object;
}
