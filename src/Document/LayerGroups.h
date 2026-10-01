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

struct ImageLayer;

// Swift's LayerOrder: the hierarchy from ids, folders and visibility alone.
namespace LayerOrder {
struct Node {
    QUuid id;
    std::optional<QUuid> parentID;
    bool isGroup;
    bool isVisible;
    friend bool operator==(const Node &, const Node &) = default;
};
struct Result {
    // Every layer and folder, as LayerHierarchy::entries lists them.
    std::vector<QUuid> order;
    // Visible, in folders that are.
    QSet<QUuid> visible;
    // The layers that show, folders left out, bottom to top.
    std::vector<QUuid> drawn;
};
// Kept until a node changes: the canvas asks each event.
Result resolve(const std::vector<ImageLayer> &layers);
}
