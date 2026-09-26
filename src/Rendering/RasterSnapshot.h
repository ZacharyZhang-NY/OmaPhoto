#pragma once
#include "Document/BrushStroke.h"
#include "IO/ImageImporter.h"
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

// Immutable sparse raster; paint commits share untouched tiles.
class RasterSnapshot {
public:
    const int width;
    const int height;
    const QImage base;
    const QRectF baseRect;
    const std::vector<BrushPatch> patches;
    const bool isMask;
    // Origin of the halving grids, kept across commits.
    const QPointF alignment;

    RasterSnapshot(int width, int height, QImage base, QRectF baseRect, std::vector<BrushPatch> patches,
                   bool isMask = false, std::optional<QPointF> alignment = std::nullopt);

    static std::shared_ptr<const RasterSnapshot> replacing(const std::optional<ImportedImage> &source,
                                                           const QRectF &sourceRect,
                                                           const std::vector<BrushPatch> &patches,
                                                           const QRectF &crop, bool isMask = false);

    void draw(const QRectF &rect, QPainter &context) const;
    QImage makeImage() const;
    bool hasMaterializedPixels() const;
    QImage thumbnail() const;

private:
    mutable std::mutex m_lock;
    mutable QImage m_materialized;
};
