#pragma once
#include "Document/DocumentLimits.h"
#include "Document/ShapeTool.h"
#include <QByteArray>
#include <QImage>
#include <QPainterPath>
#include <QRectF>
#include <map>
#include <optional>

// Photoshop vector masks rasterized, or fills mapped onto live shapes.
namespace PSDVector {
struct Raster {
    QImage image;
    QRectF bounds;
};

struct Live {
    LayerShapeStyle style;
    QRectF bounds;
    QImage image;
    std::vector<QString> notes;
};

using Extra = std::map<QString, QByteArray>;

std::optional<Live> live(const Extra &extra, QSizeF canvas, qint64 remainingPixels = DocumentLimits::documentPixelBudget());
std::optional<Raster> raster(const Extra &extra, QSizeF canvas, qint64 remainingPixels = DocumentLimits::documentPixelBudget());
std::optional<QPainterPath> path(const QByteArray &data, QSizeF canvas);
struct RGB {
    double r;
    double g;
    double b;
};
std::optional<RGB> rgb(const QByteArray &data);
std::optional<bool> boolean(const QByteArray &data, const QByteArray &key);
std::optional<double> unit(const QByteArray &data, const QByteArray &key, qsizetype from = 0);
}
