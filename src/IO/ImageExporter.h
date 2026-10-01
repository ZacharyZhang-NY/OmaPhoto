#pragma once
#include "IO/ProjectStore.h"
#include <QByteArray>
#include <QImage>
#include <functional>
#include <stdexcept>

class ExportError : public std::runtime_error {
public:
    enum class Kind { tooLarge, render, encode };
    explicit ExportError(Kind kind);
    const Kind kind;
};

// Swift's CancellationError: a newer request superseded this one.
class CancellationError : public std::exception {};

struct ExportRaster {
    QImage image;
    // Pixels per inch.
    double resolution = 72;
};

struct JPEGOptions {
    double quality = 0.85;
    // The matte behind transparent pixels.
    double red = 1;
    double green = 1;
    double blue = 1;
    bool operator==(const JPEGOptions &) const = default;
};

struct JPEGResult {
    QByteArray data;
    // Decoded from `data`, at most `previewLimit` pixels a side.
    QImage preview;
    static constexpr int previewLimit = 8192;
};

namespace ImageExporter {
ExportRaster render(const ProjectSnapshot &snapshot);
QByteArray pngData(const ProjectSnapshot &snapshot);
// Stops at Swift's three points once `cancelled` says so.
JPEGResult jpeg(const ExportRaster &raster, const JPEGOptions &options, const std::function<bool()> &cancelled = {});
void exportPNG(const ProjectSnapshot &snapshot, const QString &path);
// Replaces the file in one step, or not at all.
void write(const QByteArray &data, const QString &path);
}
