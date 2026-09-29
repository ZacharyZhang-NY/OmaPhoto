#include "Document/MagicWand.h"
#include "Rendering/TransformOverlay.h"
#include "Logging.h"
#include <QtConcurrent>
#include <cmath>

// Swift's level of detail for marching ants, zoomed out.
namespace {
// Marquees and lassos stay exact: this many elements draw whole.
constexpr int fullDetailLimit = 20'000;
}

std::optional<QPainterPath> TransformOverlay::antsOutline(const QPainterPath &path, double deviceScale) const
{
    if (m_antsSource != path) {
        m_antsSource = path;
        m_antsTask.reset();
        m_antsLevel.reset();
        m_antsPendingStep.reset();
        m_antsSourceIsComplex = path.elementCount() > fullDetailLimit;
    }
    const double scale = m_session.viewport.pointsPerPixel() * deviceScale;
    if (!m_antsSourceIsComplex || scale >= 1)
        return path;
    // Screen pixels a document pixel, a power of two above.
    const double step = std::min(1.0, std::pow(2, std::ceil(std::log2(std::max(scale, 1.0 / 4096)))));
    if ((!m_antsLevel || m_antsLevel->step != step) && m_antsPendingStep != step) {
        m_antsPendingStep = step;
        m_antsTask = std::make_unique<QFutureWatcher<std::optional<QPainterPath>>>();
        QFutureWatcher<std::optional<QPainterPath>> *watcher = m_antsTask.get();
        QObject::connect(watcher, &QFutureWatcherBase::finished, [this, watcher, step] {
            m_antsPendingStep.reset();
            if (const std::optional<QPainterPath> traced = watcher->result())
                m_antsLevel = AntsLevel{*traced, step};
            if (repaintAnts)
                repaintAnts();
        });
        const QRectF canvas(QPointF(0, 0), m_session.document().value().size());
        watcher->setFuture(QtConcurrent::run([path, canvas, step] { return traceOutline(path, canvas, step); }));
    }
    // The last step stands in until the new one lands.
    return m_antsLevel ? std::optional(m_antsLevel->path) : std::nullopt;
}

// Filled, averaged down to `step`, traced along the pixels' edges.
std::optional<QPainterPath> TransformOverlay::traceOutline(const QPainterPath &path, QRectF canvas, double step)
{
    const QRectF box = path.boundingRect().intersected(canvas);
    const QRectF region(QPointF(std::floor(box.left()), std::floor(box.top())), QPointF(std::ceil(box.right()), std::ceil(box.bottom())));
    if (box.isEmpty() || region.width() < 1 || region.height() < 1)
        return std::nullopt;
    // Half resolution at least, about 40 megapixels at most.
    const double fill = std::min(1.0, std::max(step, std::sqrt(40'000'000 / (region.width() * region.height()))));
    QImage filled(std::max(1, int(std::ceil(region.width() * fill))), std::max(1, int(std::ceil(region.height() * fill))), QImage::Format_Grayscale8);
    if (filled.isNull())
        return std::nullopt;
    filled.fill(0);
    {
        QPainter painter(&filled);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.scale(fill, fill);
        painter.translate(-region.topLeft());
        painter.fillPath(path, Qt::white);
    }
    const int width = std::max(1, int(std::ceil(region.width() * step))), height = std::max(1, int(std::ceil(region.height() * step)));
    const QImage small = filled.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (small.isNull())
        return std::nullopt;
    // Any coverage counts, so thin parts keep their outline.
    std::vector<uchar> pixels(size_t(width) * size_t(height), 0);
    for (int row = 0; row < height; ++row) {
        const uchar *line = small.constScanLine(row);
        for (int column = 0; column < width; ++column)
            pixels[size_t(row) * size_t(width) + size_t(column)] = line[column] > 0 ? 255 : 0;
    }
    std::optional<QPainterPath> traced;
    try {
        traced = MagicWand::outline(pixels, width, height);
    } catch (const MagicWandError &error) {
        // Swift's `try?` is silent; the ants wait a step.
        qCWarning(lcRendering) << "the ants' outline could not be traced:" << error.what();
        return std::nullopt;
    }
    if (!traced)
        return std::nullopt;
    return QTransform::fromTranslate(region.left(), region.top()).scale(1 / step, 1 / step).map(*traced);
}
