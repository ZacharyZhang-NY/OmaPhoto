#pragma once
#include "IO/PSD/PSDTypes.h"
#include <map>

struct PSDImport {
    int width;
    int height;
    double resolution;
    std::vector<ImageLayer> layers;
    std::vector<PSDConversion> conversions;
};

// A read Photoshop file as layers, with its conversions.
namespace PSDDocumentBuilder {
// Each pixel record's asset and thumbnail, off the UI thread.
std::map<QUuid, ImportedImage> assets(const PSDDocument &document);
PSDImport makeImport(const PSDDocument &document, const std::map<QUuid, ImportedImage> &assets = {});
// The stored patch on the layer's grid; none without memory.
std::optional<QImage> maskOnLayerGrid(const QImage &patch, const PSDRecord &record, const ImageLayer &layer, QSizeF canvas);
}
