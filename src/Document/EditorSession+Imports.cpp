#include "Document/EditorSession.h"
#include "Document/DocumentLimits.h"
#include "Document/ProjectWorkspace.h"
#include "Document/PixelAdjust.h"
#include "IO/PSD/PSDReader.h"
#include "IO/RawImporter.h"
#include "Logging.h"
#include <QtConcurrent>

bool EditorSession::canStartProjectOperation() const
{
    return !m_selectionAmountOperation && !m_textDraft && !m_isProjectBusy && !m_isImporting && !m_brushStroke && !m_warpStroke && !m_showsNewDocument
        && !m_showsImporter && !m_renamingLayerID && !m_importError && !m_levels && !m_adjustmentEditingID;
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
    const QList<QUrl> &urls = m_pendingImports.front().urls;
    const bool photoshop = std::all_of(urls.begin(), urls.end(), [](const QUrl &url) { return url.isLocalFile() && PSDReader::matches(url.toLocalFile()); });
    beginEdit(photoshop ? QStringLiteral("Import Photoshop File") : QStringLiteral("Import Images"));
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
    const QUrl url = request.urls[m_importIndex];
    if (url.isLocalFile() && RawImporter::matches(url.toLocalFile())) {
        decodeRaw(url.toLocalFile(), remainingPixels());
        return;
    }
    if (url.isLocalFile() && PSDReader::matches(url.toLocalFile())) {
        beginPSDReading(QStringLiteral("Open “%1”?").arg(url.fileName()), QStringLiteral("Import"));
        m_photoshopReader.setFuture(QtConcurrent::run([path = url.toLocalFile(), remaining = remainingPixels()]() -> PhotoshopRead {
            try {
                PSDDocument document = ImageImporter::loadPhotoshop(path, remaining);
                // No layer records: Photoshop wrote the merged image alone.
                if (document.layers.empty())
                    return {std::nullopt, {}, QString(), true};
                std::map<QUuid, ImportedImage> assets = ImageImporter::photoshopAssets(document);
                return {std::move(document), std::move(assets), QString()};
            } catch (const ImageImportError &error) {
                return {std::nullopt, {}, QString::fromUtf8(error.what())};
            } catch (const PSDError &error) {
                return {std::nullopt, {}, QString::fromUtf8(error.what())};
            } catch (const ExportError &error) {
                return {std::nullopt, {}, QString::fromUtf8(error.what())};
            }
        }));
        return;
    }
    decode(url, false);
}

qint64 EditorSession::remainingPixels() const
{
    qint64 usedPixels = 0;
    if (m_document) {
        for (const ImageLayer &layer : m_document->layers) {
            if (layer.asset)
                usedPixels += qint64(layer.asset->size().width()) * layer.asset->size().height();
        }
    }
    return DocumentLimits::documentPixelBudget() - usedPixels;
}

// `flattened`: a layerless Photoshop file, read as its merged image.
void EditorSession::decode(const QUrl &url, bool flattened)
{
    m_decoder.setFuture(QtConcurrent::run([url, flattened, remaining = remainingPixels()]() -> Decoded {
        try {
            if (!url.isLocalFile())
                throw ImageImportError(ImageImportError::Kind::unsupported);
            return {ImageImporter::decode(url.toLocalFile(), remaining, flattened), QString()};
        } catch (const ImageImportError &error) {
            return {std::nullopt, url.fileName() + ": " + error.what()};
        } catch (const PSDError &error) {
            return {std::nullopt, url.fileName() + ": " + error.what()};
        } catch (const ExportError &error) {
            return {std::nullopt, url.fileName() + ": " + error.what()};
        }
    }));
}

void EditorSession::decodeRaw(const QString &path, qint64 remaining)
{
    const QString name = QFileInfo(path).fileName();
    const std::optional<QSize> size = RawImporter::pixelSize(path);
    const auto refuse = [&](ImageImportError::Kind kind) {
        m_importFailures << name + ": " + ImageImportError(kind).what();
        m_importIndex += 1;
        decodeNext();
    };
    if (!size)
        return refuse(ImageImportError::Kind::unreadable);
    if (size->width() > DocumentLimits::maxSide || size->height() > DocumentLimits::maxSide || qint64(size->width()) * size->height() > remaining)
        return refuse(ImageImportError::Kind::tooLarge);
    developRaw(path, [this, path, name](std::optional<RawDevelopSettings> settings) {
        if (!settings) {
            m_importIndex += 1;
            decodeNext();
            return;
        }
        // Seconds of work: a worker, so the window keeps drawing.
        m_decoder.setFuture(QtConcurrent::run([path, name, settings = *settings]() -> Decoded {
            try {
                const QImage developed = RawImporter::Queue::shared().develop(path, settings, std::nullopt);
                return {ImportedImage(developed, PixelAdjust::thumbnail(developed), ProjectTab::nameWithoutSuffix(path)), QString()};
            } catch (const ImageImportError &error) {
                return {std::nullopt, name + ": " + error.what()};
            } catch (const ExportError &error) {
                return {std::nullopt, name + ": " + error.what()};
            }
        }));
    });
}

void EditorSession::developRaw(const QString &path, std::function<void(std::optional<RawDevelopSettings>)> answer)
{
    m_rawAnswer = std::move(answer);
    m_rawDevelop = RawDevelopRequest{path, RawImporter::asShot(path).value_or(RawDevelopSettings())};
    notify();
}

void EditorSession::finishRawDevelop(std::optional<RawDevelopSettings> settings)
{
    m_rawDevelop = std::nullopt;
    notify();
    // Frees the preview's decode on a worker, behind any preview.
    (void)QtConcurrent::run([] { RawImporter::Queue::shared().release(); });
    // The import resumes from the event loop, as a task.
    if (const auto answer = std::exchange(m_rawAnswer, {}))
        QMetaObject::invokeMethod(this, [answer, settings] { answer(settings); }, Qt::QueuedConnection);
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

void EditorSession::finishPhotoshopRead()
{
    const PhotoshopRead read = m_photoshopReader.result();
    const QUrl url = m_pendingImports.front().urls[m_importIndex];
    const auto next = [this] {
        m_importIndex += 1;
        decodeNext();
    };
    // Swift ends the reading first, then reads the merged image.
    if (read.layerless) {
        endPSDReading();
        decode(url, true);
        return;
    }
    // Every image has its asset: making layers cannot fail.
    const std::optional<PSDImport> imported = read.document ? std::optional(PSDDocumentBuilder::makeImport(*read.document, read.assets)) : std::nullopt;
    if (!imported) {
        endPSDReading();
        m_importFailures << url.fileName() + ": " + read.failure;
        next();
        return;
    }
    const std::vector<PSDConversion> conversions = imported->conversions;
    finishPSDReading(conversions, [this, imported = std::move(*imported), url, next](bool confirmed) {
        if (confirmed) {
            try {
                insertPhotoshop(imported, ProjectTab::nameWithoutSuffix(url.toLocalFile()), m_importPoint);
            } catch (const std::runtime_error &error) {
                m_importFailures << url.fileName() + ": " + QString::fromUtf8(error.what());
            }
        }
        next();
    });
}

void EditorSession::beginPSDReading(const QString &title, const QString &confirmTitle)
{
    m_conversionCancelled = false;
    m_conversionRequest = PSDConversionRequest{title, confirmTitle, {}, true};
    m_showsConversionSheet = true;
    notify();
}

void EditorSession::finishPSDReading(const std::vector<PSDConversion> &conversions, std::function<void(bool)> answer)
{
    if (m_conversionCancelled || conversions.empty()) {
        const bool confirmed = !m_conversionCancelled;
        endPSDReading();
        answer(confirmed);
        return;
    }
    // The sheet waits for Cancel or the confirm button.
    m_conversionAnswer = std::move(answer);
    m_conversionRequest->conversions = conversions;
    m_conversionRequest->isReading = false;
    notify();
}

void EditorSession::endPSDReading()
{
    if (m_conversionAnswer)
        return;
    m_showsConversionSheet = false;
    m_conversionRequest = std::nullopt;
    notify();
}

void EditorSession::finishConversion(bool confirmed)
{
    if (!confirmed && m_conversionRequest && m_conversionRequest->isReading)
        m_conversionCancelled = true;
    m_showsConversionSheet = false;
    m_conversionRequest = std::nullopt;
    notify();
    // The import resumes from the event loop, as a task.
    if (const std::function<void(bool)> answer = std::exchange(m_conversionAnswer, {}))
        QMetaObject::invokeMethod(this, [answer, confirmed] { answer(confirmed); }, Qt::QueuedConnection);
}

void EditorSession::insertPhotoshop(const PSDImport &imported, const QString &named, std::optional<QPointF> centeredAt)
{
    std::vector<ImageLayer> incoming = imported.layers;
    const size_t existing = m_document ? m_document->layers.size() : 0;
    if (existing + incoming.size() + (m_document ? 1 : 0) > 10'000)
        throw ImageImportError(ImageImportError::Kind::tooLarge);
    std::vector<ImageLayer> layers = m_document ? m_document->layers : std::vector<ImageLayer>();
    std::optional<ImageLayer> group;
    if (m_document) {
        // An open canvas takes the file as one folder.
        group = ImageLayer(named, m_document->size());
        group->isGroup = true;
        const std::optional<ImageLayer> active = activeLayer();
        group->parentID = active && active->isGroup ? m_activeLayerID : active ? active->parentID : std::nullopt;
        std::optional<QRectF> box;
        for (const ImageLayer &layer : incoming)
            if (!layer.isGroup)
                box = box ? box->united(QRectF(layer.origin(), layer.size())) : QRectF(layer.origin(), layer.size());
        if (centeredAt && box) {
            const QPointF shift = *centeredAt - box->center();
            for (ImageLayer &layer : incoming)
                layer.transform.origin += shift;
        }
        for (ImageLayer &layer : incoming)
            if (!layer.parentID)
                layer.parentID = group->id;
        layers.push_back(*group);
    }
    layers.insert(layers.end(), incoming.begin(), incoming.end());
    // Folders nested past Swift's limit refuse the file.
    std::vector<ProjectLayerRecord> records;
    for (const ImageLayer &layer : layers)
        records.push_back(layer.hierarchyRecord());
    try {
        LayerHierarchy::validate(records);
    } catch (const ProjectError &) {
        qCWarning(lcIO) << "a Photoshop file nests its folders too deep";
        throw PSDError(PSDError::Kind::truncated);
    }
    beginEdit(QStringLiteral("Import Photoshop File"));
    if (!group) {
        CanvasDocument document(imported.width, imported.height);
        document.layers = std::move(layers);
        document.resolution = imported.resolution;
        m_document = std::move(document);
        viewport.fit(m_document->size());
        // The topmost root layer, else the topmost layer.
        std::optional<QUuid> active = incoming.empty() ? std::nullopt : std::optional(incoming.back().id);
        for (size_t index = incoming.size(); index-- > 0;) {
            if (!incoming[index].parentID) {
                active = incoming[index].id;
                break;
            }
        }
        setActiveLayerID(active);
    } else {
        m_document->layers = std::move(layers);
        if (group->parentID)
            m_collapsedGroupIDs.remove(*group->parentID);
        setActiveLayerID(group->id);
    }
    endEdit();
    qCInfo(lcIO) << "imported a Photoshop file of" << incoming.size() << "layers";
}
