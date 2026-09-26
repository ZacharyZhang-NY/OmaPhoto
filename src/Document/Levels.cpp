#include "Document/Levels.h"
#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include "Rendering/RasterSnapshot.h"
#include <QPainter>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>
extern "C" {
#include "LevelsPixels.h"
}

QString rawValue(LevelsChannel channel)
{
    switch (channel) {
    case LevelsChannel::rgb: return QStringLiteral("RGB");
    case LevelsChannel::red: return QStringLiteral("Red");
    case LevelsChannel::green: return QStringLiteral("Green");
    case LevelsChannel::blue: return QStringLiteral("Blue");
    }
    throw std::logic_error("no such channel");
}

namespace {
double clamped(double value, double low, double high, double fallback)
{
    return std::isfinite(value) ? std::min(high, std::max(low, value)) : fallback;
}
}

LevelRange LevelRange::normalized() const
{
    LevelRange result = *this;
    result.black = clamped(black, 0, 254, 0);
    result.white = clamped(white, result.black + 1, 255, 255);
    result.gamma = clamped(gamma, 0.1, 9.99, 1);
    result.outputBlack = clamped(outputBlack, 0, 255, 0);
    result.outputWhite = clamped(outputWhite, 0, 255, 255);
    return result;
}

double LevelRange::apply(double value) const
{
    const LevelRange s = normalized();
    const double input = std::min(1.0, std::max(0.0, (value * 255 - s.black) / (s.white - s.black)));
    return (s.outputBlack + std::pow(input, 1 / s.gamma) * (s.outputWhite - s.outputBlack)) / 255;
}

LevelRange LevelsSettings::current() const
{
    return ranges[size_t(channel)];
}

void LevelsSettings::setCurrent(const LevelRange &range)
{
    ranges[size_t(channel)] = range.normalized();
}

bool LevelsSettings::isIdentity() const
{
    return std::all_of(ranges.begin(), ranges.end(), [](const LevelRange &range) { return range.normalized() == LevelRange(); });
}

double LevelsSettings::apply(double value, LevelsChannel channel) const
{
    return ranges[0].apply(ranges[size_t(channel)].apply(value));
}

double LevelsHistogramDisplay::scale(const std::array<double, 256> &bins)
{
    const auto counted = [](double bin) { return std::isfinite(bin) && bin > 0; };
    double peak = 0;
    for (const double bin : bins) {
        if (counted(bin))
            peak = std::max(peak, bin);
    }
    // The ends and lone spikes cap at four typical peaks.
    std::vector<double> interior;
    std::copy_if(bins.begin() + 1, bins.end() - 1, std::back_inserter(interior), counted);
    if (interior.empty())
        return peak;
    std::sort(interior.begin(), interior.end());
    const double typicalPeak = interior[size_t(double(interior.size() - 1) * 0.95)];
    return std::min(peak, typicalPeak * 4);
}

namespace {
// The job's image in a premultiplied context, rows packed.
QImage drawn(const QImage &image)
{
    QImage context = BrushRaster::context(image.width(), image.height(), false);
    QPainter painter(&context);
    BrushRaster::draw(image, QRectF(0, 0, image.width(), image.height()), painter);
    return context;
}
}

QImage LevelsFilter::run(const LevelsJob &job)
{
    if (job.settings.isIdentity())
        return job.image;
    QImage context = drawn(job.image);
    // On the stack: a heap failure would escape the catch.
    std::array<float, 3 * 256> tables;
    for (size_t channel = 0; channel < 3; ++channel) {
        for (size_t value = 0; value <= 255; ++value)
            tables[channel * 256 + value] = float(job.settings.apply(double(value) / 255, allLevelsChannels[channel + 1]));
    }
    // The kernel unpremultiplies each edge itself (Swift's cc5aac4 does twice).
    levels_apply(context.bits(), size_t(context.width()) * size_t(context.height()), tables.data());
    if (job.selection)
        return PixelAdjust::blend(context, job.image, *job.selection, job.mapping, false);
    return context;
}

LevelsHistogram LevelsFilter::histogram(const LevelsJob &job)
{
    const int width = job.image.width(), height = job.image.height();
    const QImage context = drawn(job.image);
    const std::optional<QImage> mask = job.selection ? std::optional(PixelAdjust::coverage(*job.selection, width, height, job.mapping)) : std::nullopt;
    // The kernel accumulates: row by row, since Qt pads masks.
    std::array<double, 1024> bins{};
    for (int y = 0; y < height; ++y)
        levels_histogram(context.constScanLine(y), mask ? mask->constScanLine(y) : nullptr, size_t(width), bins.data());
    LevelsHistogram result{};
    for (size_t channel = 0; channel < 4; ++channel)
        std::copy_n(bins.begin() + ptrdiff_t(channel * 256), 256, result[channel].begin());
    return result;
}

LevelsEdit::LevelsEdit(const ImageLayer &layer, std::optional<SelectionClip> selection)
    : layerID(layer.id), original(layer.asset.value()), transform(layer.transform), selection(std::move(selection)),
      mapping(BrushRaster::pixelToDocument(transform, original.size().width(), original.size().height()))
{
    const QSize size = original.size();
    // A lookup a pixel is quick; smaller copies looked coarse.
    const double factor = std::min(1.0, 8000.0 / std::max(size.width(), size.height()));
    if (factor < 1) {
        const int width = std::max(1, int(size.width() * factor)), height = std::max(1, int(size.height() * factor));
        previewSource = BrushRaster::context(width, height, false);
        QPainter painter(&previewSource);
        const QRectF rect(0, 0, width, height);
        if (original.raster)
            original.raster->draw(rect, painter);
        else
            BrushRaster::draw(original.image(), rect, painter);
        previewMapping = BrushRaster::pixelToDocument(transform, width, height);
    } else {
        previewSource = original.image();
        previewMapping = mapping;
    }
}

std::optional<QImage> LevelsEdit::previewImage(QUuid layer) const
{
    return layer == layerID ? preparedPreview : std::nullopt;
}

LevelsJob LevelsEdit::previewJob() const
{
    return LevelsJob{previewSource, settings, selection, previewMapping};
}

// Swift's session extension in Levels.swift.

void EditorSession::beginLevels()
{
    // `canAdjustColors` reads `levels`, not `hueSaturation`.
    if (m_hueSaturation || !canAdjustColors())
        return;
    // A pending gradient lands first, then Levels begins.
    if (m_gradientEdit) {
        commitGradient([this] { beginLevels(); });
        return;
    }
    commitTransform();
    cancelCrop();
    cancelLasso();
    const std::optional<DocumentSelection> current = selection();
    try {
        m_levels.emplace(activeLayer().value(), current ? std::optional(current->clip(m_document->size())) : std::nullopt);
    } catch (const ExportError &error) {
        setBrushError(QString::fromUtf8(error.what()));
        return;
    }
    countLevelsHistogram();
    notify();
}

void EditorSession::countLevelsHistogram()
{
    const LevelsJob job = m_levels.value().previewJob();
    m_levelsHistogram.setFuture(QtConcurrent::run([job]() -> std::optional<LevelsHistogram> {
        try {
            return LevelsFilter::histogram(job);
        } catch (const ExportError &error) {
            qCWarning(lcApp) << "the Levels histogram cannot be made:" << error.what();
            return std::nullopt;
        }
    }));
}

void EditorSession::finishLevelsHistogram()
{
    const std::optional<LevelsHistogram> result = m_levelsHistogram.result();
    // A commit cancels it, as Swift's; new edits replace it.
    if (!m_levels || m_levels->committing)
        return;
    // A failed count leaves the bins empty, as Swift's try?.
    if (result)
        m_levels->histogram = *result;
    m_levels->histogramReady = true;
    notify();
}

void EditorSession::updateLevels(const LevelsSettings &settings, bool preview)
{
    if (!m_levels || m_levels->committing)
        return;
    LevelsEdit &edit = *m_levels;
    edit.settings = settings;
    edit.preview = preview;
    // An adjustment layer takes the settings; its pixels stay.
    if (previewAdjustmentEditing(preview)) {
        notify();
        return;
    }
    if (!preview || settings.isIdentity()) {
        // Swift cancels the running preview: its result drops.
        m_levelsPreviewFor = QUuid();
        edit.pending = std::nullopt;
        edit.preparedPreview = std::nullopt;
        ++m_brushRevision;
        notify();
        return;
    }
    edit.pending = edit.previewJob();
    renderLevelsPreview();
    notify();
}

// Renders the newest job; one that arrives mid-render waits.
void EditorSession::renderLevelsPreview()
{
    if (!m_levels || !m_levels->pending || m_levelsRendering)
        return;
    const LevelsJob job = *std::exchange(m_levels->pending, std::nullopt);
    m_levelsPreviewFor = m_levels->id;
    m_levelsRendering = true;
    m_levelsPreview.setFuture(QtConcurrent::run([job]() -> std::optional<QImage> {
        try {
            return LevelsFilter::run(job);
        } catch (const ExportError &error) {
            qCWarning(lcApp) << "the Levels preview cannot be made:" << error.what();
            return std::nullopt;
        }
    }));
}

void EditorSession::finishLevelsPreview()
{
    m_levelsRendering = false;
    const std::optional<QImage> result = m_levelsPreview.result();
    // A cancelled run, or another edit's, lands nowhere.
    if (m_levels && m_levels->id == std::exchange(m_levelsPreviewFor, QUuid())) {
        m_levels->preparedPreview = result;
        ++m_brushRevision;
        notify();
    }
    renderLevelsPreview();
}

void EditorSession::setLevelsSampleMode(std::optional<LevelsSample> mode)
{
    if (!m_levels)
        return;
    m_levels->sampleMode = mode;
    ++m_brushRevision;
    notify();
}

void EditorSession::cancelLevels()
{
    if (finishAdjustmentEditing(false) || !m_levels || m_levels->committing)
        return;
    m_levels.reset();
    ++m_brushRevision;
    resumeFileRequests();
    notify();
}

void EditorSession::commitLevels(std::function<void()> done)
{
    // The caller resumes from the event loop, as after await.
    const auto finish = [this, done] {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
    };
    if (finishAdjustmentEditing(true) || !m_levels || m_levels->committing) {
        finish();
        return;
    }
    if (m_levels->settings.isIdentity()) {
        cancelLevels();
        finish();
        return;
    }
    LevelsEdit &edit = *m_levels;
    edit.committing = true;
    // Swift cancels the preview: its result and queue drop.
    m_levelsPreviewFor = QUuid();
    edit.pending = std::nullopt;
    m_committingLevels = Committing{edit.id, std::move(done)};
    setIsProjectBusy(true);
    // A painted layer flattens there, where a failure is caught.
    m_levelsCommit.setFuture(QtConcurrent::run([original = edit.original, settings = edit.settings, selection = edit.selection,
                                                mapping = edit.mapping]() -> Leveled {
        try {
            const QImage image = LevelsFilter::run(LevelsJob{original.image(), settings, selection, mapping});
            return Leveled{ImportedImage(image, PixelAdjust::thumbnail(image), QStringLiteral("Levels")), std::nullopt};
        } catch (const ExportError &error) {
            return Leveled{std::nullopt, QString::fromUtf8(error.what())};
        }
    }));
}

void EditorSession::finishLevelsCommit()
{
    const Leveled made = m_levelsCommit.result();
    const LevelsEdit edit = std::exchange(m_levels, std::nullopt).value();
    const Committing pending = std::exchange(m_committingLevels, std::nullopt).value();
    // Swift's defer: the edit goes, the project frees, redraw.
    const auto finish = [&] {
        ++m_brushRevision;
        setIsProjectBusy(false);
        if (pending.done)
            QMetaObject::invokeMethod(this, pending.done, Qt::QueuedConnection);
    };
    if (made.failure) {
        setBrushError(made.failure);
        finish();
        return;
    }
    const int index = m_document ? indexOf(m_document->layers, edit.layerID) : -1;
    if (index < 0) {
        finish();
        return;
    }
    const ImageLayer current = m_document->layers[index];
    if (!current.asset || current.asset->identity() != edit.original.identity() || current.transform != edit.transform) {
        finish();
        return;
    }
    beginEdit(QStringLiteral("Levels"));
    ImageLayer layer = current;
    layer.asset = made.asset;
    // Swift rebuilds the layer without shape, effects and text.
    layer.shape = std::nullopt;
    layer.effects = std::nullopt;
    layer.text = std::nullopt;
    m_document->layers[index] = layer;
    endEdit();
    finish();
}
