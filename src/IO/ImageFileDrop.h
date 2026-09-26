#pragma once
#include <QMimeData>
#include <QPointF>
#include <QUuid>
#include <functional>
#include <memory>
#include <vector>
#include <optional>

class EditorSession;
class ProjectWorkspace;

// Swift's ImageFileDrop: dropped files and pictures, in order, imported.
namespace ImageFileDrop {
void importProviders(const QMimeData &data, EditorSession &session, std::optional<QPointF> point, ProjectWorkspace *workspace = nullptr,
                     std::optional<QUuid> destination = std::nullopt, std::function<void()> done = {});
// Swift's drop types: a file on disk or a picture.
bool holdsImages(const QMimeData &data);
// A drag as Swift's providers: a URL each, else whole.
std::vector<std::shared_ptr<QMimeData>> providers(const QMimeData &data);
}
