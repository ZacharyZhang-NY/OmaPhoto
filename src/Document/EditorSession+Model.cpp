#include "Document/EditorSession+Model.h"
#include "IO/ProjectStore.h"
#include <algorithm>

ImageLayer::ImageLayer(ImportedImage imported, QPointF origin)
    : id(QUuid::createUuid()), asset(std::move(imported)),
      transform{.origin = origin, .size = QSizeF(asset->size())}, name(asset->name)
{
}

int indexOf(const std::vector<ImageLayer> &layers, std::optional<QUuid> id)
{
    const auto found = std::find_if(layers.begin(), layers.end(), [&](const ImageLayer &layer) { return id && layer.id == *id; });
    return found == layers.end() ? -1 : int(found - layers.begin());
}

ImageLayer::ImageLayer(QString name, QSizeF blankSize)
    : id(QUuid::createUuid()), transform{.origin = QPointF(0, 0), .size = blankSize}, name(std::move(name))
{
}

ImageLayer::ImageLayer(const ProjectLayerRecord &record, std::optional<ImportedImage> asset, std::optional<LayerMask> mask)
    : id(record.id), asset(std::move(asset)), transform(record.transform), name(record.name), isVisible(record.isVisible),
      parentID(record.parentID), isGroup(record.isGroup == true), opacity(record.opacity.value_or(1)),
      blendMode(record.blendMode.value_or(LayerBlendMode::normal)), maskSourceID(record.maskSourceID), mask(std::move(mask)),
      adjustment(record.adjustment), effects(record.effects)
{
}

bool operator==(const ImageLayer &lhs, const ImageLayer &rhs)
{
    const bool sameImage = lhs.asset.has_value() == rhs.asset.has_value()
        && (!lhs.asset || lhs.asset->identity() == rhs.asset->identity());
    return lhs.id == rhs.id && lhs.name == rhs.name && lhs.isVisible == rhs.isVisible
        && lhs.transform == rhs.transform && sameImage && lhs.parentID == rhs.parentID
        && lhs.isGroup == rhs.isGroup && lhs.opacity == rhs.opacity && lhs.blendMode == rhs.blendMode
        && lhs.mask == rhs.mask && lhs.maskSourceID == rhs.maskSourceID && lhs.adjustment == rhs.adjustment && lhs.shape == rhs.shape
        && lhs.text == rhs.text && lhs.effects == rhs.effects;
}

CanvasDocument::CanvasDocument(int width, int height)
    : id(QUuid::createUuid()), width(width), height(height)
{
}

CanvasDocument::CanvasDocument(const ProjectSnapshot &snapshot)
    : id(snapshot.manifest.documentID), width(int(snapshot.manifest.width)), height(int(snapshot.manifest.height)),
      resolution(snapshot.manifest.resolution.value_or(72))
{
    for (const ProjectLayerRecord &record : snapshot.manifest.layers) {
        const auto image = snapshot.images.find(record.id);
        layers.push_back(ImageLayer(record, image == snapshot.images.end() ? std::nullopt : std::optional(image->second),
                                    snapshot.mask(record)));
    }
}

namespace {
// Swift's CharacterSet.whitespaces: category Zs and tab.
bool isWhitespace(QChar character)
{
    return character == u'\t' || character.category() == QChar::Separator_Space;
}

// Swift's Int(String): optional sign, then ASCII digits only.
std::optional<int> wholeNumber(QStringView text)
{
    qsizetype index = text.startsWith(u'+') || text.startsWith(u'-') ? 1 : 0;
    if (index == text.size())
        return std::nullopt;
    for (; index < text.size(); ++index) {
        if (text[index] < u'0' || text[index] > u'9')
            return std::nullopt;
    }
    bool fits = false;
    const int number = text.toInt(&fits);
    return fits ? std::optional<int>(number) : std::nullopt;
}
}

std::optional<int> CanvasDocument::validDimension(const QString &value)
{
    qsizetype first = 0, last = value.size();
    while (first < last && isWhitespace(value[first]))
        ++first;
    while (last > first && isWhitespace(value[last - 1]))
        --last;
    const std::optional<int> number = wholeNumber(QStringView(value).mid(first, last - first));
    if (!number || *number < 1 || *number > 30'000)
        return std::nullopt;
    return number;
}

QString label(NavigationTool tool)
{
    switch (tool) {
    case NavigationTool::move: return QStringLiteral("Move / Transform (V)");
    case NavigationTool::marquee: return QStringLiteral("Marquee (M)");
    case NavigationTool::lasso: return QStringLiteral("Lasso (L)");
    case NavigationTool::wand: return QStringLiteral("Magic Wand (W)");
    case NavigationTool::crop: return QStringLiteral("Crop (C)");
    case NavigationTool::brush: return QStringLiteral("Brush (B) · Eraser (E)");
    case NavigationTool::spotHealing: return QStringLiteral("Spot Healing Brush (J)");
    case NavigationTool::cloneStamp: return QStringLiteral("Clone Stamp (S) · Alt-click sets the source");
    case NavigationTool::blur: return QStringLiteral("Smear (R)");
    case NavigationTool::gradient: return QStringLiteral("Gradient (G)");
    case NavigationTool::shape: return QStringLiteral("Shape (U) · Shift-U switches Rectangle/Ellipse");
    case NavigationTool::type: return QStringLiteral("Type (T)");
    case NavigationTool::eyedropper: return QStringLiteral("Eyedropper (I)");
    case NavigationTool::hand: return QStringLiteral("Hand (H)");
    case NavigationTool::zoom: return QStringLiteral("Zoom (Z)");
    // No tool sits in no rail: nothing to name.
    case NavigationTool::idle: break;
    }
    throw std::logic_error("no tool has no label");
}

bool isBrushTool(NavigationTool tool)
{
    return tool == NavigationTool::brush || tool == NavigationTool::spotHealing
        || tool == NavigationTool::cloneStamp || tool == NavigationTool::blur;
}

bool isSelectionTool(NavigationTool tool)
{
    return tool == NavigationTool::marquee || tool == NavigationTool::lasso || tool == NavigationTool::wand;
}
