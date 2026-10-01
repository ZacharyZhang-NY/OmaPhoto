#include "Document/EditorSession.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include <QtConcurrent>
#include <algorithm>
#include <cmath>

extern "C" {
#include "WandPixels.h"
}

// Swift's ColorRangeSelection.swift: the session's half.
namespace {
struct ColorRangeJob {
    QImage image;
    std::vector<uchar> include;
    std::vector<uchar> exclude;
    int fuzziness;
    bool invert;
};

struct ColorRangeResult {
    std::optional<QPainterPath> path;
    QImage preview;
    std::optional<QString> error;
};

// The straight colour under `point`, over 3 × 3 pixels.
std::optional<std::vector<uchar>> colorAt(const QImage &image, QPointF point)
{
    if (!std::isfinite(point.x()) || !std::isfinite(point.y()))
        return std::nullopt;
    const double fx = std::floor(point.x()), fy = std::floor(point.y());
    if (fx < 0 || fy < 0 || fx >= image.width() || fy >= image.height())
        return std::nullopt;
    const int x = int(fx), y = int(fy);
    std::array<int, 4> sums{};
    // Past the picture's edge, Swift's context holds clear pixels.
    for (int row = y - 1; row <= y + 1; ++row)
        for (int column = x - 1; column <= x + 1; ++column) {
            if (row < 0 || column < 0 || row >= image.height() || column >= image.width())
                continue;
            const uchar *pixel = image.constScanLine(row) + column * 4;
            for (int channel = 0; channel < 4; ++channel)
                sums[size_t(channel)] += pixel[channel];
        }
    if (sums[3] <= 0)
        return std::nullopt;
    std::vector<uchar> color(3);
    for (int channel = 0; channel < 3; ++channel)
        color[size_t(channel)] = uchar(std::min(255, (sums[size_t(channel)] * 255 + sums[3] / 2) / sums[3]));
    return color;
}

std::vector<uchar> colorRangeMask(const ColorRangeJob &job)
{
    const int width = job.image.width(), height = job.image.height();
    try {
        std::vector<uchar> mask(size_t(width) * size_t(height));
        color_range_mask(job.image.constBits(), size_t(width), size_t(height), size_t(job.image.bytesPerLine()), job.include.data(),
                         int(job.include.size() / 3), job.exclude.data(), int(job.exclude.size() / 3), job.fuzziness, job.invert ? 1 : 0,
                         mask.data());
        return mask;
    } catch (const std::bad_alloc &) {
        throw ExportError(ExportError::Kind::render);
    }
}

// The mask shrunk to the panel's preview, twice its size.
QImage preview(const std::vector<uchar> &mask, int width, int height)
{
    const double scale = std::min(ColorRangeEdit::previewSize.width() / width, ColorRangeEdit::previewSize.height() / height) * 2;
    const int w = std::max(1, int(width * scale)), h = std::max(1, int(height * scale));
    QImage full(width, height, QImage::Format_Grayscale8);
    if (full.isNull())
        throw ExportError(ExportError::Kind::render);
    for (int y = 0; y < height; ++y)
        std::copy_n(mask.data() + size_t(y) * size_t(width), width, full.scanLine(y));
    const QImage shrunk = full.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (shrunk.isNull())
        throw ExportError(ExportError::Kind::render);
    return shrunk;
}

ColorRangeResult colorRangeResult(const ColorRangeJob &job)
{
    ColorRangeResult result;
    try {
        const std::vector<uchar> mask = colorRangeMask(job);
        result.preview = preview(mask, job.image.width(), job.image.height());
        if (std::any_of(mask.begin(), mask.end(), [](uchar value) { return value != 0; }))
            result.path = MagicWand::outline(mask, job.image.width(), job.image.height());
    } catch (const MagicWandError &error) {
        result.error = QString::fromUtf8(error.what());
    } catch (const ExportError &error) {
        result.error = QString::fromUtf8(error.what());
    }
    return result;
}
}

bool EditorSession::canSelectColorRange() const
{
    return canEditSelection();
}

void EditorSession::beginColorRange()
{
    if (!canSelectColorRange())
        return;
    const std::optional<QImage> image = selectionSample(*m_document, true);
    if (!image)
        return;
    m_colorRange.emplace(*image, m_document->selection);
    notify();
}

void EditorSession::sampleColorRange(QPointF point, bool shift, bool option)
{
    if (!m_colorRange)
        return;
    ColorRangeEdit &edit = m_colorRange.value();
    const std::optional<std::vector<uchar>> color = colorAt(edit.image, point);
    if (!color)
        return;
    switch (option ? HueSampleMode::remove : shift ? HueSampleMode::add : edit.sampleMode) {
    case HueSampleMode::replace:
        edit.include = *color;
        edit.exclude.clear();
        break;
    case HueSampleMode::add:
        edit.include.insert(edit.include.end(), color->begin(), color->end());
        break;
    case HueSampleMode::remove:
        edit.exclude.insert(edit.exclude.end(), color->begin(), color->end());
        break;
    }
    updateColorRange();
}

void EditorSession::updateColorRange()
{
    if (!m_colorRange || !m_document)
        return;
    ColorRangeEdit &edit = m_colorRange.value();
    const int generation = ++edit.generation;
    if (!edit.hasColors()) {
        m_document->selection = edit.original;
        edit.preview = QImage();
        notify();
        return;
    }
    const ColorRangeJob job{edit.image, edit.include, edit.exclude, int(std::round(edit.fuzziness)), edit.invert};
    // A watcher a change, as Swift starts a task each.
    auto *watcher = new QFutureWatcher<ColorRangeResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, id = edit.id, generation, document = m_document->id] {
        watcher->deleteLater();
        const ColorRangeResult result = watcher->result();
        if (!m_colorRange || m_colorRange->id != id || m_colorRange->generation != generation || !m_document || m_document->id != document)
            return;
        m_colorRange->error = result.error;
        m_colorRange->preview = result.preview;
        if (result.error) {
            qCWarning(lcApp).noquote() << "Color Range could not select:" << *result.error;
        } else {
            m_document->selection = result.path ? std::optional(DocumentSelection{*result.path, m_selectionAntialiased}) : std::nullopt;
        }
        notify();
    });
    watcher->setFuture(QtConcurrent::run([job] { return colorRangeResult(job); }));
    // The colours changed now; their match lands later.
    notify();
}

void EditorSession::commitColorRange()
{
    if (!m_colorRange)
        return;
    const std::optional<DocumentSelection> result = m_document->selection;
    const ColorRangeEdit edit = std::exchange(m_colorRange, std::nullopt).value();
    m_document->selection = edit.original;
    resumeFileRequests();
    if (edit.hasColors() && !edit.error) {
        if (result)
            setSelection(result, QStringLiteral("Color Range"));
        else
            deselect();
    }
    // Closing is news even when the selection stays.
    notify();
}

void EditorSession::cancelColorRange()
{
    if (!m_colorRange)
        return;
    m_document->selection = m_colorRange->original;
    m_colorRange.reset();
    resumeFileRequests();
    notify();
}

void EditorSession::setColorRangeSampleMode(HueSampleMode mode)
{
    if (!m_colorRange || m_colorRange->sampleMode == mode)
        return;
    m_colorRange->sampleMode = mode;
    notify();
}

void EditorSession::setColorRangeHeld(std::optional<HueSampleMode> held)
{
    if (!m_colorRange || m_colorRange->held == held)
        return;
    m_colorRange->held = held;
    notify();
}

void EditorSession::setColorRangeFuzziness(double fuzziness)
{
    // Swift's binding: whole, within the range, changed or nothing.
    const double clamped = std::min(ColorRangeEdit::fuzzinessHigh, std::max(ColorRangeEdit::fuzzinessLow, std::round(fuzziness)));
    if (!m_colorRange || m_colorRange->fuzziness == clamped)
        return;
    m_colorRange->fuzziness = clamped;
    updateColorRange();
}

void EditorSession::setColorRangeInvert(bool invert)
{
    if (!m_colorRange)
        return;
    m_colorRange->invert = invert;
    updateColorRange();
}
