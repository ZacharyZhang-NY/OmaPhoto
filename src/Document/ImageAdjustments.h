#pragma once
#include "Document/ColorPalette.h"
#include <QImage>
#include <QPointF>
#include <array>
#include <functional>
#include <optional>

// Swift's ImageAdjustmentPixels: a kernel over premultiplied RGBA.
namespace ImageAdjustmentPixels {
// The body gets pixels, width, height and bytes a row.
QImage run(const QImage &image, const std::function<void(uchar *, int, int, qsizetype)> &body);
// Within range; a value that is no number falls back.
double clamp(double value, double low, double high, double fallback);
}

// A straight sRGB colour an adjustment stores, 0 to 1.
struct AdjustmentColor {
    AdjustmentColor(double red, double green, double blue) : red(red), green(green), blue(blue) {}
    explicit AdjustmentColor(const PaletteColor &color) : AdjustmentColor(color.red, color.green, color.blue) {}
    double red;
    double green;
    double blue;
    bool isValid() const;
    AdjustmentColor clamped() const;
    friend bool operator==(const AdjustmentColor &, const AdjustmentColor &) = default;
};

// Photoshop's Exposure: stops and offset in linear light, then gamma.
struct ExposureSettings {
    static constexpr double exposureLow = -20, exposureHigh = 20;
    static constexpr double offsetLow = -0.5, offsetHigh = 0.5;
    static constexpr double gammaLow = 0.01, gammaHigh = 9.99;
    double exposure = 0;
    double offset = 0;
    double gamma = 1;
    bool isValid() const;
    ExposureSettings normalized() const;
    // Each input byte's output, 0 to 1, through linear light.
    std::array<float, 256> table() const;
    QImage apply(const QImage &image) const;
    friend bool operator==(const ExposureSettings &, const ExposureSettings &) = default;
};

// Gradient Map: brightness picks a colour between the two ends.
struct GradientMapSettings {
    AdjustmentColor shadows{0, 0, 0};
    AdjustmentColor highlights{1, 1, 1};
    bool reversed = false;
    bool isValid() const;
    GradientMapSettings normalized() const;
    // Swift's ends: the darkest tone's colour, then the lightest's.
    struct Ends {
        AdjustmentColor dark;
        AdjustmentColor light;
    };
    Ends ends() const;
    QImage apply(const QImage &image) const;
    friend bool operator==(const GradientMapSettings &, const GradientMapSettings &) = default;
};

// Film grain, fixed in document space by its seed.
struct GrainSettings {
    static constexpr double amountLow = 0, amountHigh = 100;
    static constexpr double sizeLow = 0.5, sizeHigh = 20;
    static constexpr double roughnessLow = 0, roughnessHigh = 100;
    double amount = 25;
    double size = 1.5;
    double roughness = 50;
    quint32 seed = 0;
    bool isValid() const;
    GrainSettings normalized() const;
    // Pixels placed in the document; a given seed wins.
    QImage apply(const QImage &image, QPointF origin = QPointF(), double unitsPerPixel = 1, std::optional<quint32> seed = std::nullopt) const;
    friend bool operator==(const GrainSettings &, const GrainSettings &) = default;
};
