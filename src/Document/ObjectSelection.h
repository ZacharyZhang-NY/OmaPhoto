#pragma once
#include <QImage>
#include <QPainterPath>
#include <optional>

struct ObjectSelectionSettings {
    // Read the visible composite rather than the active layer.
    bool sampleAllLayers = true;
    // Positive erodes the mask inward; negative grows it outward.
    int edgeOffset = 0;
    friend bool operator==(const ObjectSelectionSettings &, const ObjectSelectionSettings &) = default;
};

// Swift's ObjectSelection; U²-Net regions stand for Vision's instances.
namespace ObjectSelection {
// The model's grid the regions are found on.
inline constexpr int grid = 320;
// The outline in pixels; none off the image or background.
std::optional<QPainterPath> select(const QImage &image, QPointF point, int edgeOffset, bool smoothEdges);
// The binary grid's region holding the cell, else none.
std::optional<std::vector<uchar>> region(const std::vector<uchar> &mask, int width, int height, int x, int y);
// Swift's steps, a pixel ring each: erode inward, dilate outward.
std::vector<uchar> adjusted(std::vector<uchar> mask, int width, int height, int edgeOffset);
// Stair steps rounded off; winding and subpath order kept.
QPainterPath smoothed(const QPainterPath &path);
}
