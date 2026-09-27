#pragma once
#include "Document/Guides.h"
#include "Document/LayerAdjustment.h"
#include "Document/LayerAppearance.h"
#include "Document/LayerEffects.h"
#include "Document/LayerTransform.h"
#include "Document/ShapeTool.h"
#include "Document/TypeTool.h"
#include "IO/ImageImporter.h"
#include <QByteArray>
#include <QString>
#include <QUuid>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

struct LayerMask;

// Absent optionals stay out of the manifest.
struct ProjectLayerRecord {
    QUuid id;
    QString name;
    bool isVisible;
    LayerTransform transform;
    std::optional<QString> imageFile;
    std::optional<QUuid> parentID = std::nullopt;
    std::optional<bool> isGroup = std::nullopt;
    std::optional<double> opacity = std::nullopt;
    std::optional<LayerBlendMode> blendMode = std::nullopt;
    std::optional<QString> maskFile = std::nullopt;
    std::optional<bool> maskEnabled = std::nullopt;
    std::optional<QUuid> maskSourceID = std::nullopt;
    std::optional<LayerAdjustment> adjustment = std::nullopt;
    std::optional<LayerTransform> maskPlacement = std::nullopt;
    // Absent in older projects, which means linked.
    std::optional<bool> maskLinked = std::nullopt;
    // A shape layer's shape; older versions keep the pixels alone.
    std::optional<LayerShapeStyle> shape = std::nullopt;
    // What the layer draws round itself; its pixels stay.
    std::optional<LayerEffects> effects = std::nullopt;
    std::optional<LayerTextStyle> text = std::nullopt;

    // Drawn opacity, folders included; defined in LayerGroups.cpp.
    double effectiveOpacity(const std::map<QUuid, ProjectLayerRecord> &byID) const;
};

// A UUID as Swift writes it: upper case, no braces.
QString uuidString(const QUuid &id);

struct ProjectManifest {
    QString format = QStringLiteral("com.compositor.project");
    // Swift's Int: a wild number must reach validation whole.
    qint64 version = 8;
    QString colorSpace = QStringLiteral("sRGB");
    // Version-1 projects carry none: 72 pixels per inch.
    std::optional<double> resolution = std::nullopt;
    QUuid documentID;
    qint64 width;
    qint64 height;
    std::optional<QUuid> activeLayerID;
    std::vector<ProjectLayerRecord> layers;
    // Alignment guides; versions 1 to 7 carry none.
    std::optional<std::vector<CanvasGuide>> guides = std::nullopt;

    // Pretty JSON with sorted keys; absent optionals are left out.
    QByteArray encoded() const;
    // Throws ProjectError(invalid) on anything Swift's decoder refuses.
    static ProjectManifest decoded(const QByteArray &data);
    // Format and version alone: newer files may not decode.
    static std::pair<QString, qint64> header(const QByteArray &data);
};

struct ProjectSnapshot {
    ProjectManifest manifest;
    std::map<QUuid, ImportedImage> images;
    std::map<QUuid, ImportedImage> masks = {};
    // A record's mask, from its mask file and fields.
    std::optional<LayerMask> mask(const ProjectLayerRecord &layer) const;
};

class ProjectError : public std::runtime_error {
public:
    enum class Kind { invalid, version, missingImage, tooLarge, encode };
    explicit ProjectError(Kind kind);
    static ProjectError unsupportedVersion(qint64 version);
    const Kind kind;
    // Set only for Kind::version.
    const std::optional<qint64> version;

private:
    ProjectError(Kind kind, std::optional<qint64> version, const QString &description);
};

// A .comp project: a directory holding manifest.json and images/.
namespace ProjectStore {
// Replaces the project in one step or leaves it untouched.
void save(const ProjectSnapshot &snapshot, const QString &path);
ProjectSnapshot load(const QString &path);
}
