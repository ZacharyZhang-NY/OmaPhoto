#include "Document/LayerEffects+Renderer.h"
#include "Document/BrushStroke.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include "Rendering/LayerEffectsKernel.h"
#include <QMutex>
#include <QPainter>
#include <cmath>
#include <vector>

// Swift's LayerEffectsRenderer, on the kernel alone.
namespace {
struct Entry {
    QImage image;
    std::optional<QImage> mask;
    LayerEffects effects;
    LayerEffectsRenderer::Rendered result;
};

qint64 bytes(const Entry &entry)
{
    return entry.result.image.sizeInBytes() + entry.image.sizeInBytes() + (entry.mask ? entry.mask->sizeInBytes() : 0);
}

// Swift's `===`: the same pixels, not equal ones.
bool same(const std::optional<QImage> &lhs, const std::optional<QImage> &rhs)
{
    return lhs.has_value() == rhs.has_value() && (!lhs || lhs->cacheKey() == rhs->cacheKey());
}

QMutex lock;
std::vector<Entry> entries;

// The layer's pixels through its mask, as they show.
QImage masked(const QImage &image, const std::optional<QImage> &mask)
{
    if (!mask)
        return image;
    const QRectF bounds(0, 0, image.width(), image.height());
    QImage result = BrushRaster::context(image.width(), image.height(), false);
    QPainter painter(&result);
    BrushRaster::draw(image, bounds, painter);
    // CoreGraphics scales a clip mask smoothly to its bounds.
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    painter.drawImage(bounds, BrushRaster::alphaView(*mask));
    return result;
}
}

std::optional<LayerEffectsRenderer::Rendered> LayerEffectsRenderer::cached(const QImage &image, const std::optional<QImage> &mask,
                                                                           const std::optional<LayerEffects> &effects)
{
    const LayerEffects shown = effects.value_or(LayerEffects()).visible();
    if (shown.isEmpty() || !shown.isValid())
        return std::nullopt;
    {
        const QMutexLocker locked(&lock);
        for (const Entry &entry : entries) {
            if (entry.image.cacheKey() == image.cacheKey() && same(entry.mask, mask) && entry.effects == shown)
                return entry.result;
        }
    }
    try {
        const Entry made{image, mask, shown, render(image, mask, shown)};
        // About 64 MiB of results and sources, eight at most.
        constexpr qint64 budget = 64 * 1024 * 1024;
        const QMutexLocker locked(&lock);
        if (bytes(made) <= budget) {
            entries.push_back(made);
            qint64 total = 0;
            for (const Entry &entry : entries)
                total += bytes(entry);
            while (entries.size() > 8 || total > budget) {
                total -= bytes(entries.front());
                entries.erase(entries.begin());
            }
        }
        return made.result;
    } catch (const ProjectError &error) {
        qCWarning(lcRendering) << "a layer's effects could not be made:" << error.what();
    } catch (const ExportError &error) {
        qCWarning(lcRendering) << "a layer's effects could not be made:" << error.what();
    }
    return std::nullopt;
}

LayerTransform LayerEffectsRenderer::placed(const LayerTransform &transform, const QImage &image, double inset)
{
    const double width = image.width(), height = image.height();
    if (!(width > inset * 2) || !(height > inset * 2))
        return transform;
    LayerTransform grown = transform;
    grown.size = QSizeF(transform.size.width() * width / (width - inset * 2), transform.size.height() * height / (height - inset * 2));
    grown.origin = transform.center() - QPointF(grown.size.width() / 2, grown.size.height() / 2);
    return grown;
}

double LayerEffectsRenderer::margin(const LayerEffects &all)
{
    const LayerEffects effects = all.visible();
    double margin = 0;
    if (effects.stroke && !effects.stroke->inside)
        margin = std::max(margin, effects.stroke->size);
    if (effects.shadow)
        margin = std::max(margin, effects.shadow->distance + effects.shadow->blur * 3);
    return std::ceil(margin) + 2;
}

LayerEffectsRenderer::Rendered LayerEffectsRenderer::render(const QImage &image, const std::optional<QImage> &mask, const LayerEffects &all)
{
    const LayerEffects effects = all.visible();
    if (!effects.isValid())
        throw ProjectError(ProjectError::Kind::invalid);
    const double inset = margin(effects);
    const qint64 width = image.width() + qint64(inset) * 2, height = image.height() + qint64(inset) * 2;
    if (width <= 0 || height <= 0 || width * height > 100'000'000)
        throw ProjectError(ProjectError::Kind::tooLarge);
    // The pixels with room round them, then the effects.
    QImage padded = BrushRaster::context(int(width), int(height), false);
    {
        QPainter painter(&padded);
        BrushRaster::draw(masked(image, mask), QRectF(inset, inset, image.width(), image.height()), painter);
    }
    return {LayerEffectsKernel::render(padded, effects), inset};
}
