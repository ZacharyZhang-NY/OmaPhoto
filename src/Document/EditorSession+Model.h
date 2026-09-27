#pragma once
#include "Document/LayerAdjustment.h"
#include "Document/LayerAppearance.h"
#include "Document/Guides.h"
#include "Document/LayerGroups.h"
#include "Document/LayerMask.h"
#include "Document/LayerTransform.h"
#include "Document/Selection.h"
#include "Document/ShapeTool.h"
#include "Document/TypeTool.h"
#include "IO/ImageImporter.h"
#include <QString>
#include <QHash>
#include <QUuid>
#include <map>
#include <optional>
#include <vector>

struct ImageLayer {
    QUuid id;
    std::optional<ImportedImage> asset;
    LayerTransform transform;
    QString name;
    bool isVisible = true;
    std::optional<QUuid> parentID;
    bool isGroup = false;
    double opacity = 1;
    LayerBlendMode blendMode = LayerBlendMode::normal;
    std::optional<QUuid> maskSourceID;
    std::optional<LayerMask> mask;
    std::optional<LayerAdjustment> adjustment;
    // Set on layers the Shape tool made; see `liveShape`.
    std::optional<LayerShape> shape;
    // A stroke, shadows or overlay drawn round the pixels.
    std::optional<LayerEffects> effects;
    std::optional<LayerText> text;

    ImageLayer(ImportedImage imported, QPointF origin);
    ImageLayer(QString name, QSizeF blankSize);
    // A layer as a project records it.
    ImageLayer(const ProjectLayerRecord &record, std::optional<ImportedImage> asset, std::optional<LayerMask> mask);

    QPointF origin() const { return transform.origin; }
    QSizeF size() const { return transform.size; }
    ProjectLayerRecord hierarchyRecord() const;
    // Drawn opacity, folders included (LayerOpacity).
    double effectiveOpacity(const std::map<QUuid, ImageLayer> &byID) const;
    LayerTransform maskTransform() const;
    // The shape this layer still is: none once repainted otherwise.
    std::optional<LayerShape> liveShape() const;
    // The text this layer still is: none once repainted otherwise.
    std::optional<LayerText> liveText() const;
    friend bool operator==(const ImageLayer &lhs, const ImageLayer &rhs);
};

// The layer's place in the list, or -1.
int indexOf(const std::vector<ImageLayer> &layers, std::optional<QUuid> id);

struct ProjectSnapshot;

struct CanvasDocument {
    QUuid id;
    int width;
    int height;
    double resolution = 72;
    std::vector<ImageLayer> layers; // Bottom to top.
    // Saved with the project; undo covers them.
    std::vector<CanvasGuide> guides;
    // Part of the document so undo covers it; never saved.
    std::optional<DocumentSelection> selection;

    CanvasDocument(int width, int height);
    // The canvas a project holds, each layer as recorded.
    explicit CanvasDocument(const ProjectSnapshot &snapshot);

    QSizeF size() const { return QSizeF(width, height); }
    static std::optional<int> validDimension(const QString &value);
    std::vector<LayerHierarchy::Entry> hierarchyEntries() const;
    QSet<QUuid> effectiveVisibleIDs() const;
    QHash<QUuid, double> effectiveOpacities() const;
    std::vector<ImageLayer> renderLayers() const;
    friend bool operator==(const CanvasDocument &lhs, const CanvasDocument &rhs) = default;
};

enum class NavigationTool {
    move, marquee, lasso, wand, crop, brush, spotHealing, cloneStamp, blur,
    gradient, shape, type, eyedropper, hand, zoom, idle
};

// The rail's tooltip: the tool's name and its key.
QString label(NavigationTool tool);
bool isBrushTool(NavigationTool tool);
bool isSelectionTool(NavigationTool tool);
