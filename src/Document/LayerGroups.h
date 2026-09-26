#pragma once
#include "IO/ProjectStore.h"
#include <QSet>
#include <vector>

namespace LayerHierarchy {
struct Entry {
    ProjectLayerRecord layer;
    int depth;
    bool visible;
};

std::vector<Entry> entries(const std::vector<ProjectLayerRecord> &layers, bool topFirst = false,
                           const QSet<QUuid> &collapsed = {});
std::vector<ProjectLayerRecord> visibleLayers(const std::vector<ProjectLayerRecord> &layers);
void validate(const std::vector<ProjectLayerRecord> &layers);
}
