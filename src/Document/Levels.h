#pragma once
#include "Document/LayerTransform.h"
#include "Document/Selection.h"
#include "IO/ImageImporter.h"
#include <QImage>
#include <QTransform>
#include <QUuid>
#include <array>
#include <optional>

struct ImageLayer;
// Defined with the automatic levels, as Swift's.
enum class LevelsSample;

// The composite first, then each colour.
enum class LevelsChannel { rgb, red, green, blue };
inline constexpr std::array<LevelsChannel, 4> allLevelsChannels{LevelsChannel::rgb, LevelsChannel::red, LevelsChannel::green, LevelsChannel::blue};
// Swift's rawValue: the channel picker's title.
QString rawValue(LevelsChannel channel);

// One channel's input range, gamma and output range.
struct LevelRange {
    double black = 0;
    double gamma = 1;
    double white = 255;
    double outputBlack = 0;
    double outputWhite = 255;
    // Clamped into range; a value that is no number resets.
    LevelRange normalized() const;
    double apply(double value) const;
    friend bool operator==(const LevelRange &, const LevelRange &) = default;
};

struct LevelsSettings {
    LevelsChannel channel = LevelsChannel::rgb;
    std::array<LevelRange, 4> ranges{};
    LevelRange current() const;
    // Stored normalized, as Swift's setter.
    void setCurrent(const LevelRange &range);
    bool isIdentity() const;
    // The channel's range first, then the composite's.
    double apply(double value, LevelsChannel channel) const;
    // Swift's extension in LevelsAutomatic: a sample calibrates each channel.
    LevelsSettings sampling(const std::array<double, 3> &rgb, LevelsSample mode) const;
    friend bool operator==(const LevelsSettings &, const LevelsSettings &) = default;
};

// The composite's bins, then red's, green's and blue's.
using LevelsHistogram = std::array<std::array<double, 256>, 4>;

// Swift's LevelsHistogramDisplay: spikes cannot flatten the graph.
namespace LevelsHistogramDisplay {
double scale(const std::array<double, 256> &bins);
}

struct LevelsJob {
    QImage image;
    LevelsSettings settings;
    std::optional<SelectionClip> selection;
    QTransform mapping;
};

// Swift's LevelsFilter.
namespace LevelsFilter {
// The settings over the image, through the selection.
QImage run(const LevelsJob &job);
// Alpha-weighted; RGB is the mean of the channels, not luminance.
LevelsHistogram histogram(const LevelsJob &job);
}

// An open Levels edit: the layer's pixels, settings and preview.
class LevelsEdit {
public:
    LevelsEdit(const ImageLayer &layer, std::optional<SelectionClip> selection);

    // The prepared preview, for this edit's own layer.
    std::optional<QImage> previewImage(QUuid layer) const;
    LevelsJob previewJob() const;

    // Swift's object identity: results land on their own edit.
    const QUuid id = QUuid::createUuid();
    const QUuid layerID;
    const ImportedImage original;
    const LayerTransform transform;
    const std::optional<SelectionClip> selection;
    const QTransform mapping;
    // Full size to 8000 pixels a side, else scaled down.
    QImage previewSource;
    QTransform previewMapping;
    std::optional<LevelsSample> sampleMode;
    LevelsSettings settings;
    bool preview = true;
    bool committing = false;
    LevelsHistogram histogram{};
    bool histogramReady = false;
    std::optional<QImage> preparedPreview;
    std::optional<LevelsJob> pending;
};
