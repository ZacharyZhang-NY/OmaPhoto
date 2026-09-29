#pragma once
#include "Document/DocumentLimits.h"
#include "Document/LayerAdjustment.h"
#include "Document/LayerAppearance.h"
#include <QImage>
#include <QPainter>
#include <QUuid>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>

// Per-render dependency cache: a layer shows through its source's alpha.
class LiveMaskRenderer {
public:
    static constexpr qint64 pixelBudget = DocumentLimits::maxSurfacePixels;
    using Source = std::function<std::optional<QUuid>(QUuid)>;
    // Draws a layer through `clip`: device-sized coverage, null for none.
    using DrawOwn = std::function<void(QUuid, QPainter &, const QImage &clip)>;

    LiveMaskRenderer(Source source, DrawOwn drawOwn, qint64 pixelBudget = LiveMaskRenderer::pixelBudget);
    // Clipping stacks share the base's alpha instead of repainting it.
    void prepareStacks(const std::vector<QUuid> &ids, const std::function<std::optional<QUuid>(QUuid)> &parent,
                       const std::function<LayerBlendMode(QUuid)> &blend);
    void drawComposite(QUuid id, QPainter &context, const QImage &clip = QImage());
    void draw(QUuid id, QPainter &context, const QImage &clip = QImage());
    // An adjustment layer's settings, opacity and own mask as coverage.
    std::function<std::optional<LayerAdjustment>(QUuid)> adjustment = [](QUuid) { return std::optional<LayerAdjustment>(); };
    std::function<double(QUuid)> adjustmentOpacity = [](QUuid) { return 1.0; };
    std::function<void(QUuid, const QPainter &, QImage &coverage)> adjustmentClip = [](QUuid, const QPainter &, QImage &) {};
    // Painter units a document pixel: the canvas's zoom.
    double adjustmentScale = 1;

private:
    // The pixels beneath, adjusted and mixed back through coverage.
    void adjust(QUuid id, QPainter &context, const QImage &clip);
    // A layer's alpha through its own source; nil on failure.
    std::optional<QImage> coverage(QUuid id, const QPainter &context);
    bool fits(const QPainter &context) const;

    const Source m_source;
    const DrawOwn m_drawOwn;
    const qint64 m_pixelBudget;
    // Standard containers: Qt 6.4's hash inserts terminate out of memory.
    std::map<QUuid, QImage> m_cache;
    std::set<QUuid> m_visiting;
    std::map<QUuid, std::vector<QUuid>> m_stacks;
    std::set<QUuid> m_stacked;
    std::map<QUuid, LayerBlendMode> m_stackModes;
    // Every prepared layer's mode, as Swift's `blendMode`.
    std::map<QUuid, LayerBlendMode> m_modes;
};
