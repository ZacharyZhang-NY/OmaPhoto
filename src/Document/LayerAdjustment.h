#pragma once
#include "Document/Curves.h"
#include "Document/Filters.h"
#include "Document/HueSaturation.h"
#include "Document/ImageAdjustments.h"
#include "Document/Levels.h"
#include <QRectF>
#include <QString>
#include <array>
#include <optional>

// An adjustment layer's kind, in the Layer menu's order.
enum class AdjustmentKind { hsv, levels, curves, exposure, gradientMap, grain, addNoise, gaussianBlur, motionBlur, invert, blackWhite, colorBalance };
inline constexpr std::array allAdjustmentKinds{AdjustmentKind::hsv,          AdjustmentKind::levels,     AdjustmentKind::curves,
                                               AdjustmentKind::exposure,     AdjustmentKind::gradientMap, AdjustmentKind::grain,
                                               AdjustmentKind::addNoise,     AdjustmentKind::gaussianBlur, AdjustmentKind::motionBlur,
                                               AdjustmentKind::invert,       AdjustmentKind::blackWhite, AdjustmentKind::colorBalance};
// Swift's rawValue: the new layer's name and the manifest's word.
QString rawValue(AdjustmentKind kind);
// The kind a manifest names, or none.
std::optional<AdjustmentKind> adjustmentKind(const QString &text);
// The filter panel that edits it; Levels, Hue/Saturation, Invert: none.
std::optional<FilterKind> filterKind(AdjustmentKind kind);
// Every kind but Invert opens an editor.
bool isEditable(AdjustmentKind kind);

// An adjustment layer's settings; its kind reads only its own.
struct LayerAdjustment {
    AdjustmentKind kind;
    double hue = 0;
    double saturation = 0;
    double lightness = 0;
    bool colorize = false;
    // Absent in projects from before range-aware Hue/Saturation.
    std::optional<HueSaturationSettings> hsvSettings = std::nullopt;
    LevelsSettings levels{};
    CurvesSettings curves{};
    // Absent unless set, so older kinds save exactly as before.
    std::optional<ExposureSettings> exposureSettings = std::nullopt;
    std::optional<GradientMapSettings> gradientMapSettings = std::nullopt;
    std::optional<GrainSettings> grainSettings = std::nullopt;
    std::optional<BlackWhiteSettings> blackWhiteSettings = std::nullopt;
    std::optional<ColorBalanceSettings> colorBalanceSettings = std::nullopt;
    // Absent in projects from before blur and noise layers.
    std::optional<double> blurRadius = std::nullopt;
    std::optional<double> motionAngle = std::nullopt;
    std::optional<double> motionDistance = std::nullopt;
    std::optional<double> noiseAmount = std::nullopt;
    std::optional<bool> noiseGaussian = std::nullopt;
    std::optional<bool> noiseMonochromatic = std::nullopt;
    std::optional<quint32> noiseSeed = std::nullopt;
    HueSaturationSettings resolvedHSV() const;
    // Swift's computed properties over the optional settings.
    ExposureSettings exposure() const;
    void setExposure(const ExposureSettings &value);
    GradientMapSettings gradientMap() const;
    void setGradientMap(const GradientMapSettings &value);
    GrainSettings grain() const;
    void setGrain(const GrainSettings &value);
    BlackWhiteSettings blackWhite() const;
    void setBlackWhite(const BlackWhiteSettings &value);
    ColorBalanceSettings colorBalance() const;
    void setColorBalance(const ColorBalanceSettings &value);
    double gaussianRadius() const;
    void setGaussianRadius(double value);
    double resolvedMotionAngle() const;
    void setResolvedMotionAngle(double value);
    double resolvedMotionDistance() const;
    void setResolvedMotionDistance(double value);
    double resolvedNoiseAmount() const;
    void setResolvedNoiseAmount(double value);
    bool resolvedNoiseGaussian() const;
    void setResolvedNoiseGaussian(bool value);
    bool resolvedNoiseMonochromatic() const;
    void setResolvedNoiseMonochromatic(bool value);
    quint32 resolvedNoiseSeed() const;
    void setResolvedNoiseSeed(quint32 value);
    // Document pixels a partial redraw reads past its rect.
    double samplingMargin() const;
    bool isValid() const;
    // Region: the document shown; scale: image pixels a document pixel.
    QImage apply(const QImage &image, std::optional<QRectF> region = std::nullopt, double scale = 1) const;
    friend bool operator==(const LayerAdjustment &, const LayerAdjustment &) = default;
};
