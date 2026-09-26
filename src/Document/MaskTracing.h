#pragma once
#include <QImage>
#include <QPainterPath>
#include <optional>

// Raster coverage traced into an outline along exact pixel edges.
namespace MaskTracing {
std::optional<QPainterPath> darkPixels(const QImage &image);
std::optional<QPainterPath> opaquePixels(const QImage &image);
}
