#include "Document/ProjectWorkspace.h"
#include "Document/LiveLayerMask.h"
#include "IO/ImageExporter.h"
#include "IO/ImageFileDrop.h"
#include "Logging.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrent>

const QString ProjectWorkspace::layerType = QStringLiteral("com.compositor.layer-row");

ProjectTab::ProjectTab(QString name) : defaultName(std::move(name)), controller(session) {}

QString ProjectTab::nameWithoutSuffix(const QString &path)
{
    // As Swift's deletingPathExtension: `.comp` alone is all name.
    const QString name = QFileInfo(path).fileName();
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    return dot > 0 && dot < name.size() - 1 ? name.left(dot) : name;
}

QString ProjectTab::title() const
{
    return session.projectPath() ? nameWithoutSuffix(*session.projectPath()) : defaultName;
}

ProjectWorkspace::ProjectWorkspace()
{
    const auto first = std::make_shared<ProjectTab>(QStringLiteral("Untitled"));
    first->session.skipsInitialClipboardCanvasSize = true;
    first->controller.workspace = this;
    m_tabs = {first};
    m_selectedID = first->id;
}

std::shared_ptr<ProjectTab> ProjectWorkspace::tab(QUuid id) const
{
    for (const std::shared_ptr<ProjectTab> &each : m_tabs) {
        if (each->id == id)
            return each;
    }
    return nullptr;
}

ProjectTab &ProjectWorkspace::current() const
{
    const std::shared_ptr<ProjectTab> selected = tab(m_selectedID);
    return selected ? *selected : *m_tabs.front();
}

bool ProjectWorkspace::canSwitch() const
{
    return !m_isManaging && current().session.canStartProjectOperation() && !current().session.hueSaturation() && !current().session.filterEdit()
        && !current().session.gradientEdit() && !current().session.pixelMove() && !current().session.colorPicker();
}

void ProjectWorkspace::setManaging(bool managing)
{
    m_isManaging = managing;
    emit changed();
}

void ProjectWorkspace::finish(const std::function<void()> &done)
{
    // The caller goes on first, like a resumed Swift task.
    if (done)
        QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
}

void ProjectWorkspace::finish(const std::function<void(bool)> &done, bool value)
{
    if (done)
        QMetaObject::invokeMethod(this, [done, value] { done(value); }, Qt::QueuedConnection);
}

ProjectTab &ProjectWorkspace::addTab(bool reuseEmpty)
{
    if (reuseEmpty && m_tabs.size() == 1 && !current().session.document())
        return current();
    const auto added = std::make_shared<ProjectTab>(QStringLiteral("Untitled %1").arg(m_nextNumber));
    m_nextNumber += 1;
    added->controller.workspace = this;
    added->controller.window = window;
    m_tabs.push_back(added);
    m_selectedID = added->id;
    emit changed();
    return *added;
}

void ProjectWorkspace::select(QUuid id)
{
    if (id == m_selectedID || !canSwitch() || !tab(id))
        return;
    current().session.commitTransform();
    m_selectedID = id;
    current().controller.window = window;
    emit changed();
}

void ProjectWorkspace::newCanvas()
{
    if (!canSwitch())
        return;
    current().session.commitTransform();
    addTab(false);
}

void ProjectWorkspace::open(std::optional<QString> path, std::function<void(bool)> done)
{
    if (!canSwitch()) {
        finish(done, false);
        return;
    }
    setManaging(true);
    const auto end = [this, done](bool opened) {
        setManaging(false);
        finish(done, opened);
    };
    if (path) {
        loadProject(*path, end);
        return;
    }
    // A project is a folder here: one a dialog.
    auto *panel = new QFileDialog(window, QStringLiteral("Open Project"));
    panel->setAttribute(Qt::WA_DeleteOnClose);
    panel->setFileMode(QFileDialog::Directory);
    connect(panel, &QDialog::finished, this, [this, panel, end](int result) {
        if (result != QDialog::Accepted) {
            end(false);
            return;
        }
        loadProject(panel->selectedFiles().value(0), end);
    });
    panel->open();
}

void ProjectWorkspace::loadProject(const QString &path, std::function<void(bool)> then)
{
    for (const std::shared_ptr<ProjectTab> &existing : m_tabs) {
        if (ProjectController::samePlace(existing->session.projectPath(), path)) {
            m_selectedID = existing->id;
            emit changed();
            then(true);
            return;
        }
    }
    // Loaded unattached: a failed open leaves no broken tab.
    const auto loading = std::make_shared<ProjectTab>(ProjectTab::nameWithoutSuffix(path));
    loading->controller.window = window;
    // That tab's controller answers, perhaps after this is gone.
    loading->controller.open(path, [self = QPointer<ProjectWorkspace>(this), loading, then](bool opened) {
        if (!self)
            return;
        if (!opened) {
            then(false);
            return;
        }
        if (self->m_tabs.size() == 1 && !self->current().session.document())
            self->m_tabs.clear();
        loading->controller.workspace = self;
        self->m_tabs.push_back(loading);
        self->m_selectedID = loading->id;
        emit self->changed();
        then(true);
    });
}

void ProjectWorkspace::close(QUuid id, std::function<void()> done)
{
    const std::shared_ptr<ProjectTab> closing = tab(id);
    if (!canSwitch() || !closing) {
        finish(done);
        return;
    }
    setManaging(true);
    closing->controller.window = window;
    // Held here, the tab outlives its own controller's answer.
    closing->controller.confirmQuit([self = QPointer<ProjectWorkspace>(this), closing, done](bool confirmed) {
        if (!self)
            return;
        if (confirmed)
            self->removeTab(closing->id);
        self->setManaging(false);
        self->finish(done);
    });
}

void ProjectWorkspace::removeTab(QUuid id)
{
    size_t index = 0;
    while (index < m_tabs.size() && m_tabs[index]->id != id)
        index += 1;
    // Nothing erased is nothing changed: no iterator to trust.
    if (std::erase_if(m_tabs, [&](const std::shared_ptr<ProjectTab> &each) { return each->id == id; }) == 0)
        return;
    if (m_tabs.empty())
        addTab(false);
    else if (m_selectedID == id)
        m_selectedID = m_tabs[std::min(index, m_tabs.size() - 1)]->id;
    emit changed();
}

std::vector<std::shared_ptr<ProjectTab>> ProjectWorkspace::quitOrder() const
{
    // The project on screen first, then the rest in order.
    std::vector<std::shared_ptr<ProjectTab>> order{tab(current().id)};
    for (const std::shared_ptr<ProjectTab> &each : m_tabs) {
        if (each->id != current().id)
            order.push_back(each);
    }
    return order;
}

void ProjectWorkspace::confirmQuit(std::function<void(bool)> done)
{
    if (!canSwitch()) {
        finish(done, false);
        return;
    }
    setManaging(true);
    askNext(quitOrder(), 0, std::move(done));
}

void ProjectWorkspace::askNext(std::vector<std::shared_ptr<ProjectTab>> order, size_t index, std::function<void(bool)> done)
{
    if (index == order.size()) {
        setManaging(false);
        finish(done, true);
        return;
    }
    const std::shared_ptr<ProjectTab> asked = order[index];
    m_selectedID = asked->id;
    asked->controller.window = window;
    emit changed();
    asked->controller.confirmQuit([self = QPointer<ProjectWorkspace>(this), order, index, done](bool confirmed) {
        if (!self)
            return;
        if (confirmed) {
            self->askNext(order, index + 1, done);
            return;
        }
        self->setManaging(false);
        self->finish(done, false);
    });
}

void ProjectWorkspace::closeWindow(QWidget *closing)
{
    confirmQuit([this, closing = QPointer<QWidget>(closing)](bool confirmed) {
        if (!confirmed)
            return;
        m_tabs.clear();
        addTab(false);
        // The window's close asks us; this mark lets it through.
        if (closing) {
            closing->setProperty("closeConfirmed", true);
            closing->close();
        }
    });
}

void ProjectWorkspace::receive(const QList<QUrl> &urls, std::optional<QUuid> destination, std::optional<QPointF> point,
                               std::function<void()> done)
{
    // Swift sleeps 30 ms between looks; so does this.
    if (!canSwitch()) {
        QTimer::singleShot(30, this, [=, this] { receive(urls, destination, point, done); });
        return;
    }
    setManaging(true);
    receiveNext(urls, destination, point, std::move(done));
}

void ProjectWorkspace::receiveNext(QList<QUrl> urls, std::optional<QUuid> destination, std::optional<QPointF> point,
                                   std::function<void()> done)
{
    if (urls.isEmpty()) {
        setManaging(false);
        finish(done);
        return;
    }
    const QUrl url = urls.takeFirst();
    const auto next = [=, this] { receiveNext(urls, destination, point, done); };
    // A project opens its own tab; a picture joins one.
    if (ProjectController::isProject(url)) {
        loadProject(url.toLocalFile(), [next](bool) { next(); });
        return;
    }
    const std::shared_ptr<ProjectTab> into = destination ? tab(*destination) : tab(addTab().id);
    if (!into) {
        next();
        return;
    }
    m_selectedID = into->id;
    emit changed();
    // The callback keeps the tab alive, as Swift's await does.
    into->session.importImages({url}, point, [self = QPointer<ProjectWorkspace>(this), into, next] {
        if (self)
            next();
    });
}

void ProjectWorkspace::receiveProviders(const QMimeData &data, std::optional<QUuid> destination, std::optional<QPointF> point,
                                        std::function<void()> done)
{
    if (data.hasFormat(layerType))
        copyNext(layerIDs(data), destination, point, std::move(done));
    else
        importNext(ImageFileDrop::providers(data), destination, point, std::move(done));
}

// One provider after another, as Swift awaits each.
void ProjectWorkspace::importNext(std::vector<std::shared_ptr<QMimeData>> providers, std::optional<QUuid> destination, std::optional<QPointF> point,
                                  std::function<void()> done)
{
    if (providers.empty()) {
        finish(done);
        return;
    }
    const std::shared_ptr<QMimeData> provider = providers.front();
    providers.erase(providers.begin());
    // The front tab hears what failed; it stays alive.
    const std::shared_ptr<ProjectTab> front = tab(m_selectedID);
    ImageFileDrop::importProviders(*provider, front->session, point, this, destination,
                                   [self = QPointer<ProjectWorkspace>(this), front, providers = std::move(providers), destination, point, done]() mutable {
                                       if (self)
                                           self->importNext(std::move(providers), destination, point, std::move(done));
                                   });
}

std::optional<QUuid> ProjectWorkspace::layerID(const QString &text)
{
    const QString trimmed = text.trimmed();
    const QUuid id = QUuid::fromString(trimmed);
    if (uuidString(id).compare(trimmed, Qt::CaseInsensitive) != 0)
        return std::nullopt;
    return id;
}

std::vector<QUuid> ProjectWorkspace::layerIDs(const QMimeData &data)
{
    std::vector<QUuid> ids;
    for (const QString &line : QString::fromUtf8(data.data(layerType)).split(QLatin1Char('\n'))) {
        if (const std::optional<QUuid> id = layerID(line))
            ids.push_back(*id);
    }
    return ids;
}

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
    // A null pointer aborts when read; an iterator would not.
    std::shared_ptr<ProjectTab> from;
    for (const std::shared_ptr<ProjectTab> &each : m_tabs) {
        if (!from && each->session.document() && indexOf(each->session.document()->layers, id) >= 0)
            from = each;
    }
    if (!canSwitch() || !from || !from->session.canEditLayers() || destination == std::optional(from->id)) {
        finish(done);
        return;
    }
    Copy copy{.id = id, .from = from, .target = nullptr, .original = from->session.document().value(), .layers = {}, .included = {}, .point = point};
    const ProjectSnapshot snapshot = copy.from->session.projectSnapshot().value();
    copy.target = destination ? tab(*destination) : tab(addTab(false).id);
    if (!copy.target || (destination && !copy.target->session.canStartProjectOperation())
        || (copy.target->session.document() && !copy.target->session.canEditLayers())) {
        finish(done);
        return;
    }
    copy.included = copy.from->session.descendantIDs(id);
    copy.included.insert(id);
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
    const QPointF anchor = copy.layers[indexOf(copy.layers, copy.id)].transform.center();
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
        // Swift's copy lists its fields; effects are not among them.
        layer.effects = std::nullopt;
        layers.push_back(layer);
    }
    // Busy sessions refuse a new canvas: free it first.
    into.setIsProjectBusy(false);
    into.beginEdit(QStringLiteral("Copy Layers from Project"));
    if (!into.document())
        into.createDocument(int(size.width()), int(size.height()));
    into.m_document->layers.insert(into.m_document->layers.end(), layers.begin(), layers.end());
    into.setActiveLayerID(mapping.value(copy.id));
    into.endEdit();
    // Announced by the caller, which stops managing next.
    m_selectedID = copy.target->id;
}
