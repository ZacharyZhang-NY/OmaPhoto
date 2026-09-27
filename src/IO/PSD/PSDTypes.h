#pragma once
#include "Document/EditorSession+Model.h"
#include <QImage>
#include <QRectF>
#include <QString>
#include <QUuid>
#include <optional>
#include <stdexcept>
#include <vector>

class PSDError : public std::runtime_error {
public:
    enum class Kind { truncated, unsupportedVersion, unsupportedColorMode, unsupportedDepth, unsupportedCompression };
    explicit PSDError(Kind kind);
    Kind kind;
};

// A note on what the import changes, by layer.
struct PSDConversion {
    QUuid id = QUuid::createUuid();
    QString layerName;
    QString message;
    friend bool operator==(const PSDConversion &, const PSDConversion &) = default;
};

enum class PSDLayerKind { raster, group, adjustment, text, smartObject, effects, vector };

struct PSDRecord {
    QUuid id;
    std::optional<QUuid> parentID;
    QString name;
    bool isGroup = false;
    bool isVisible = true;
    double opacity = 1;
    QString blendKey = QStringLiteral("norm");
    bool clipping = false;
    QRectF bounds;
    std::optional<QImage> image;
    std::optional<QImage> mask;
    bool maskEnabled = true;
    bool maskLinked = true;
    std::optional<LayerAdjustment> adjustment;
    PSDLayerKind kind = PSDLayerKind::raster;
    std::optional<LayerShapeStyle> shape;
    std::vector<QString> shapeNotes;
    std::optional<LayerBlendMode> blendMode() const;
};

struct PSDDocument {
    int width;
    int height;
    double resolution;
    // Bottom to top, folders included; hidden dividers are not stored.
    std::vector<PSDRecord> layers;
};

// Swift's `LayerBlendMode.fromPSD`: Photoshop's four-letter keys.
std::optional<LayerBlendMode> fromPSD(const QString &key);
