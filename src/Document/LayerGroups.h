#pragma once
#include "IO/ProjectStore.h"
#include <QSet>
#include <functional>
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

// A folder's opacity multiplies into each layer inside it.
namespace LayerOpacity {
// A layer's opacity and parent, or none when unknown.
using Node = std::function<std::optional<std::pair<double, std::optional<QUuid>>>(QUuid)>;
double effective(double own, std::optional<QUuid> parent, const Node &folder);
}
