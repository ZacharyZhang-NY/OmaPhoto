#include "Rendering/EffectsPreviewCache.h"
#include "Document/BrushStroke.h"
#include "Document/LayerEffects+Renderer.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include <QTimer>
#include <cmath>
#include <set>

EffectsPreviewCache::EffectsPreviewCache(QObject *parent) : QObject(parent)
{
    m_worker.setMaxThreadCount(1);
}

EffectsPreviewCache::~EffectsPreviewCache()
{
    // Queued renders go; the pool waits for the one running.
    m_worker.clear();
}

bool EffectsPreviewCache::Request::matches(const Request &other) const
{
    // A move only moves the image; a placed mask resamples.
    const bool sameMaskGeometry = (!maskSource && !other.maskSource) || (!placement && !other.placement)
        || (placement == other.placement && transform == other.transform);
    return image.identity() == other.image.identity() && maskSource == other.maskSource && sameMaskGeometry && effects == other.effects
        && sideLimit == other.sideLimit;
}

void EffectsPreviewCache::seed(QUuid id, const QImage &image, const LayerTransform &placement)
{
    // A render under way is for the pixels this replaces.
    cancel(id);
    m_seeds.insert_or_assign(id, Result{image, 0, placement});
}

std::optional<EffectsPreviewCache::Result> EffectsPreviewCache::rendered(QUuid id) const
{
    if (m_entries.contains(id) && m_entries.at(id).result)
        return m_entries.at(id).result;
    return m_seeds.contains(id) ? std::optional(m_seeds.at(id)) : std::nullopt;
}

void EffectsPreviewCache::prepare(const std::vector<ImageLayer> &layers)
{
    std::set<QUuid> ids;
    for (const ImageLayer &layer : layers) {
        if (layer.effects && !layer.effects->visible().isEmpty())
            ids.insert(layer.id);
    }
    for (auto entry = m_entries.begin(); entry != m_entries.end();) {
        if (ids.contains(entry->first)) {
            ++entry;
            continue;
        }
        entry->second.request.cancelled->store(true);
        entry = m_entries.erase(entry);
    }
    std::erase_if(m_seeds, [&](const auto &seed) { return !ids.contains(seed.first); });
    // About 64 MiB of output shared by every effect layer.
    m_sideLimit = std::min(1536, std::max(32, int(std::sqrt(16'777'216.0 / double(std::max<size_t>(1, ids.size()))))));
}

std::optional<EffectsPreviewCache::Result> EffectsPreviewCache::preview(const ImageLayer &layer, const std::optional<QImage> &mask,
                                                                         const LayerTransform &transform,
                                                                         const std::optional<LayerTransform> &maskPlacement,
                                                                         const std::function<void()> &completion)
{
    const LayerEffects effects = layer.effects.value_or(LayerEffects()).visible();
    if (!layer.asset || effects.isEmpty() || !effects.isValid()) {
        cancel(layer.id);
        return std::nullopt;
    }
    const std::optional<ImageIdentity> maskSource = layer.mask && layer.mask->isEnabled ? std::optional(layer.mask->asset.identity()) : std::nullopt;
    const Request request{QUuid::createUuid(), *layer.asset, mask, maskSource, maskPlacement, transform, effects, m_sideLimit,
                          std::make_shared<std::atomic<bool>>(false)};
    if (m_entries.contains(layer.id) && m_entries.at(layer.id).request.matches(request))
        return m_entries.at(layer.id).result;
    std::optional<Result> previous;
    if (m_entries.contains(layer.id)) {
        const Entry &old = m_entries.at(layer.id);
        old.request.cancelled->store(true);
        // Kept while the settings change on the same pixels.
        if (old.request.image.identity() == request.image.identity() && old.request.maskSource == maskSource
            && old.request.effects.kinds() == effects.kinds())
            previous = old.result;
    }
    if (!previous && m_seeds.contains(layer.id))
        previous = m_seeds.at(layer.id);
    m_entries.insert_or_assign(layer.id, Entry{request, previous});
    const QUuid layerID = layer.id;
    QTimer::singleShot(60, this, [this, request, layerID, completion] {
        m_worker.start([this, request, layerID, completion] {
            if (request.cancelled->load())
                return;
            const std::optional<Result> result = render(request);
            if (request.cancelled->load())
                return;
            QMetaObject::invokeMethod(this, [this, layerID, id = request.id, result, completion] { land(layerID, id, result, completion); },
                                      Qt::QueuedConnection);
        });
    });
    return previous;
}

void EffectsPreviewCache::land(QUuid layerID, QUuid requestID, const std::optional<Result> &result, const std::function<void()> &completion)
{
    if (!m_entries.contains(layerID) || m_entries.at(layerID).request.id != requestID)
        return;
    m_entries.at(layerID).result = result;
    if (result)
        m_seeds.erase(layerID);
    completion();
}

void EffectsPreviewCache::cancel(QUuid id)
{
    if (!m_entries.contains(id))
        return;
    m_entries.at(id).request.cancelled->store(true);
    m_entries.erase(id);
}

std::optional<EffectsPreviewCache::Result> EffectsPreviewCache::render(const Request &request)
{
    try {
        // A painted layer flattens here, off the UI thread.
        const QImage image = request.image.image();
        const double margin = LayerEffectsRenderer::margin(request.effects);
        // The margins count too; even a 500px stroke stays bounded.
        const double factor = std::min(1.0, double(request.sideLimit - 8) / (double(std::max(image.width(), image.height())) + 2 * margin));
        const int width = std::max(1, int(std::round(image.width() * factor)));
        const int height = std::max(1, int(std::round(image.height() * factor)));
        // Swift's high quality: each axis filtered by its own measure.
        const auto resized = [&](const QImage &source, bool mask) {
            // A mask travels as alpha, so its values pass exactly.
            const QImage scaled = (mask ? BrushRaster::alphaView(source) : source)
                                      .scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                      .convertToFormat(mask ? QImage::Format_Alpha8 : QImage::Format_RGBA8888_Premultiplied);
            const QImage result = mask ? QImage(scaled.constBits(), width, height, scaled.bytesPerLine(), QImage::Format_Grayscale8).copy() : scaled;
            if (result.isNull())
                throw ExportError(ExportError::Kind::render);
            return result;
        };
        const QImage pixels = factor == 1 ? image : resized(image, false);
        const std::optional<QImage> mask = request.mask ? std::optional(factor == 1 ? *request.mask : resized(*request.mask, true)) : std::nullopt;
        LayerEffects effects = request.effects;
        if (effects.stroke)
            effects.stroke->size *= factor;
        if (effects.shadow) {
            effects.shadow->distance *= factor;
            effects.shadow->blur *= factor;
        }
        // Swift forgets the inner shadow, which then shows too wide.
        if (effects.innerShadow) {
            effects.innerShadow->distance *= factor;
            effects.innerShadow->blur *= factor;
        }
        const LayerEffectsRenderer::Rendered rendered = LayerEffectsRenderer::render(pixels, mask, effects);
        return Result{rendered.image, rendered.inset, std::nullopt};
    } catch (const ExportError &error) {
        qCWarning(lcRendering) << "an effects preview could not be made:" << error.what();
    }
    return std::nullopt;
}
