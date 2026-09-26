#include "Document/EditorSession.h"
#include "Logging.h"
#include <QtConcurrent>

bool EditorSession::canStartProjectOperation() const
{
    return !m_textDraft && !m_isProjectBusy && !m_isImporting && !m_brushStroke && !m_warpStroke && !m_showsNewDocument && !m_showsImporter
        && !m_renamingLayerID && !m_importError && !m_levels && !m_adjustmentEditingID;
}

void EditorSession::waitForFileRequest(std::function<void()> ready)
{
    if (canStartProjectOperation())
        ready();
    else
        m_fileRequestWaiters.push_back(std::move(ready));
}

void EditorSession::resumeFileRequests()
{
    if (!canStartProjectOperation())
        return;
    // Each waiter looks again when its turn comes.
    for (const std::function<void()> &waiter : std::exchange(m_fileRequestWaiters, {}))
        QMetaObject::invokeMethod(this, [this, waiter] { waitForFileRequest(waiter); }, Qt::QueuedConnection);
}

void EditorSession::waitForProjectAccess(std::function<void()> ready)
{
    if (!m_isProjectBusy)
        ready();
    else
        m_projectWaiters.push_back(std::move(ready));
}

void EditorSession::updateBusyIndicator()
{
    // Quick operations never dim the controls.
    if (m_isProjectBusy) {
        if (!m_busyTimer.isActive() && !m_showsBusy)
            m_busyTimer.start();
    } else {
        m_busyTimer.stop();
        m_showsBusy = false;
    }
}

void EditorSession::importImages(const QList<QUrl> &urls, std::optional<QPointF> point, std::function<void()> done)
{
    if (urls.isEmpty()) {
        if (done)
            done();
        return;
    }
    if (m_brushStroke)
        finishBrush();
    cancelCrop();
    commitTransform();
    waitForProjectAccess([this, urls, point, done] {
        m_pendingImports.push_back({urls, point, done});
        if (!m_isImporting) {
            setIsImporting(true);
            drainImports();
        }
    });
}

void EditorSession::drainImports()
{
    if (m_pendingImports.empty()) {
        setIsImporting(false);
        if (!m_importFailures.isEmpty())
            setImportError(std::exchange(m_importFailures, {}).join("\n\n"));
        return;
    }
    beginEdit(QStringLiteral("Import Images"));
    // Without a canvas the first image makes it.
    m_importPoint = m_document ? m_pendingImports.front().point : std::nullopt;
    m_importIndex = 0;
    decodeNext();
}

void EditorSession::decodeNext()
{
    const ImportRequest &request = m_pendingImports.front();
    if (m_importIndex == request.urls.size()) {
        endEdit();
        qCInfo(lcApp) << "imported a request of" << request.urls.size() << "files";
        // The caller goes on once the queue's state is settled.
        if (request.done)
            QMetaObject::invokeMethod(this, request.done, Qt::QueuedConnection);
        m_pendingImports.pop_front();
        drainImports();
        return;
    }
    qint64 usedPixels = 0;
    if (m_document) {
        for (const ImageLayer &layer : m_document->layers) {
            if (layer.asset)
                usedPixels += qint64(layer.asset->size().width()) * layer.asset->size().height();
        }
    }
    const QUrl url = request.urls[m_importIndex];
    m_decoder.setFuture(QtConcurrent::run([url, remaining = 100'000'000 - usedPixels]() -> Decoded {
        try {
            if (!url.isLocalFile())
                throw ImageImportError(ImageImportError::Kind::unsupported);
            return {ImageImporter::decode(url.toLocalFile(), remaining), QString()};
        } catch (const ImageImportError &error) {
            return {std::nullopt, url.fileName() + ": " + error.what()};
        }
    }));
}

void EditorSession::finishDecode()
{
    const Decoded decoded = m_decoder.result();
    if (decoded.asset)
        insert(*decoded.asset, m_importPoint);
    else
        m_importFailures << decoded.failure;
    m_importIndex += 1;
    decodeNext();
}
