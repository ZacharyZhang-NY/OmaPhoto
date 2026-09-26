#pragma once
#include "IO/ProjectStore.h"
#include <QJsonObject>
#include <QJsonValue>

// Swift's synthesized Codable by hand: the readers the manifest shares.
namespace ManifestJson {
[[noreturn]] void refuse();
QString string(const QJsonValue &value);
bool boolean(const QJsonValue &value);
double number(const QJsonValue &value);
// Swift's Int: Qt holds a whole 64-bit token exactly.
qint64 integer(const QJsonValue &value);
QUuid uuid(const QJsonValue &value);

// Absent and null both mean nil.
template <typename Decode>
auto optional(const QJsonObject &object, const char *key, Decode decode) -> std::optional<decltype(decode(QJsonValue()))>
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull())
        return std::nullopt;
    return decode(value);
}

// LayerAdjustment's keys, in ProjectStore+Adjustment.cpp.
LayerAdjustment adjustment(const QJsonValue &value);
QJsonObject encoded(const LayerAdjustment &adjustment);

// LayerEffects' keys, in ProjectStore+Effects.cpp.
LayerEffects effects(const QJsonValue &value);
QJsonObject encoded(const LayerEffects &effects);
}
