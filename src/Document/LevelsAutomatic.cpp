#include "Document/LevelsAutomatic.h"
#include "Document/EditorSession.h"
#include "IO/ImageExporter.h"
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

QString rawValue(LevelsSample sample)
{
    switch (sample) {
    case LevelsSample::black: return QStringLiteral("Black");
    case LevelsSample::gray: return QStringLiteral("Gray");
    case LevelsSample::white: return QStringLiteral("White");
    }
    throw std::logic_error("no such sample");
}

QString rawValue(LevelsAuto mode)
{
    switch (mode) {
    case LevelsAuto::contrast: return QStringLiteral("Contrast");
    case LevelsAuto::color: return QStringLiteral("Color");
    case LevelsAuto::neutral: return QStringLiteral("Color + neutral midtones");
    }
    throw std::logic_error("no such mode");
}

namespace {
// The bins holding all but a thousandth at either end.
std::optional<std::pair<double, double>> endpoints(const std::array<double, 256> &bins)
{
    const double total = std::accumulate(bins.begin(), bins.end(), 0.0);
    if (!(total > 0))
        return std::nullopt;
    double sum = 0;
    int low = 0, high = 255;
    for (int index = 0; index < 256; ++index) {
        sum += bins[size_t(index)];
        if (sum > total * 0.001) {
            low = index;
            break;
        }
    }
    sum = 0;
    for (int index = 255; index >= 0; --index) {
        sum += bins[size_t(index)];
        if (sum > total * 0.001) {
            high = index;
            break;
        }
    }
    return low < high ? std::optional(std::pair(double(low), double(high))) : std::nullopt;
}
}

LevelsSettings settings(LevelsAuto mode, const LevelsHistogram &histogram)
{
    LevelsSettings result;
    if (mode == LevelsAuto::contrast) {
        // A shared interval preserves channel relationships.
        std::optional<double> low, high;
        for (size_t channel = 1; channel < 4; ++channel) {
            if (const std::optional<std::pair<double, double>> limits = endpoints(histogram[channel])) {
                low = std::min(low.value_or(limits->first), limits->first);
                high = std::max(high.value_or(limits->second), limits->second);
            }
        }
        // Each channel's low lies below its high: so do theirs.
        if (low)
            result.ranges[0] = LevelRange{*low, 1, high.value(), 0, 255};
        return result;
    }
    for (size_t channel = 1; channel < 4; ++channel) {
        const std::optional<std::pair<double, double>> limits = endpoints(histogram[channel]);
        if (!limits)
            continue;
        LevelRange range{limits->first, 1, limits->second, 0, 255};
        if (mode == LevelsAuto::neutral) {
            const double total = std::accumulate(histogram[channel].begin(), histogram[channel].end(), 0.0);
            double weighted = 0;
            for (size_t index = 0; index < 256; ++index)
                weighted += range.apply(double(index) / 255) * histogram[channel][index];
            // Weight at both ends keeps the mean inside, under 9.99.
            range.gamma = std::max(0.1, std::log(weighted / total) / std::log(0.5));
        }
        result.ranges[channel] = range;
    }
    return result;
}

// Samples are unpremultiplied RGB; the channels calibrate together.
LevelsSettings LevelsSettings::sampling(const std::array<double, 3> &rgb, LevelsSample mode) const
{
    LevelsSettings result = *this;
    result.ranges[0] = LevelRange();
    for (size_t channel = 1; channel < 4; ++channel) {
        LevelRange range = result.ranges[channel];
        const double value = rgb[channel - 1] * 255;
        if (mode == LevelsSample::black) {
            range.black = std::min(range.white - 1, value);
        } else if (mode == LevelsSample::white) {
            range.white = std::max(range.black + 1, value);
        } else {
            const double fraction = (value - range.black) / (range.white - range.black);
            if (!(fraction > 0 && fraction < 1))
                continue;
            range.gamma = std::log(fraction) / std::log(0.5);
        }
        range.outputBlack = 0;
        range.outputWhite = 255;
        result.ranges[channel] = range.normalized();
    }
    return result;
}

// Swift's session extension in LevelsAutomatic.swift.

void EditorSession::autoLevels(LevelsAuto mode)
{
    if (!m_levels || !m_levels->histogramReady || m_levels->committing)
        return;
    m_levels->sampleMode = std::nullopt;
    updateLevels(::settings(mode, m_levels->histogram), m_levels->preview);
}

void EditorSession::sampleLevels(QPointF point)
{
    // Positive bounds: a point that is no number samples nothing.
    if (!m_levels || !m_levels->sampleMode || m_levels->committing || !m_document
        || !(point.x() >= 0 && point.y() >= 0 && point.x() < m_document->width && point.y() < m_document->height))
        return;
    const LevelsEdit &edit = *m_levels;
    const QPointF pixel = edit.mapping.inverted().map(point);
    const QSize size = edit.original.size();
    if (!(pixel.x() >= 0 && pixel.y() >= 0 && pixel.x() < size.width() && pixel.y() < size.height()))
        return;
    try {
        QImage context = BrushRaster::context(1, 1, false);
        {
            QPainter painter(&context);
            BrushRaster::draw(edit.original.image(), QRectF(0, 0, 1, 1), painter, QRectF(std::floor(pixel.x()), std::floor(pixel.y()), 1, 1));
        }
        const uchar *bytes = context.constBits();
        if (bytes[3] == 0)
            return;
        const auto channel = [bytes](int index) { return std::min(1.0, double(bytes[index]) / bytes[3]); };
        updateLevels(edit.settings.sampling({channel(0), channel(1), channel(2)}, *edit.sampleMode), edit.preview);
    } catch (const ExportError &error) {
        setBrushError(QString::fromUtf8(error.what()));
    }
}
