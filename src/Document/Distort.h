#pragma once
#include "Document/LayerTransform.h"
#include "IO/ImageImporter.h"
#include <QImage>
#include <QPainterPath>
#include <QRect>
#include <optional>

// Free distortion: the layer's four corners move on their own.
namespace DistortWarp {
struct Warped {
    QImage image;
    LayerTransform transform;
};
struct Trimmed {
    QImage image;
    LayerTransform transform;
    // In the warp's pixels, for cropping a mask to match.
    QRect crop;
};

// Clockwise from the top left, in document pixels.
Corners corners(const LayerTransform &transform);
// Four finite corners with some area to both halves.
bool isUsable(const Corners &corners);
// A shape a perspective can take, wound either way.
bool isConvex(const Corners &corners);
// The perspective taking the unit square onto `corners`.
QTransform homography(const Corners &corners);
// `image` under `transform`, resampled so its corners land on `corners`.
Warped warp(const QImage &image, const LayerTransform &transform, const Corners &corners, bool isMask,
            std::optional<double> limit = std::nullopt);
// A full warp cropped to its visible pixels.
Trimmed warpTrimmed(const QImage &image, const LayerTransform &transform, const Corners &corners);
// Where `placement`'s corners land under `transform`'s perspective.
Corners carried(const LayerTransform &placement, const LayerTransform &transform, const Corners &corners);
// A path through the distortion; nil when the shape folds.
std::optional<QPainterPath> mapPath(const QPainterPath &path, const QTransform &pixelToDocument, QSizeF pixelSize,
                                    const LayerTransform &transform, const Corners &corners);
// A mask warped like `warp`, its background outside the shape.
Warped warpMask(const QImage &image, const LayerTransform &transform, const Corners &corners, double background,
                std::optional<double> limit = std::nullopt);
}

// Where a distortion takes a layer: transform and corners.
struct DistortTarget {
    LayerTransform transform;
    Corners corners;
};

// The layer warped for the canvas, its mask alongside.
struct DistortPreview {
    QImage image;
    std::optional<QImage> mask;
    LayerTransform transform;
};

// Effects last warped for a distortion, kept while corners rest.
struct DistortEffectsCache {
    Corners corners;
    QImage image;
    std::optional<DistortWarp::Warped> result;
};

// The canvas's last warped preview, kept while nothing changed.
struct DistortPreviewCache {
    Corners corners;
    LayerTransform draft;
    ImageIdentity image;
    std::optional<ImageIdentity> mask;
    std::optional<DistortPreview> result;
};
