#include "IO/ProjectStore+Json.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QVariant>

// Swift's synthesized Codable by hand: same keys, same refusals.
void ManifestJson::refuse()
{
    throw ProjectError(ProjectError::Kind::invalid);
}

QString ManifestJson::string(const QJsonValue &value)
{
    if (!value.isString())
        refuse();
    return value.toString();
}

bool ManifestJson::boolean(const QJsonValue &value)
{
    if (!value.isBool())
        refuse();
    return value.toBool();
}

double ManifestJson::number(const QJsonValue &value)
{
    if (!value.isDouble())
        refuse();
    return value.toDouble();
}

qint64 ManifestJson::integer(const QJsonValue &value)
{
    const QVariant exact = value.toVariant();
    if (exact.typeId() != QMetaType::LongLong)
        refuse();
    return exact.toLongLong();
}

QUuid ManifestJson::uuid(const QJsonValue &value)
{
    // Anchored at both ends: `$` would pass a trailing newline.
    static const QRegularExpression canonical(
        QRegularExpression::anchoredPattern("[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}"));
    const QString text = string(value);
    if (!canonical.match(text).hasMatch())
        refuse();
    return QUuid::fromString(text);
}

namespace {
using namespace ManifestJson;

// CGPoint and CGSize: two numbers; what is missing is refused.
std::pair<double, double> pair(const QJsonValue &value)
{
    return {number(value.toArray().at(0)), number(value.toArray().at(1))};
}

LayerTransform transform(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    const auto [x, y] = pair(object.value("origin"));
    const auto [width, height] = pair(object.value("size"));
    const std::optional<LayerSampling> sampling = layerSampling(string(object.value("sampling")));
    if (!sampling)
        refuse();
    return {.origin = {x, y}, .size = {width, height}, .rotation = number(object.value("rotation")),
            .flipX = boolean(object.value("flipX")), .flipY = boolean(object.value("flipY")), .sampling = *sampling};
}

LayerBlendMode blendMode(const QJsonValue &value)
{
    const std::optional<LayerBlendMode> mode = layerBlendMode(string(value));
    if (!mode)
        refuse();
    return *mode;
}

QPointF point(const QJsonValue &value)
{
    const auto [x, y] = pair(value);
    return {x, y};
}

LayerShapeStyle shape(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    const std::optional<ShapeKind> kind = shapeKind(string(object.value("kind")));
    if (!kind)
        refuse();
    return {.kind = *kind, .red = number(object.value("red")), .green = number(object.value("green")), .blue = number(object.value("blue")),
            .cornerRadius = number(object.value("cornerRadius")), .lineWidth = optional(object, "lineWidth", number),
            .start = optional(object, "start", point), .end = optional(object, "end", point)};
}

QSizeF size(const QJsonValue &value)
{
    const auto [width, height] = pair(value);
    return {width, height};
}

// Synthesized Codable skips defaults: all but the box required.
LayerTextStyle textStyle(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    const std::optional<TextAlignment> alignment = textAlignment(string(object.value("alignment")));
    if (!alignment)
        refuse();
    return {.content = string(object.value("content")), .fontName = string(object.value("fontName")), .fontSize = number(object.value("fontSize")),
            .red = number(object.value("red")), .green = number(object.value("green")), .blue = number(object.value("blue")),
            .alignment = *alignment, .tracking = number(object.value("tracking")), .leading = number(object.value("leading")),
            .boxSize = optional(object, "boxSize", size)};
}

CanvasGuide guide(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    const std::optional<CanvasGuide::Axis> axis = guideAxis(string(object.value("axis")));
    // A value that is no object has no axis: refused.
    if (!axis)
        refuse();
    return {uuid(object.value("id")), *axis, number(object.value("position"))};
}

std::vector<CanvasGuide> guides(const QJsonValue &value)
{
    if (!value.isArray())
        refuse();
    std::vector<CanvasGuide> result;
    for (const QJsonValue &each : value.toArray())
        result.push_back(guide(each));
    return result;
}

ProjectLayerRecord layer(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    return {.id = uuid(object.value("id")), .name = string(object.value("name")), .isVisible = boolean(object.value("isVisible")),
            .transform = transform(object.value("transform")), .imageFile = optional(object, "imageFile", string),
            .parentID = optional(object, "parentID", uuid), .isGroup = optional(object, "isGroup", boolean),
            .opacity = optional(object, "opacity", number), .blendMode = optional(object, "blendMode", blendMode),
            .maskFile = optional(object, "maskFile", string), .maskEnabled = optional(object, "maskEnabled", boolean),
            .maskSourceID = optional(object, "maskSourceID", uuid), .adjustment = optional(object, "adjustment", ManifestJson::adjustment),
            .maskPlacement = optional(object, "maskPlacement", transform),
            .maskLinked = optional(object, "maskLinked", boolean), .shape = optional(object, "shape", shape),
            .effects = optional(object, "effects", ManifestJson::effects), .text = optional(object, "text", textStyle)};
}

QJsonObject encoded(const LayerTransform &transform)
{
    return {{"origin", QJsonArray{transform.origin.x(), transform.origin.y()}},
            {"size", QJsonArray{transform.size.width(), transform.size.height()}},
            {"rotation", transform.rotation}, {"flipX", transform.flipX}, {"flipY", transform.flipY},
            {"sampling", rawValue(transform.sampling)}};
}

QJsonObject encoded(const LayerShapeStyle &shape)
{
    QJsonObject object{{"kind", rawValue(shape.kind)}, {"red", shape.red}, {"green", shape.green}, {"blue", shape.blue},
                       {"cornerRadius", shape.cornerRadius}};
    if (shape.lineWidth)
        object.insert("lineWidth", *shape.lineWidth);
    if (shape.start)
        object.insert("start", QJsonArray{shape.start->x(), shape.start->y()});
    if (shape.end)
        object.insert("end", QJsonArray{shape.end->x(), shape.end->y()});
    return object;
}

QJsonObject encoded(const LayerTextStyle &text)
{
    QJsonObject object{{"content", text.content}, {"fontName", text.fontName}, {"fontSize", text.fontSize}, {"red", text.red}, {"green", text.green},
                       {"blue", text.blue}, {"alignment", rawValue(text.alignment)}, {"tracking", text.tracking}, {"leading", text.leading}};
    if (text.boxSize)
        object.insert("boxSize", QJsonArray{text.boxSize->width(), text.boxSize->height()});
    return object;
}

QJsonObject encoded(const ProjectLayerRecord &layer)
{
    QJsonObject object{{"id", uuidString(layer.id)}, {"name", layer.name}, {"isVisible", layer.isVisible},
                       {"transform", encoded(layer.transform)}};
    if (layer.imageFile)
        object.insert("imageFile", *layer.imageFile);
    if (layer.parentID)
        object.insert("parentID", uuidString(*layer.parentID));
    if (layer.isGroup)
        object.insert("isGroup", *layer.isGroup);
    if (layer.opacity)
        object.insert("opacity", *layer.opacity);
    if (layer.blendMode)
        object.insert("blendMode", rawValue(*layer.blendMode));
    if (layer.maskFile)
        object.insert("maskFile", *layer.maskFile);
    if (layer.maskEnabled)
        object.insert("maskEnabled", *layer.maskEnabled);
    if (layer.maskSourceID)
        object.insert("maskSourceID", uuidString(*layer.maskSourceID));
    if (layer.adjustment)
        object.insert("adjustment", ManifestJson::encoded(*layer.adjustment));
    if (layer.maskPlacement)
        object.insert("maskPlacement", encoded(*layer.maskPlacement));
    if (layer.maskLinked)
        object.insert("maskLinked", *layer.maskLinked);
    if (layer.shape)
        object.insert("shape", encoded(*layer.shape));
    if (layer.effects)
        object.insert("effects", ManifestJson::encoded(*layer.effects));
    if (layer.text)
        object.insert("text", encoded(*layer.text));
    return object;
}
}

QString uuidString(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces).toUpper();
}

QByteArray ProjectManifest::encoded() const
{
    QJsonArray records;
    for (const ProjectLayerRecord &layer : layers)
        records.append(::encoded(layer));
    QJsonObject object{{"format", format}, {"version", version}, {"colorSpace", colorSpace}, {"documentID", uuidString(documentID)},
                       {"width", width}, {"height", height}, {"layers", records}};
    if (resolution)
        object.insert("resolution", *resolution);
    if (activeLayerID)
        object.insert("activeLayerID", uuidString(*activeLayerID));
    if (guides) {
        QJsonArray lines;
        for (const CanvasGuide &guide : *guides)
            lines.append(QJsonObject{{"id", uuidString(guide.id)}, {"axis", rawValue(guide.axis)}, {"position", guide.position}});
        object.insert("guides", lines);
    }
    // Swift indents by two; four would outgrow 4 MiB sooner.
    QByteArray json;
    for (const QByteArray &line : QJsonDocument(object).toJson(QJsonDocument::Indented).split('\n')) {
        const qsizetype indent = line.size() - line.trimmed().size();
        json += line.mid(indent / 2) + '\n';
    }
    json.chop(1);
    return json;
}

ProjectManifest ProjectManifest::decoded(const QByteArray &data)
{
    const QJsonObject object = QJsonDocument::fromJson(data).object();
    if (!object.value("layers").isArray())
        refuse();
    std::vector<ProjectLayerRecord> records;
    for (const QJsonValue &value : object.value("layers").toArray())
        records.push_back(layer(value));
    return {.format = string(object.value("format")), .version = integer(object.value("version")),
            .colorSpace = string(object.value("colorSpace")), .resolution = optional(object, "resolution", number),
            .documentID = uuid(object.value("documentID")), .width = integer(object.value("width")),
            .height = integer(object.value("height")), .activeLayerID = optional(object, "activeLayerID", uuid), .layers = records,
            .guides = optional(object, "guides", ::guides)};
}

std::pair<QString, qint64> ProjectManifest::header(const QByteArray &data)
{
    const QJsonObject object = QJsonDocument::fromJson(data).object();
    return {string(object.value("format")), integer(object.value("version"))};
}
