#pragma once
#include "Document/LayerTransform.h"
#include "IO/ImageImporter.h"
#include <QPainter>
#include <QUuid>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

// Layer-local coverage: white reveals, black hides, gray is soft.
struct LayerMask {
    ImportedImage asset;
    bool isEnabled = true;
    // Set once the mask moves apart from its layer.
    std::optional<LayerTransform> placement;
    bool isLinked = true;

    explicit LayerMask(ImportedImage asset, bool isEnabled = true,
                       std::optional<LayerTransform> placement = std::nullopt, bool isLinked = true);

    std::optional<QImage> enabledImage() const;
    friend bool operator==(const LayerMask &lhs, const LayerMask &rhs);
    LayerMask replacing(ImportedImage asset) const;
    static bool isValid(const QImage &image);
    static LayerMask solid(bool revealing);
    static ImportedImage assetFrom(const QImage &image);

    std::optional<LayerTransform> placementMovingLayer(const LayerTransform &old, const LayerTransform &updated) const;
    static double background(const QImage &thumbnail);
    static QImage placed(int width, int height, const LayerTransform &layer, const LayerTransform &placement,
                         int maskWidth, int maskHeight, double background,
                         const std::function<void(QPainter &)> &compose);
    static void drawSmooth(const QImage &image, const QRectF &rect, QPainter &context);
    // The mask over a layer's pixel grid; nil while disabled.
    std::optional<QImage> clipImage(const std::optional<LayerTransform> &placement, const LayerTransform &layer,
                                    int width, int height, std::optional<double> limit = std::nullopt) const;
};

// The canvas's last preview of an unlinked mask distorted alone.
struct MaskDistortPreviewCache {
    Corners corners;
    LayerTransform draft;
    ImageIdentity mask;
    LayerTransform layer;
    std::optional<QImage> result;
};

// Masks resampled into their layers' grids, least recently used dropped.
class MaskPlacementCache {
public:
    static MaskPlacementCache &shared();
    std::optional<QImage> image(const QImage &mask, const LayerTransform &placement, const LayerTransform &layer,
                                int width, int height, const std::function<std::optional<QImage>()> &build);

private:
    struct Entry {
        QImage mask;
        LayerTransform placement;
        LayerTransform layer;
        int width;
        int height;
        QImage image;
        quint64 lastUse;
    };
    std::vector<Entry> m_entries;
    quint64 m_clock = 0;
    std::mutex m_lock;
};

// A folder's mask, multiplied into every layer inside the folder.
struct FolderMaskClip {
    QImage image;
    LayerTransform transform;

    using Applier = std::function<void(const QPainter &context, QImage &coverage)>;

    // Multiplies the mask, placed like a layer's own, into coverage.
    void apply(QPointF center, const QPainter &context, QImage &coverage, double scale = 1) const;
    // Draws ids in order, each through its folders' masks.
    static void draw(const std::vector<QUuid> &ids, const std::function<std::optional<QUuid>(QUuid)> &parent,
                     const std::function<std::optional<Applier>(QUuid)> &clip, const QPainter &context,
                     const std::function<void(QUuid, const QImage &clip)> &drawLayer);
};
