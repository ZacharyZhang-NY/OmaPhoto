#include "IO/ProjectStore+Json.h"

// LayerEffects' synthesized Codable: every key but `enabled`.
namespace {
using namespace ManifestJson;

QJsonObject object(const QJsonValue &value)
{
    if (!value.isObject())
        refuse();
    return value.toObject();
}

StrokeEffect stroke(const QJsonValue &value)
{
    const QJsonObject keys = object(value);
    return {optional(keys, "enabled", boolean), number(keys.value("size")), number(keys.value("red")), number(keys.value("green")),
            number(keys.value("blue")), number(keys.value("opacity")), boolean(keys.value("inside"))};
}

ShadowEffect shadow(const QJsonValue &value)
{
    const QJsonObject keys = object(value);
    return {optional(keys, "enabled", boolean), number(keys.value("angle")), number(keys.value("distance")), number(keys.value("blur")),
            number(keys.value("red")), number(keys.value("green")), number(keys.value("blue")), number(keys.value("opacity"))};
}

ColorOverlayEffect colorOverlay(const QJsonValue &value)
{
    const QJsonObject keys = object(value);
    return {optional(keys, "enabled", boolean), number(keys.value("red")), number(keys.value("green")), number(keys.value("blue")),
            number(keys.value("opacity"))};
}

InnerShadowEffect innerShadow(const QJsonValue &value)
{
    const QJsonObject keys = object(value);
    return {optional(keys, "enabled", boolean), number(keys.value("angle")), number(keys.value("distance")), number(keys.value("blur")),
            number(keys.value("red")), number(keys.value("green")), number(keys.value("blue")), number(keys.value("opacity"))};
}

void colour(QJsonObject &keys, double red, double green, double blue)
{
    keys.insert("red", red);
    keys.insert("green", green);
    keys.insert("blue", blue);
}
}

LayerEffects ManifestJson::effects(const QJsonValue &value)
{
    const QJsonObject keys = object(value);
    return {optional(keys, "stroke", stroke), optional(keys, "shadow", shadow), optional(keys, "colorOverlay", colorOverlay),
            optional(keys, "innerShadow", innerShadow)};
}

QJsonObject ManifestJson::encoded(const LayerEffects &effects)
{
    QJsonObject keys;
    if (effects.stroke) {
        const StrokeEffect &stroke = *effects.stroke;
        QJsonObject object{{"size", stroke.size}, {"opacity", stroke.opacity}, {"inside", stroke.inside}};
        colour(object, stroke.red, stroke.green, stroke.blue);
        if (stroke.enabled)
            object.insert("enabled", *stroke.enabled);
        keys.insert("stroke", object);
    }
    if (effects.shadow) {
        const ShadowEffect &shadow = *effects.shadow;
        QJsonObject object{{"angle", shadow.angle}, {"distance", shadow.distance}, {"blur", shadow.blur}, {"opacity", shadow.opacity}};
        colour(object, shadow.red, shadow.green, shadow.blue);
        if (shadow.enabled)
            object.insert("enabled", *shadow.enabled);
        keys.insert("shadow", object);
    }
    if (effects.colorOverlay) {
        const ColorOverlayEffect &overlay = *effects.colorOverlay;
        QJsonObject object{{"opacity", overlay.opacity}};
        colour(object, overlay.red, overlay.green, overlay.blue);
        if (overlay.enabled)
            object.insert("enabled", *overlay.enabled);
        keys.insert("colorOverlay", object);
    }
    if (effects.innerShadow) {
        const InnerShadowEffect &inner = *effects.innerShadow;
        QJsonObject object{{"angle", inner.angle}, {"distance", inner.distance}, {"blur", inner.blur}, {"opacity", inner.opacity}};
        colour(object, inner.red, inner.green, inner.blue);
        if (inner.enabled)
            object.insert("enabled", *inner.enabled);
        keys.insert("innerShadow", object);
    }
    return keys;
}
