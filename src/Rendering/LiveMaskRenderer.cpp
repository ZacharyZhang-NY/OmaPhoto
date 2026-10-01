#include "Rendering/LiveMaskRenderer.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include "Rendering/LayerRenderer.h"

extern "C" {
#include "BrushPixels.h"
}

LiveMaskRenderer::LiveMaskRenderer(Source source, DrawOwn drawOwn, qint64 pixelBudget)
    : m_source(std::move(source)), m_drawOwn(std::move(drawOwn)), m_pixelBudget(pixelBudget)
{
}

void LiveMaskRenderer::prepareStacks(const std::vector<QUuid> &ids, const std::function<std::optional<QUuid>(QUuid)> &parent,
                                     const std::function<LayerBlendMode(QUuid)> &blend)
{
    for (const QUuid &id : ids)
        m_modes.insert_or_assign(id, blend(id));
    for (size_t index = 0; index < ids.size(); ++index) {
        const QUuid base = ids[index];
        if (m_source(base))
            continue;
        std::vector<QUuid> children;
        for (size_t next = index + 1; next < ids.size(); ++next) {
            if (m_source(ids[next]) != base || parent(ids[next]) != parent(base))
                break;
            children.push_back(ids[next]);
        }
        if (children.empty())
            continue;
        m_stackModes.insert_or_assign(base, blend(base));
        m_stacked.insert(children.begin(), children.end());
        m_stacks.insert_or_assign(base, std::move(children));
    }
}

void LiveMaskRenderer::drawComposite(QUuid id, QPainter &context, const QImage &clip)
{
    if (m_stacked.contains(id))
        return;
    // Clipped, an adjustment draws inside its base's stack alone.
    if (adjustment(id)) {
        if (!m_source(id))
            adjust(id, context, clip);
        return;
    }
    const auto children = m_stacks.find(id);
    const QSize device(context.device()->width(), context.device()->height());
    QImage group, alpha;
    if (children != m_stacks.end() && fits(context)) {
        group = QImage(device, QImage::Format_RGBA8888_Premultiplied);
        alpha = QImage(device, QImage::Format_Grayscale8);
        if (group.isNull() || alpha.isNull())
            qCWarning(lcRendering) << "a clipping stack could not be allocated:" << device;
    }
    if (group.isNull() || alpha.isNull()) {
        // Without a group each child draws alone, through the base.
        if (children != m_stacks.end()) {
            for (const QUuid &child : children->second)
                m_stacked.erase(child);
        }
        draw(id, context, clip);
        return;
    }
    group.fill(0);
    {
        QPainter painter(&group);
        painter.setTransform(context.deviceTransform());
        m_drawOwn(id, painter, QImage());
    }
    // Children blend at the base's full coverage; its alpha returns.
    layer_extract_alpha(group.constBits(), size_t(group.bytesPerLine()), alpha.bits(), size_t(alpha.bytesPerLine()),
                        size_t(device.width()), size_t(device.height()));
    layer_unpremultiply_opaque(group.bits(), size_t(group.bytesPerLine()), size_t(device.width()), size_t(device.height()));
    {
        QPainter painter(&group);
        painter.setTransform(context.deviceTransform());
        for (const QUuid &child : children->second) {
            if (adjustment(child))
                adjust(child, painter, QImage());
            else
                m_drawOwn(child, painter, QImage());
        }
    }
    layer_restore_alpha(group.bits(), size_t(group.bytesPerLine()), alpha.constBits(), size_t(alpha.bytesPerLine()),
                        size_t(device.width()), size_t(device.height()));
    // Device pixels already: one to one, at the painter's opacity.
    LayerRenderer::composite(context, context.deviceTransform().inverted(), QRectF(group.rect()), LayerSampling::nearest,
                             InterpolationQuality::none, {.opacity = context.opacity(), .blendMode = m_stackModes.at(id), .clip = clip}, QRectF(),
                             [&](QPainter &aside) { aside.drawImage(QRectF(group.rect()), group); });
}

void LiveMaskRenderer::draw(QUuid id, QPainter &context, const QImage &clip)
{
    // drawOwn's painter changes end here, even when it throws.
    struct Saved {
        QPainter &painter;
        explicit Saved(QPainter &context) : painter(context) { painter.save(); }
        ~Saved() { painter.restore(); }
    } saved(context);
    const std::optional<QUuid> sourceID = m_source(id);
    if (!sourceID) {
        m_drawOwn(id, context, clip);
        return;
    }
    const std::optional<QImage> shown = coverage(*sourceID, context);
    if (!shown)
        return;
    QImage combined = *shown;
    if (!clip.isNull()) {
        // A failed copy of the cached coverage must not pass.
        QPainter multiplying(&combined);
        if (!multiplying.isActive())
            throw ExportError(ExportError::Kind::render);
        multiplying.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        multiplying.drawImage(QRectF(combined.rect()), clip);
    }
    m_drawOwn(id, context, combined);
}

bool LiveMaskRenderer::fits(const QPainter &context) const
{
    const qint64 width = context.device()->width(), height = context.device()->height();
    return width > 0 && height > 0 && width * height <= m_pixelBudget;
}

std::optional<QImage> LiveMaskRenderer::coverage(QUuid id, const QPainter &context)
{
    if (const auto found = m_cache.find(id); found != m_cache.end())
        return found->second;
    if (!fits(context))
        qCWarning(lcRendering) << "clip coverage passes its pixel budget:" << context.device()->width() << "x" << context.device()->height();
    if (m_visiting.contains(id) || m_visiting.size() >= 256 || !fits(context))
        return std::nullopt;
    struct Visit {
        std::set<QUuid> &visiting;
        QUuid id;
        ~Visit() { visiting.erase(id); }
    } visit{m_visiting, id};
    m_visiting.insert(id);
    const QSize device(context.device()->width(), context.device()->height());
    QImage pixels(device, QImage::Format_RGBA8888_Premultiplied), gray(device, QImage::Format_Alpha8);
    if (pixels.isNull() || gray.isNull()) {
        qCWarning(lcRendering) << "clip coverage could not be allocated:" << device;
        return std::nullopt;
    }
    pixels.fill(0);
    {
        QPainter painter(&pixels);
        painter.setTransform(context.deviceTransform());
        draw(id, painter);
    }
    layer_extract_alpha(pixels.constBits(), size_t(pixels.bytesPerLine()), gray.bits(), size_t(gray.bytesPerLine()),
                        size_t(device.width()), size_t(device.height()));
    m_cache.insert_or_assign(id, gray);
    return gray;
}

namespace {
// Colours blend at full coverage; the original alpha then returns.
QImage blended(const QImage &original, const QImage &adjusted, LayerBlendMode mode)
{
    const int width = original.width(), height = original.height();
    QImage base = original.copy(), top = adjusted.copy(), alpha(original.size(), QImage::Format_Grayscale8);
    if (base.isNull() || top.isNull() || alpha.isNull())
        throw ExportError(ExportError::Kind::render);
    layer_extract_alpha(base.constBits(), size_t(base.bytesPerLine()), alpha.bits(), size_t(alpha.bytesPerLine()), size_t(width), size_t(height));
    layer_unpremultiply_opaque(base.bits(), size_t(base.bytesPerLine()), size_t(width), size_t(height));
    layer_unpremultiply_opaque(top.bits(), size_t(top.bytesPerLine()), size_t(width), size_t(height));
    {
        QPainter painter(&base);
        LayerRenderer::composite(painter, QTransform(), QRectF(base.rect()), LayerSampling::nearest, InterpolationQuality::none,
                                 {.opacity = 1, .blendMode = mode}, QRectF(), [&](QPainter &aside) { aside.drawImage(QRectF(top.rect()), top); });
    }
    layer_restore_alpha(base.bits(), size_t(base.bytesPerLine()), alpha.constBits(), size_t(alpha.bytesPerLine()), size_t(width), size_t(height));
    return base;
}

// Adjusted over original by opacity, coverage and clip.
QImage mixed(const QImage &adjusted, const QImage &original, const QImage &coverage, const QImage &clip, double opacity)
{
    QImage result = original.copy();
    if (result.isNull())
        throw ExportError(ExportError::Kind::render);
    for (int y = 0; y < result.height(); ++y) {
        const uchar *top = adjusted.constScanLine(y), *weight = coverage.constScanLine(y);
        const uchar *outer = clip.isNull() ? nullptr : clip.constScanLine(y);
        uchar *target = result.scanLine(y);
        for (int x = 0; x < result.width(); ++x) {
            const double share = opacity * weight[x] / 255 * (outer ? outer[x] / 255.0 : 1);
            for (int channel = 0; channel < 4; ++channel) {
                const int index = x * 4 + channel;
                target[index] = uchar(std::lround(target[index] + (top[index] - target[index]) * share));
            }
        }
    }
    return result;
}
}

void LiveMaskRenderer::adjust(QUuid id, QPainter &context, const QImage &clip)
{
    auto *device = dynamic_cast<QImage *>(context.device());
    if (!device)
        throw std::logic_error("an adjustment reads a painter on an image");
    if (!fits(context)) {
        qCWarning(lcRendering) << "an adjustment passes its pixel budget:" << device->size();
        return;
    }
    // What the device shows; Grain and Noise keep the document.
    const QRectF shown = context.deviceTransform().inverted().mapRect(QRectF(device->rect()));
    const QRectF region = adjustmentRegion ? adjustmentRegion(shown) : shown;
    try {
        // The kernels' byte order, one image pixel a device pixel.
        QImage original = device->convertToFormat(QImage::Format_RGBA8888_Premultiplied).copy();
        if (original.isNull())
            throw ExportError(ExportError::Kind::render);
        original.setDevicePixelRatio(1);
        // Device pixels a document pixel, where Swift's surface counts points.
        QImage adjusted = adjustment(id).value().apply(original, region, adjustmentScale * device->width() / shown.width());
        const auto found = m_modes.find(id);
        const LayerBlendMode mode = found != m_modes.end() ? found->second : LayerBlendMode::normal;
        if (mode != LayerBlendMode::normal)
            adjusted = blended(original, adjusted, mode);
        QImage coverage(device->size(), QImage::Format_Alpha8);
        if (coverage.isNull())
            throw ExportError(ExportError::Kind::render);
        coverage.fill(255);
        adjustmentClip(id, context, coverage);
        const QImage result = mixed(adjusted, original, coverage, clip, adjustmentOpacity(id));
        // Back in device pixels, through the painter's own clip.
        context.save();
        context.setTransform(context.deviceTransform().inverted(), true);
        context.setCompositionMode(QPainter::CompositionMode_Source);
        context.drawImage(QRectF(result.rect()), result);
        context.restore();
    } catch (const ExportError &error) {
        qCWarning(lcRendering) << "an adjustment could not be drawn:" << error.what();
    }
}
