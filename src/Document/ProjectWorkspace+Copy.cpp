#include "Document/ProjectWorkspace.h"
#include "Document/LiveLayerMask.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include <QFutureWatcher>
#include <QtConcurrent>

// Several rows are copied one after another, as Swift awaits.
void ProjectWorkspace::copyNext(std::vector<QUuid> ids, std::optional<QUuid> destination, std::optional<QPointF> point, std::function<void()> done)
{
    if (ids.empty()) {
        finish(done);
        return;
    }
    const QUuid first = ids.front();
    ids.erase(ids.begin());
    copyLayer(first, destination, point, [self = QPointer(this), ids = std::move(ids), destination, point, done = std::move(done)]() mutable {
        if (self)
            self->copyNext(std::move(ids), destination, point, std::move(done));
    });
}

std::optional<QUuid> ProjectWorkspace::draggedLayerSource(const QMimeData &data) const
{
    const std::vector<QUuid> ids = layerIDs(data);
    if (ids.empty())
        return std::nullopt;
    for (const std::shared_ptr<ProjectTab> &each : m_tabs) {
        if (each->session.document() && indexOf(each->session.document()->layers, ids.front()) >= 0)
            return each->id;
    }
    return std::nullopt;
}

bool ProjectWorkspace::canReceiveDrag(const QMimeData &data, std::optional<QUuid> destination) const
{
    const std::optional<QUuid> source = draggedLayerSource(data);
    return !destination || !source || *source != *destination;
}

void ProjectWorkspace::copyLayer(QUuid id, std::optional<QUuid> destination, std::optional<QPointF> point, std::function<void()> done)
{
    copyLayers({id}, destination, point, std::move(done));
}

// Several keep their places relative to each other.
void ProjectWorkspace::copyLayers(const std::vector<QUuid> &ids, std::optional<QUuid> destination, std::optional<QPointF> point,
                                  std::function<void()> done)
{
    // A null pointer aborts when read; an iterator would not.
    std::shared_ptr<ProjectTab> from;
    for (const std::shared_ptr<ProjectTab> &each : m_tabs) {
        if (!from && !ids.empty() && each->session.document() && indexOf(each->session.document()->layers, ids.front()) >= 0)
            from = each;
    }
    if (!canSwitch() || !from || !from->session.canEditLayers() || destination == std::optional(from->id)) {
        finish(done);
        return;
    }
    Copy copy{.ids = ids, .from = from, .target = nullptr, .original = from->session.document().value(), .layers = {}, .included = {}, .point = point};
    const ProjectSnapshot snapshot = copy.from->session.projectSnapshot().value();
    copy.target = destination ? tab(*destination) : tab(addTab(false).id);
    if (!copy.target || (destination && !copy.target->session.canStartProjectOperation())
        || (copy.target->session.document() && !copy.target->session.canEditLayers())) {
        finish(done);
        return;
    }
    for (const QUuid &id : ids) {
        copy.included.insert(id);
        copy.included.unite(copy.from->session.descendantIDs(id));
    }
    const auto pixels = [](const std::vector<ImageLayer> &layers) {
        qint64 total = 0;
        for (const ImageLayer &layer : layers)
            total += layer.asset ? qint64(layer.asset->size().width()) * layer.asset->size().height() : 0;
        return total;
    };
    for (const ImageLayer &layer : copy.original.layers) {
        if (copy.included.contains(layer.id))
            copy.layers.push_back(layer);
    }
    const qint64 used = copy.target->session.document() ? pixels(copy.target->session.document()->layers) : 0;
    if (used + pixels(copy.layers) > 100'000'000) {
        copy.target->session.setImportError(QStringLiteral("The copied layers exceed this project’s 100-megapixel limit."));
        finish(done);
        return;
    }
    setManaging(true);
    copy.from->session.setIsProjectBusy(true);
    copy.target->session.setIsProjectBusy(true);
    // A mask from outside the copy goes into the pixels.
    std::vector<QUuid> outside;
    for (const ImageLayer &layer : copy.layers) {
        if (layer.maskSourceID && !copy.included.contains(*layer.maskSourceID))
            outside.push_back(layer.id);
    }
    auto *watcher = new QFutureWatcher<Baked>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, copy, done] {
        watcher->deleteLater();
        const Baked baked = watcher->result();
        if (baked.failure) {
            qCWarning(lcApp).noquote() << "cannot copy the layers:" << *baked.failure;
            copy.target->session.setImportError(baked.failure);
        } else {
            place(copy, baked.images);
        }
        // Managing ends last: its signal sees both projects free.
        copy.from->session.setIsProjectBusy(false);
        copy.target->session.setIsProjectBusy(false);
        setManaging(false);
        finish(done);
    });
    watcher->setFuture(QtConcurrent::run([snapshot, outside]() -> Baked {
        Baked result;
        try {
            for (const QUuid layer : outside) {
                if (const std::optional<ImportedImage> image = LiveMaskBaker::bake(snapshot, layer))
                    result.images.insert({layer, *image});
            }
        } catch (const ExportError &error) {
            result.failure = QString::fromUtf8(error.what());
        }
        return result;
    }));
}

void ProjectWorkspace::place(const Copy &copy, const EditorSession::BakedImages &baked)
{
    QHash<QUuid, QUuid> mapping;
    for (const ImageLayer &layer : copy.layers)
        mapping.insert(layer.id, QUuid::createUuid());
    EditorSession &into = copy.target->session;
    const QSizeF size = into.document() ? into.document()->size() : copy.original.size();
    // One layer anchors on its centre; several, on their box.
    QRectF pictured;
    for (const ImageLayer &layer : copy.layers) {
        if (!layer.isGroup)
            pictured = pictured.isNull() ? QRectF(layer.transform.origin, layer.transform.size) : pictured.united(QRectF(layer.transform.origin, layer.transform.size));
    }
    const QPointF anchor = copy.ids.size() == 1 || pictured.isNull() ? copy.layers[indexOf(copy.layers, copy.ids.front())].transform.center() : pictured.center();
    const QPointF shift = copy.point.value_or(QPointF(size.width() / 2, size.height() / 2)) - anchor;
    const auto mapped = [&](const std::optional<QUuid> &old) {
        return old && mapping.contains(*old) ? std::optional(mapping.value(*old)) : std::nullopt;
    };
    std::vector<ImageLayer> layers;
    for (ImageLayer layer : copy.layers) {
        if (baked.contains(layer.id))
            layer.asset = baked.at(layer.id);
        layer.id = mapping.value(layer.id);
        layer.transform.origin += shift;
        if (layer.mask && layer.mask->placement)
            layer.mask->placement->origin += shift;
        // A parent or mask source outside the copy lets go.
        layer.parentID = mapped(layer.parentID);
        layer.maskSourceID = mapped(layer.maskSourceID);
        layers.push_back(layer);
    }
    // Busy sessions refuse a new canvas: free it first.
    into.setIsProjectBusy(false);
    into.beginEdit(QStringLiteral("Copy Layers from Project"));
    if (!into.document())
        into.createDocument(int(size.width()), int(size.height()));
    into.m_document->layers.insert(into.m_document->layers.end(), layers.begin(), layers.end());
    into.setActiveLayerID(mapping.value(copy.ids.front()));
    into.m_selectedLayerIDs.clear();
    // A layer the source tab lacks was not copied.
    for (const QUuid &id : copy.ids) {
        if (mapping.contains(id))
            into.m_selectedLayerIDs.insert(mapping.value(id));
    }
    into.endEdit();
    // Announced by the caller, which stops managing next.
    m_selectedID = copy.target->id;
}

// Ctrl+V with layers copied whole; false leaves Paste to pixels.
bool ProjectWorkspace::pasteCopiedLayer()
{
    std::shared_ptr<ProjectTab> source;
    for (const std::shared_ptr<ProjectTab> &each : m_tabs) {
        const std::optional<CopiedLayer> &copied = each->session.copiedLayer();
        if (!source && copied && EditorSession::clipboardHolds(copied->data))
            source = each;
    }
    if (!source || !source->session.document())
        return false;
    std::vector<QUuid> ids;
    for (const QUuid &id : source->session.copiedLayer()->ids) {
        if (indexOf(source->session.document()->layers, id) >= 0)
            ids.push_back(id);
    }
    if (ids.empty())
        return false;
    if (source->id == m_selectedID) {
        if (!source->session.canEditLayers())
            return false;
        source->session.duplicateLayers(ids, QStringLiteral("Paste"));
        return true;
    }
    copyLayers(ids, m_selectedID);
    return true;
}
