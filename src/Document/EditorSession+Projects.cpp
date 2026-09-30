#include "Document/EditorSession.h"
#include "Document/DocumentLimits.h"
#include "Logging.h"

std::optional<ProjectSnapshot> EditorSession::projectSnapshot() const
{
    if (!m_document)
        return std::nullopt;
    ProjectSnapshot snapshot{.manifest = {.resolution = m_document->resolution, .documentID = m_document->id, .width = m_document->width,
                                          .height = m_document->height, .activeLayerID = m_activeLayerID, .layers = {}},
                             .images = {}};
    // Growing would hold two record lists at once.
    snapshot.manifest.layers.reserve(m_document->layers.size());
    for (const ImageLayer &layer : m_document->layers) {
        if (layer.asset)
            snapshot.images.insert({layer.id, *layer.asset});
        if (layer.mask)
            snapshot.masks.insert({layer.id, layer.mask->asset});
        snapshot.manifest.layers.push_back(layer.hierarchyRecord());
    }
    // No guides leave the key out, as Swift writes nil.
    if (!m_document->guides.empty())
        snapshot.manifest.guides = m_document->guides;
    return snapshot;
}

void EditorSession::installProject(const ProjectSnapshot &snapshot, const QString &path)
{
    m_collapsedGroupIDs.clear();
    m_isMaskSelected = false;
    m_cropRect.reset();
    m_guideDrag = std::nullopt;
    m_transformEdit = std::nullopt;
    m_document = CanvasDocument(snapshot);
    // Install loads shapes and text; Swift's resize rebuild does not.
    for (size_t index = 0; index < m_document->layers.size(); ++index) {
        ImageLayer &layer = m_document->layers[index];
        layer.shape = LayerShape::loaded(snapshot.manifest.layers[index].shape, layer.asset);
        layer.text = LayerText::loaded(snapshot.manifest.layers[index].text, layer.asset);
    }
    setActiveLayerID(snapshot.manifest.activeLayerID);
    m_projectPath = path;
    m_renamingLayerID = std::nullopt;
    history.reset();
    viewport.fit(m_document->size());
    qCInfo(lcApp).noquote() << "installed a project of" << int(m_document->layers.size()) << "layers from" << path;
    resumeFileRequests();
    notify();
}

void EditorSession::reloadProject(const ProjectSnapshot &snapshot)
{
    if (!m_projectPath)
        return;
    const CanvasViewport kept = viewport;
    const QSet<QUuid> collapsed = m_collapsedGroupIDs;
    const std::optional<QUuid> active = m_activeLayerID;
    const QSet<QUuid> selected = m_selectedLayerIDs;
    installProject(snapshot, *m_projectPath);
    viewport = kept;
    QSet<QUuid> ids;
    for (const ProjectLayerRecord &record : snapshot.manifest.layers)
        ids.insert(record.id);
    m_collapsedGroupIDs = collapsed & ids;
    if (active && ids.contains(*active)) {
        setActiveLayerID(active);
        m_selectedLayerIDs = (selected & ids) | QSet<QUuid>{*active};
    }
    notify();
}

void EditorSession::setProjectPath(std::optional<QString> path)
{
    m_projectPath = std::move(path);
    notify();
}

void EditorSession::clearProject()
{
    m_collapsedGroupIDs.clear();
    m_isMaskSelected = false;
    m_cropRect.reset();
    m_transformEdit = std::nullopt;
    m_guideDrag = std::nullopt;
    m_document = std::nullopt;
    setActiveLayerID(std::nullopt);
    m_renamingLayerID = std::nullopt;
    m_projectPath = std::nullopt;
    history.reset();
    resumeFileRequests();
    notify();
}

void EditorSession::createNewProject(int width, int height)
{
    if (m_isProjectBusy || m_isImporting || width < 1 || width > DocumentLimits::maxSide || height < 1 || height > DocumentLimits::maxSide)
        return;
    clearProject();
    createDocument(width, height, true);
}
