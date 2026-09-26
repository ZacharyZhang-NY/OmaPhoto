#pragma once
#include "Rendering/LayerRenderer.h"
#include "Rendering/RasterSnapshot.h"
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

// Draws an image plus replacement tiles as one image would.
namespace TiledLayerRenderer {
struct Piece {
    // Grid pixels this piece draws.
    QRectF interior;
    // Grid pixels its image holds: the interior and a margin.
    QRectF region;
    QImage image;
    Piece offsetBy(QPointF offset) const;
};

// How one layer's grid maps into its local drawing space.
struct Frame {
    QRectF bounds;
    double pixelWidth;
    double pixelHeight;
    int level;
    double device;
    LayerSampling sampling;
    // Grid pixels the painter's clip can show.
    QRectF visible;
    QRectF mapped(const QRectF &rect) const;
};

// Piece squares: large for committed snapshots, small for live strokes.
inline constexpr double committedCell = 1024;
inline constexpr double strokeCell = 256;

// Grid pixels a change's reduced, resampled pixels can reach.
double support(int level);
void drawRaster(const std::shared_ptr<const RasterSnapshot> &raster, const LayerTransform &transform, QPointF center,
                QPainter &context, const LayerRenderer::Options &options = {});
// A tiled edit in progress over the pixels at `sourceRect`.
void drawStroke(int width, int height, const QRectF &sourceRect, const std::vector<BrushPatch> &patches, const QImage &image,
                const std::shared_ptr<const RasterSnapshot> &raster, const LayerTransform &transform, QPointF center,
                QPainter &context, const LayerRenderer::Options &options = {});
// The layer through its mask as the stroke leaves it.
void drawMaskStroke(int width, int height, const QRectF &sourceRect, const std::vector<BrushPatch> &patches,
                    const std::optional<ImportedImage> &oldMask, const QImage &image,
                    const std::shared_ptr<const RasterSnapshot> &raster, const LayerTransform &transform, QPointF center,
                    QPainter &context, const LayerRenderer::Options &options = {});
std::optional<Piece> piece(const QRectF &interior, int level, QPointF origin, const std::optional<QRectF> &bounds, bool mask,
                           const std::function<void(QPainter &, const QRectF &region)> &compose);
std::vector<QRectF> interiors(const std::vector<QRectF> &rects, double margin, double size, double step, QPointF origin,
                              const std::optional<QRectF> &visible);
QRectF aligned(const QRectF &rect, double step, QPointF origin);
}

// Committed rasters' pieces, built once per snapshot and level.
class TiledPieceCache {
public:
    static constexpr qint64 pixelBudget = 150'000'000;
    explicit TiledPieceCache(qint64 pixelBudget = TiledPieceCache::pixelBudget);
    static TiledPieceCache &shared();
    std::vector<TiledLayerRenderer::Piece> pieces(const std::shared_ptr<const RasterSnapshot> &raster, int level);

private:
    struct Key {
        const RasterSnapshot *raster;
        int level;
        bool operator==(const Key &) const = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key &key) const;
    };
    struct Entry {
        std::shared_ptr<const RasterSnapshot> raster;
        std::vector<TiledLayerRenderer::Piece> pieces;
        quint64 lastUse;
        qint64 pixels;
    };
    const qint64 m_pixelBudget;
    std::unordered_map<Key, Entry, KeyHash> m_entries;
    quint64 m_clock = 0;
    std::mutex m_lock;
};
