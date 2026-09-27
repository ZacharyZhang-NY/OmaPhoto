#pragma once
#include "Document/CameraRawColor.h"
#include "Document/CameraRawDetailOptics.h"
#include "Document/CameraRawGeometryCalibration.h"
#include <QImage>
#include <array>
#include <optional>
#include <utility>

struct FilterJob;

// Relative offsets, not kelvin; `auto` is a keyword here.
enum class CameraRawWhiteBalance { custom, automatic };
enum class CameraRawGlowStyle { diffusion, bloom, halation };
// Highlight Priority's Highlights slider protects bright pixels.
enum class CameraRawVignetteStyle { highlightPriority, colorPriority, paintOverlay };
// Option on a Light slider shows clipping; never written in.
enum class CameraRawClipping { highlights = 1, shadows = 2 };
enum class CameraRawScopeMode { histogram, vectorscope };

// Swift's panel eyes: a hidden group leaves the grade.
struct CameraRawGroups {
    bool light = true;
    bool color = true;
    bool effects = true;
    bool curve = true;
    bool mixer = true;
    bool grading = true;
    bool detail = true;
    bool optics = true;
    bool geometry = true;
    bool calibration = true;
    friend bool operator==(const CameraRawGroups &, const CameraRawGroups &) = default;
};

// Defaults leave the image unchanged.
struct CameraRawSettings {
    static constexpr double exposureLow = -5, exposureHigh = 5;
    // A full warm or cool swing on red and blue.
    static constexpr double temperatureGain = 0.35;
    static constexpr double tintRedBlue = 0.15;
    static constexpr double tintGreen = 0.30;

    CameraRawWhiteBalance whiteBalance = CameraRawWhiteBalance::custom;
    // Cool to warm, −100 to 100; tint green to magenta.
    double temperature = 0;
    double tint = 0;
    // Stops of linear light, −5 to 5.
    double exposure = 0;
    double contrast = 0;
    double highlights = 0;
    double shadows = 0;
    double whites = 0;
    double blacks = 0;
    double vibrance = 0;
    double saturation = 0;
    // Local contrast: Texture the finer band, Clarity the broader.
    double texture = 0;
    double clarity = 0;
    double dehaze = 0;
    // 0 to 100; range, spread and warmth rest at zero.
    double glow = 0;
    CameraRawGlowStyle glowStyle = CameraRawGlowStyle::diffusion;
    double glowRange = 0;
    double glowSpread = 0;
    double glowWarmth = 0;
    // Negative darkens the edges, positive lightens; the centre stays.
    double vignetteAmount = 0;
    CameraRawVignetteStyle vignetteStyle = CameraRawVignetteStyle::highlightPriority;
    double vignetteMidpoint = 50;
    double vignetteRoundness = 0;
    double vignetteFeather = 50;
    double vignetteHighlights = 0;
    double grainAmount = 0;
    double grainSize = 25;
    double grainRoughness = 50;
    CameraRawCurveSettings curve;
    CameraRawMixerSettings mixer;
    CameraRawGradingSettings grading;
    CameraRawDetailSettings detail;
    CameraRawOpticsSettings optics;
    CameraRawGeometrySettings geometry;
    CameraRawCalibrationSettings calibration;

    bool adjustsLight() const;
    bool adjustsColor() const;
    bool adjustsEffects() const;
    bool isIdentity() const;
    CameraRawSettings normalized() const;
    // The grade with some panel eyes off: those groups zeroed.
    CameraRawSettings applying(const CameraRawGroups &shown) const;
    // Camera Raw's 0 to 100 size in `adjust_grain`'s pixels.
    double grainKernelSize() const;
    struct Gains {
        double red;
        double green;
        double blue;
    };
    Gains gains() const;
    // `clipping`: the Option view; `scale`: preview pixels a pixel.
    QImage apply(const QImage &image, std::optional<CameraRawClipping> clipping = std::nullopt, double scale = 1, quint32 seed = 0,
                 int visualizePointColor = -1, bool sharpenMask = false) const;
    struct Balance {
        double temperature;
        double tint;
    };
    // The temperature and tint making a linear pixel neutral.
    static std::optional<Balance> neutralizeLinear(double red, double green, double blue);
    static std::optional<Balance> neutralizeStraight(double red, double green, double blue);
    // Gray-world balance of the opaque pixels.
    static std::optional<Balance> autoBalance(const QImage &image);
    void applyCurveColor(uchar *pixels, int width, int height, qsizetype stride, int visualize) const;
    void applyDetailOptics(uchar *pixels, int width, int height, qsizetype stride, double scale, double profileStrength, bool sharpenMask) const;
    void applyCalibration(uchar *pixels, int width, int height, qsizetype stride) const;
    friend bool operator==(const CameraRawSettings &, const CameraRawSettings &) = default;
};

// An RGB histogram and a hue and saturation vectorscope.
struct CameraRawScope {
    static constexpr int binCount = 256;
    static constexpr int scopeSide = 64;
    std::array<double, binCount> red{};
    std::array<double, binCount> green{};
    std::array<double, binCount> blue{};
    // Density from the centre out, row by row.
    std::vector<double> vectorscope = std::vector<double>(scopeSide * scopeSide);
    // One vertical scale, so the ribbons stay comparable.
    double peak() const;
    // Counts the graded image, clear pixels skipped.
    static std::optional<CameraRawScope> make(const QImage &image);
    // Clipped shadows blue, clipped highlights red, over the grade.
    static QImage overlay(const QImage &image, bool shadows, bool highlights);
    // The preview and the scope of the grade itself.
    static std::pair<QImage, CameraRawScope> preview(const FilterJob &job);
    friend bool operator==(const CameraRawScope &, const CameraRawScope &) = default;
};

// Swift's CameraRawDrag: a targeted adjustment under way.
struct CameraRawDrag {
    double startY;
    CameraRawSettings settings;
    double tone;
    double hue;
};

// Swift's FilterEdit fields for the Camera Raw panel.
struct CameraRawPanel {
    // Eyes off drop a group from preview and OK.
    CameraRawGroups shows;
    CameraRawCurvePage curvePage = CameraRawCurvePage::parametric;
    CameraRawPointChannel pointChannel = CameraRawPointChannel::rgb;
    CameraRawMixerPage mixerPage = CameraRawMixerPage::hsl;
    CameraRawMixerTab mixerTab = CameraRawMixerTab::hue;
    int mixerSwatch = 0;
    int pointIndex = 0;
    CameraRawGradePage gradePage = CameraRawGradePage::threeWay;
    bool targetsCurve = false;
    bool targetsMixer = false;
    bool samplesPointColor = false;
    std::optional<CameraRawDrag> drag;
    // The white-balance and defringe eyedroppers.
    bool samplesWhiteBalance = false;
    bool samplesDefringe = false;
    // Guided Upright: lines dragged on the preview.
    bool drawingGeometryGuide = false;
    std::optional<std::pair<QPointF, QPointF>> guideDraft;
    // Option on a Light slider, or on Sharpening's Masking.
    std::optional<CameraRawClipping> clipping;
    bool sharpenMask = false;
    // The histogram's clipping marks, never baked in on OK.
    bool showsShadowClipping = false;
    bool showsHighlightClipping = false;
    CameraRawScopeMode scopeMode = CameraRawScopeMode::histogram;
    std::optional<CameraRawScope> scope;
    // The adjusted preview's RGB under the pointer.
    std::optional<std::array<int, 3>> readout;
    // The point colour being shown in isolation, or −1.
    int pointColorVisualizeIndex(const CameraRawSettings &settings) const;
};
