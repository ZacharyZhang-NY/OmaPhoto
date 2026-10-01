#include "Document/ProjectWorkspace.h"
#include "IO/ImageFileDrop.h"
#include "Logging.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QTimer>
#include <algorithm>

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
    current().controller.resumeExternalChangeCheck();
    emit changed();
}

void ProjectWorkspace::moveTab(QUuid id, int index)
{
    const auto from = std::ranges::find_if(m_tabs, [id](const std::shared_ptr<ProjectTab> &tab) { return tab->id == id; });
    if (from == m_tabs.end())
        return;
    const auto target = std::clamp<std::ptrdiff_t>(index, 0, std::ptrdiff_t(m_tabs.size()) - 1);
    if (target == from - m_tabs.begin())
        return;
    const std::shared_ptr<ProjectTab> tab = *from;
    m_tabs.erase(from);
    m_tabs.insert(m_tabs.begin() + target, tab);
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

// Swift's finishTextEditing: open text is applied before asking.
bool ProjectWorkspace::finishTextEditing()
{
    for (const std::shared_ptr<ProjectTab> &each : quitOrder()) {
        if (each->session.textDraft() && !each->session.finishText())
            return false;
    }
    return true;
}

void ProjectWorkspace::confirmQuit(std::function<void(bool)> done)
{
    if (!finishTextEditing() || !canSwitch()) {
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
