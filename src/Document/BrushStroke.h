#pragma once
#include "Document/EditorSession+Model.h"
#include "Document/DocumentLimits.h"
#include "Document/Gradient.h"
#include "Document/LayerTransform.h"
#include "Document/Selection.h"
#include "IO/ImageImporter.h"
#include "Rendering/BrushCoverage.h"
#include <QImage>
#include <QPainter>
#include <array>
#include <functional>
#include <map>
#include <set>

struct BrushPatch {
    QRectF rect;
    QImage image;
};

// Swift's three ways of healing a spot, in its order.
enum class SpotHealingMode { contentAware, createTexture, proximityMatch };
inline constexpr std::array allSpotHealingModes{SpotHealingMode::contentAware, SpotHealingMode::createTexture, SpotHealingMode::proximityMatch};
QString rawValue(SpotHealingMode mode);

// Tip, colour, and what the stroke does with them.
struct BrushSettings {
    double diameter = 40;
    double hardness = 1;
    double red = 0;
    double green = 0;
    double blue = 0;
    // Caps the whole stroke: overlapping dabs never exceed it.
    double opacity = 1;
    // 0–100: the brush trails the pointer on this string.
    double smoothing = 0;
    // Erase clears the layer's pixels instead of painting on them.
    bool erasing = false;
    // Spot healing takes nearby pixels instead of the colour.
    bool healing = false;
    SpotHealingMode healingMode = SpotHealingMode::contentAware;
};

// Shared raster drawing; coverage is copied without color conversion.
namespace BrushRaster {
QImage context(int width, int height, bool mask);
// Copies `image`, or its `source` part, into `rect`.
void draw(const QImage &image, const QRectF &rect, QPainter &context, const QRectF &source = QRectF());
// A solid colour through gray coverage, and a clip, 1:1.
void fill(const QColor &color, const QImage &coverage, const QRectF &rect, double alpha, QImage &context,
          QPainter::CompositionMode mode = QPainter::CompositionMode_SourceOver, const QImage &clip = QImage());
// Gray mask bytes read as alpha, without a copy.
QImage alphaView(const QImage &mask);
// CoreGraphics boundingBoxOfClipPath: the clip within the device.
QRectF visibleRect(const QPainter &context);
// Premultiplied pixels shifted right and down by fractions, bilinear.
QImage shiftedByFraction(const QImage &image, double fx, double fy);
QTransform pixelToDocument(const LayerTransform &transform, int width, int height);
}

// A stroke's pixels as a painted layer, placed and bounded.
struct PaintSnapshot {
    ImportedImage asset;
    LayerTransform transform;
    QRectF bounds;
};

// Swift's BrushCommit actor: the stroke's tiles made one image.
namespace BrushCommit {
struct Input {
    int width;
    int height;
    std::optional<ImportedImage> source;
    std::vector<BrushPatch> patches;
    bool mask;
    QString name;
    QRectF sourceRect;
    // A grown mask where no old pixel or edit reaches.
    double fill = 1;
};
struct Output {
    ImportedImage asset;
    QRectF pixelBounds;
};
ImportedImage expandMask(const ImportedImage &asset, const Input &input, const QRectF &crop);
Output render(const Input &input);
}

// Only touched 256 px tiles hold pixels; snapshots copy those.
class BrushStroke {
public:
    // `growsMask`: the brush and fills reach the canvas on masks.
    BrushStroke(const ImageLayer &layer, bool mask, const BrushSettings &settings, QSizeF canvas, bool growsMask = false);

    const ImageLayer layer;
    const bool isMask;
    const BrushSettings settings;
    const QRectF canvas;
    const int width;
    const int height;
    const QRectF sourceRect;
    const QTransform pixelToDocument;
    const LayerTransform paintTransform;
    // A mask past its pixels: 1 reveals, 0 hides.
    const double maskBackground;
    // Swift's Int: the budget left may run below zero.
    qint64 pixelLimit = DocumentLimits::documentPixelBudget();
    // Limits every edit to the document selection; nil when none.
    std::optional<SelectionClip> selectionClip;
    // Painted through the tip, placed: document or `inGrid` pixels.
    struct Clone {
        QImage image;
        QRectF placed;
        bool inGrid;
    };
    std::optional<Clone> clone;
    // Blur: the clone is the layer softened, in place.
    bool isBlur = false;
    // The clone replaces what is under the tip, clearing too.
    bool replacesWithClone = false;
    // The undo name when the stroke's kind says none.
    std::optional<QString> editName;
    static constexpr int tileSize = 256;
    // Soft-tip deposition rate, shared with the continuous integral.
    static double spacingFraction(double hardness);

    // A sample; the newest piece shows as a straight tail.
    void append(QPointF point);
    // Replaces the tail with the curve's last piece; repeatable.
    void flush();
    std::vector<BrushPatch> patches() const;
    const std::optional<QRectF> &dirtyDocumentRect() const { return m_dirtyDocumentRect; }
    // The selection, or the canvas, filled with one colour.
    void fill(const QColor &color);
    // Swift's fillGradient: across the canvas, linear or radial.
    void fillGradient(GradientShape shape, QPointF start, QPointF end, const std::array<QColor, 2> &colors, double opacity);
    // Pixels inside the selection made transparent, where pixels are.
    void clearPixels();
    // Cuts the selected pixels from the source; false with none.
    bool liftSelection();
    // The tiles anew: a hole, and the pixels `offset` away.
    void moveLifted(QSizeF offset, bool duplicate);
    // A placed mask as the stroke leaves it (LayerMask.cpp).
    std::optional<QImage> placedMaskPreview(const LayerTransform &placement) const;
    QRectF committedBounds() const;
    LayerTransform committedTransform() const;
    LayerTransform transform(const QRectF &bounds) const;
    // `rect` shifted to copy from `offset` document pixels away.
    QRectF gridRect(const QRectF &rect, QSizeF offset) const;
    // Spot Healing's end: the painted spot rebuilt from around it.
    void heal();
    PaintSnapshot paintSnapshot() const;
    BrushCommit::Input commitInput() const;

private:
    struct Tile {
        QRectF rect;
        QImage context;
        std::optional<QImage> image;
        QImage base;
    };
    struct Lifted {
        QImage image;
        QRectF rect;
    };
    // The grid the stroke paints in, made before the members.
    struct Grid {
        int width;
        int height;
        QRectF sourceRect;
        QTransform pixelToDocument;
        LayerTransform paintTransform;
    };
    static Grid grid(const ImageLayer &layer, bool mask, const BrushSettings &settings, const QRectF &canvas, bool growsMask);
    BrushStroke(const ImageLayer &layer, bool mask, const BrushSettings &settings, const QRectF &canvas, const Grid &grid);
    std::vector<BrushSegment> continuousCurve(QPointF start, QPointF end, QPointF before, QPointF after) const;
    std::set<qint64> continuousKeys(const std::vector<BrushSegment> &segments) const;
    void renderContinuous(const std::vector<BrushSegment> &settled, const std::vector<BrushSegment> &tail);
    void publish(const std::set<qint64> &changed);
    QImage selectionCoverage(const Tile &tile) const;
    QImage clonePixels(const Tile &tile) const;
    void allocateTile(qint64 key, qint64 x, qint64 y);
    // `draw` in document coordinates over every tile the canvas covers.
    void paintCanvas(bool withinSource, const std::function<void(QPainter &)> &draw);

    // The pixels the stroke starts from: mask's or layer's.
    const std::optional<ImportedImage> m_source;
    const QColor m_paintColor;
    std::optional<QRectF> m_allocatedBounds;
    std::vector<QPointF> m_samples;
    std::optional<QRectF> m_dirtyDocumentRect;
    // Tile keys, row × columns + column, pass int.
    std::map<qint64, Tile> m_tiles;
    std::map<qint64, BrushCoverage::Tile> m_coverageTiles;
    std::set<qint64> m_tailKeys;
    // Per-tile gray coverage; the tile is recomposed from it.
    std::map<qint64, QImage> m_coverage;
    // Selected pixels cut from the source, in layer pixels.
    std::optional<Lifted> m_lifted;
    std::set<qint64> m_moveTiles;
};
