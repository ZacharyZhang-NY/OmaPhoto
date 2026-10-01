#include "Document/EditorSession.h"
#include "Document/LayerGroups.h"
#include <map>
#include <mutex>
#include <stdexcept>

namespace {
using Children = std::map<std::optional<QUuid>, std::vector<const LayerOrder::Node *>>;

void visit(const Children &children, const std::optional<QUuid> &parent, int depth, bool shown, LayerOrder::Result &result)
{
    if (depth > 64)
        return;
    const auto found = children.find(parent);
    if (found == children.end())
        return;
    for (const LayerOrder::Node *node : found->second) {
        const bool effective = shown && node->isVisible;
        result.order.push_back(node->id);
        if (effective) {
            result.visible.insert(node->id);
            if (!node->isGroup)
                result.drawn.push_back(node->id);
        }
        if (node->isGroup)
            visit(children, node->id, depth + 1, effective, result);
    }
}

// The last nodes asked about and their answer.
struct Known {
    std::mutex lock;
    std::optional<std::pair<std::vector<LayerOrder::Node>, LayerOrder::Result>> last;
};

Known &known()
{
    static Known value;
    return value;
}
}

LayerOrder::Result LayerOrder::resolve(const std::vector<ImageLayer> &layers)
{
    std::vector<Node> nodes;
    nodes.reserve(layers.size());
    for (const ImageLayer &layer : layers)
        nodes.push_back(Node{layer.id, layer.parentID, layer.isGroup, layer.isVisible});
    Known &cache = known();
    {
        const std::lock_guard<std::mutex> held(cache.lock);
        if (cache.last && cache.last->first == nodes)
            return cache.last->second;
    }
    Children children;
    for (const Node &node : nodes)
        children[node.parentID].push_back(&node);
    Result result;
    visit(children, std::nullopt, 0, true, result);
    const std::lock_guard<std::mutex> held(cache.lock);
    cache.last.emplace(std::move(nodes), result);
    return result;
}

LayerOrder::Result CanvasDocument::hierarchy() const
{
    return LayerOrder::resolve(layers);
}

QSet<QUuid> CanvasDocument::effectiveVisibleIDs() const
{
    return hierarchy().visible;
}

QHash<QUuid, double> CanvasDocument::effectiveOpacities() const
{
    // Each layer's opacity and folder, no whole layers copied.
    QHash<QUuid, std::pair<double, std::optional<QUuid>>> folders;
    for (const ImageLayer &layer : layers) {
        if (folders.contains(layer.id))
            throw std::logic_error("two layers share an id");
        folders.insert(layer.id, {layer.opacity, layer.parentID});
    }
    QHash<QUuid, double> result;
    for (auto entry = folders.cbegin(); entry != folders.cend(); ++entry) {
        result.insert(entry.key(), LayerOpacity::effective(entry->first, entry->second, [&folders](QUuid id) {
                          return folders.contains(id) ? std::optional(folders.value(id)) : std::nullopt;
                      }));
    }
    return result;
}

std::vector<ImageLayer> CanvasDocument::renderLayers() const
{
    const std::vector<QUuid> order = hierarchy().drawn;
    if (order.empty())
        return {};
    std::map<QUuid, size_t> index;
    for (size_t place = 0; place < layers.size(); ++place) {
        if (!index.emplace(layers[place].id, place).second)
            throw std::logic_error("two layers share an id");
    }
    std::vector<ImageLayer> result;
    result.reserve(order.size());
    for (const QUuid id : order)
        result.push_back(layers[index.at(id)]);
    return result;
}
