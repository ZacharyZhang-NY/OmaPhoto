#pragma once
#include "Document/BrushStroke.h"
#include "Document/LayerEffects.h"
#include <QImage>
#include <QUuid>
#include <map>
#include <memory>
#include <optional>

// Swift's LayerEffectsSurface: effects redone only where paint lands.
class LayerEffectsSurface {
public:
    // Swift's init?: none past 80 million pixels, logged. Effects shown.
    static std::unique_ptr<LayerEffectsSurface> make(QUuid layerID, const LayerEffects &effects, QSizeF grid, QRectF sourceRect);
    // Whether it still fits the stroke and settings.
    bool matches(QUuid layerID, const LayerEffects &effects, QSizeF grid, QRectF sourceRect) const;
    // Everything at first, then only where the paint changed.
    void update(const std::optional<ImportedImage> &base, const std::vector<BrushPatch> &patches, const std::optional<QImage> &mask);
    const std::optional<QImage> &image() const { return m_image; }

    const QUuid layerID;
    // The stroke's grid in layer pixels, the layer's own inside.
    const QSizeF grid;
    const QRectF sourceRect;
    // The room the effects need round the pixels.
    const double margin;
    // Where it was last drawn, handed on at the end.
    std::optional<LayerTransform> placement;

private:
    LayerEffectsSurface(QUuid layerID, const LayerEffects &effects, QSizeF grid, QRectF sourceRect, double margin, QImage context);
    double reach() const;
    void compose(const QRectF &region, const std::optional<ImportedImage> &base, const std::vector<BrushPatch> &patches,
                 const std::optional<QImage> &mask);
    QImage window(const QRectF &region, const std::optional<ImportedImage> &base, const std::vector<BrushPatch> &patches,
                  const std::optional<QImage> &mask) const;

    const LayerEffects m_effects;
    QImage m_context;
    // Each tile's origin and the pixels last taken from it.
    std::map<std::pair<qint64, qint64>, qint64> m_taken;
    std::optional<QImage> m_image;
};
