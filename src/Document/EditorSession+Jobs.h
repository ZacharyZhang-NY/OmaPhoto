#pragma once
#include "Document/BrushStroke.h"
#include "Document/Filters.h"
#include "Document/HueSaturation.h"
#include "Document/Selection.h"
#include "IO/ImageImporter.h"
#include "IO/ProjectStore.h"
#include <QImage>
#include <QPainterPath>
#include <QSet>
#include <QUrl>
#include <functional>
#include <map>
#include <optional>

// The private structs of EditorSession, its private base of types.
struct SessionJobs {
    // A brush family's tip while another family is in use.
    struct ParkedTip {
        double diameter;
        double hardness;
        double opacity;
    };
    struct ImportRequest {
        QList<QUrl> urls;
        std::optional<QPointF> point;
        std::function<void()> done;
    };
    struct Decoded {
        std::optional<ImportedImage> asset;
        QString failure;
    };
    struct Baked {
        std::map<QUuid, ImportedImage> images;
        std::optional<QString> failure;
    };
    // What an invert waits for and writes back.
    struct Inverting {
        QUuid layerID;
        bool mask;
        std::optional<ImageIdentity> image;
        std::optional<ImageIdentity> maskImage;
        std::function<void()> done;
    };
    struct Inverted {
        std::optional<QImage> image;
        std::optional<QString> failure;
    };
    // What a wand click waits for and applies.
    struct Wanding {
        SelectionMode mode;
        QUuid documentID;
        std::function<void()> done;
    };
    struct Wanded {
        std::optional<QPainterPath> path;
        std::optional<QString> failure;
    };
    // The last stroke's end, for a Shift-click's line.
    struct LastBrushPoint {
        QPointF point;
        QUuid layerID;
        bool mask;
    };
    struct PendingOpacityDigit {
        int digit;
        double time;
    };
    // A raster edit's result on the way to the layer.
    struct RasterMade {
        std::optional<BrushCommit::Output> output;
        std::optional<QString> failure;
    };
    struct CommittingRaster {
        std::shared_ptr<const BrushStroke> stroke;
        QString name;
        std::function<void()> alsoApply;
        std::function<void()> done;
    };
    // A filter's preview, and its result for the layer.
    struct Filtered {
        std::optional<QImage> image;
        std::optional<QString> failure;
        // The job's settings, which the preview was made with.
        FilterSettings settings;
    };
    struct FilterMade {
        std::optional<ImportedImage> asset;
        std::optional<LayerTransform> transform;
        std::optional<QString> failure;
    };
    // The crop's resized project, or why it failed.
    struct Cropped {
        std::optional<ProjectSnapshot> snapshot;
        std::optional<QString> failure;
    };
    // Levels' result for the layer.
    struct Leveled {
        std::optional<ImportedImage> asset;
        std::optional<QString> failure;
    };
    // A commit's caller, waiting for the preview or the result.
    struct Committing {
        QUuid editID;
        std::function<void()> done;
    };
    // Hue/Saturation's pixels, or why they failed.
    struct Adjusted {
        std::optional<AdjustedPixels> pixels;
        std::optional<QString> failure;
    };
    // The layers beneath an adjustment, drawn, or why not.
    struct AdjustmentInput {
        std::optional<ImportedImage> asset;
        std::optional<QString> failure;
    };
    // A Hue/Saturation commit keeps its edit and its caller.
    struct CommittingHue {
        HueSaturationEdit edit;
        std::function<void()> done;
    };
    // A merge worked out once: what goes, what it becomes.
    struct MergePlan {
        std::vector<QUuid> ids;
        QSet<QUuid> removed;
        QString name;
        std::optional<QUuid> parent;
        QUuid anchor;
        QString action;
    };
    struct TransformDuplicate {
        std::vector<QUuid> copies;
        QSet<QUuid> source;
        QUuid primary;
    };
};
