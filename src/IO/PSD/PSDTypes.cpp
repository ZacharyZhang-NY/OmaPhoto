#include "IO/PSD/PSDTypes.h"
#include <map>

namespace {
QString description(PSDError::Kind kind)
{
    switch (kind) {
    case PSDError::Kind::truncated:
        return QStringLiteral("The Photoshop file could not be read. It may be damaged or incomplete.");
    case PSDError::Kind::unsupportedVersion:
        return QStringLiteral("Large Document (.psb) Photoshop files aren’t supported.");
    case PSDError::Kind::unsupportedColorMode:
    case PSDError::Kind::unsupportedDepth:
        return QStringLiteral("Only 8-bit RGB Photoshop files can be imported.");
    case PSDError::Kind::unsupportedCompression:
        return QStringLiteral("This Photoshop file uses a layer compression method that isn’t supported.");
    }
    Q_UNREACHABLE();
}
}

PSDError::PSDError(Kind kind) : std::runtime_error(description(kind).toStdString()), kind(kind) {}

std::optional<LayerBlendMode> fromPSD(const QString &key)
{
    static const std::map<QString, LayerBlendMode> modes{
        {QStringLiteral("norm"), LayerBlendMode::normal},     {QStringLiteral("mul "), LayerBlendMode::multiply},
        {QStringLiteral("scrn"), LayerBlendMode::screen},     {QStringLiteral("over"), LayerBlendMode::overlay},
        {QStringLiteral("sLit"), LayerBlendMode::softLight},  {QStringLiteral("dark"), LayerBlendMode::darken},
        {QStringLiteral("lite"), LayerBlendMode::lighten},    {QStringLiteral("diff"), LayerBlendMode::difference},
        {QStringLiteral("div "), LayerBlendMode::colorDodge}, {QStringLiteral("idiv"), LayerBlendMode::colorBurn},
        {QStringLiteral("hue "), LayerBlendMode::hue},        {QStringLiteral("sat "), LayerBlendMode::saturation},
        {QStringLiteral("colr"), LayerBlendMode::color},      {QStringLiteral("lum "), LayerBlendMode::luminosity},
    };
    if (!modes.contains(key))
        return std::nullopt;
    return modes.at(key);
}

std::optional<LayerBlendMode> PSDRecord::blendMode() const
{
    return fromPSD(blendKey);
}
