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
enum class AdjustmentKind { hsv, levels, curves, exposure, gradientMap, grain };
inline constexpr std::array allAdjustmentKinds{AdjustmentKind::hsv,      AdjustmentKind::levels,      AdjustmentKind::curves,
                                               AdjustmentKind::exposure, AdjustmentKind::gradientMap, AdjustmentKind::grain};
// Swift's rawValue: the new layer's name and the manifest's word.
QString rawValue(AdjustmentKind kind);
// The kind a manifest names, or none.
std::optional<AdjustmentKind> adjustmentKind(const QString &text);
// The filter panel that edits it; Levels and Hue/Saturation: none.
std::optional<FilterKind> filterKind(AdjustmentKind kind);

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
    HueSaturationSettings resolvedHSV() const;
    // Swift's computed properties over the optional settings.
    ExposureSettings exposure() const;
    void setExposure(const ExposureSettings &value);
    GradientMapSettings gradientMap() const;
    void setGradientMap(const GradientMapSettings &value);
    GrainSettings grain() const;
    void setGrain(const GrainSettings &value);
    bool isValid() const;
    // `region`: the document `image` covers, whole pixels by default.
    QImage apply(const QImage &image, std::optional<QRectF> region = std::nullopt) const;
    friend bool operator==(const LayerAdjustment &, const LayerAdjustment &) = default;
};
