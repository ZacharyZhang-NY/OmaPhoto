#include "Document/MagicWand.h"
#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include "Rendering/LayerRenderer.h"
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <cstdlib>
extern "C" {
#include "WandPixels.h"
}

QString title(WandSampleSize size)
{
    switch (size) {
    case WandSampleSize::point:
        return QStringLiteral("Point Sample");
    case WandSampleSize::threeByThree:
        return QStringLiteral("3 by 3 Average");
    case WandSampleSize::fiveByFive:
        return QStringLiteral("5 by 5 Average");
    }
    throw std::logic_error("unknown sample size");
}

int radius(WandSampleSize size)
{
    return int(size);
}

MagicWandError::MagicWandError(Kind kind)
    : std::runtime_error(kind == Kind::tooDetailed
                             ? "That selection is too detailed to outline. Try a different Tolerance, or turn on Contiguous."
                             : "There isn’t enough memory to make that selection."),
      kind(kind)
{
}

std::optional<QPainterPath> MagicWand::select(const QImage &image, QPointF point, const WandSettings &settings)
{
    const int width = image.width(), height = image.height();
    if (!std::isfinite(point.x()) || !std::isfinite(point.y()))
        return std::nullopt;
    const double fx = std::floor(point.x()), fy = std::floor(point.y());
    if (fx < 0 || fy < 0 || fx >= width || fy >= height)
        return std::nullopt;
    // Premultiplied RGBA, rows top down, as the kernel reads.
    QImage rgba = BrushRaster::context(width, height, false);
    QPainter painter(&rgba);
    BrushRaster::draw(image, QRectF(0, 0, width, height), painter);
    painter.end();
    std::vector<uchar> selected(size_t(width) * size_t(height));
    const long count = wand_mask(rgba.constBits(), size_t(width), size_t(height), size_t(rgba.bytesPerLine()), size_t(fx), size_t(fy),
                                 size_t(radius(settings.sampleSize)), std::clamp(settings.tolerance, 0, 255), settings.contiguous ? 1 : 0, selected.data());
    if (count < 0)
        throw MagicWandError(MagicWandError::Kind::memory);
    if (count == 0)
        return std::nullopt;
    return outline(selected, width, height);
}

std::optional<QPainterPath> MagicWand::outline(const std::vector<uchar> &mask, int width, int height)
{
    if (width <= 0 || height <= 0 || mask.size() != size_t(width) * size_t(height))
        return std::nullopt;
    int32_t *points = nullptr, *loops = nullptr;
    size_t pointCount = 0, loopCount = 0;
    const int status = wand_trace(mask.data(), size_t(width), size_t(height), &points, &pointCount, &loops, &loopCount);
    const std::unique_ptr<int32_t, decltype(&std::free)> ownedPoints(points, &std::free), ownedLoops(loops, &std::free);
    if (status == -2)
        throw MagicWandError(MagicWandError::Kind::tooDetailed);
    if (status != 0)
        throw MagicWandError(MagicWandError::Kind::memory);
    if (loopCount == 0)
        return std::nullopt;
    // Outer loops clockwise, holes the other way: winding fills them.
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    size_t index = 0;
    for (size_t loop = 0; loop < loopCount; ++loop) {
        const size_t length = size_t(loops[loop]);
        path.moveTo(points[index * 2], points[index * 2 + 1]);
        for (size_t corner = index + 1; corner < index + length; ++corner)
            path.lineTo(points[corner * 2], points[corner * 2 + 1]);
        path.closeSubpath();
        index += length;
    }
    return path;
}

void EditorSession::setWandSettings(const WandSettings &settings)
{
    m_wandSettings = settings;
    notify();
}

// What the wand reads: composite, or the active layer's pixels.
std::optional<QImage> EditorSession::wandSample(const CanvasDocument &document) const
{
    try {
        QImage context = BrushRaster::context(document.width, document.height, false);
        QPainter painter(&context);
        // A painted asset flattens here; the catch covers it too.
        if (m_wandSettings.sampleAllLayers) {
            drawLiveComposite(document, painter);
        } else if (const std::optional<ImageLayer> layer = activeLayer(); layer && !layer->isGroup && layer->asset) {
            const LayerTransform transform = displayedTransform(*layer);
            LayerRenderer::draw(layer->asset->image(), transform, transform.center(), painter, {});
        }
        painter.end();
        return context;
    } catch (const ExportError &error) {
        qCWarning(lcApp) << "the wand cannot sample the canvas:" << error.what();
        return std::nullopt;
    }
}

void EditorSession::magicWand(QPointF point, SelectionMode mode, std::function<void()> done)
{
    // The caller resumes from the event loop, as after await.
    const auto finish = [this, done] {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
    };
    const std::optional<CanvasDocument> &document = m_document;
    // Swift's positive bounds refuse a point that is no number.
    const bool inside = document && point.x() >= 0 && point.y() >= 0 && point.x() < document->width && point.y() < document->height;
    if (!canEditSelection() || m_isProjectBusy || m_selectionMoveOrigin || !inside) {
        finish();
        return;
    }
    const std::optional<QImage> sample = wandSample(*document);
    if (!sample) {
        finish();
        return;
    }
    setIsProjectBusy(true);
    m_wanding = Wanding{mode, document->id, std::move(done)};
    m_wand.setFuture(QtConcurrent::run([sample = *sample, point, settings = m_wandSettings]() -> Wanded {
        try {
            return Wanded{MagicWand::select(sample, point, settings), std::nullopt};
        } catch (const MagicWandError &error) {
            return Wanded{std::nullopt, QString::fromUtf8(error.what())};
        } catch (const ExportError &error) {
            return Wanded{std::nullopt, QString::fromUtf8(error.what())};
        }
    }));
}

void EditorSession::finishWand()
{
    const Wanded result = m_wand.result();
    const Wanding pending = std::exchange(m_wanding, std::nullopt).value();
    setIsProjectBusy(false);
    if (result.failure) {
        qCWarning(lcApp).noquote() << "the wand could not select:" << *result.failure;
        setBrushError(result.failure);
    } else if (m_document && m_document->id == pending.documentID) {
        if (!result.path) {
            // Nothing matched: New clears, as a lasso click enclosing nothing.
            if (pending.mode == SelectionMode::replace)
                deselect();
        } else if (pending.mode == SelectionMode::replace) {
            // A traced outline lies on the canvas: no costly clip.
            setSelection(DocumentSelection{*result.path, m_selectionAntialiased}, QStringLiteral("Magic Wand"));
        } else {
            applySelection(*result.path, pending.mode, QStringLiteral("Magic Wand"));
        }
    }
    if (pending.done)
        QMetaObject::invokeMethod(this, pending.done, Qt::QueuedConnection);
}
