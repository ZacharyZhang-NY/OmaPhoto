#pragma once
#include "Document/Curves.h"
#include <QString>
#include <array>
#include <vector>

// Swift's CameraRawColor.swift: Curve, Color Mixer, Color Grading.
enum class CameraRawCurvePage { parametric, point };
enum class CameraRawPointChannel { rgb, red, green, blue };
enum class CameraRawMixerPage { hsl, color, point };
enum class CameraRawMixerTab { hue, saturation, luminance };
enum class CameraRawGradePage { threeWay, shadows, midtones, highlights, global };

// Parametric regions and point curves, points 0 to 1.
struct CameraRawCurveSettings {
    double shadows = 0;
    double darks = 0;
    double lights = 0;
    double highlights = 0;
    // Dividers, 0 to 100: where each region hands off.
    double shadowSplit = 25;
    double darkSplit = 50;
    double lightSplit = 75;
    std::vector<CurvePoint> rgb = linear();
    std::vector<CurvePoint> red = linear();
    std::vector<CurvePoint> green = linear();
    std::vector<CurvePoint> blue = linear();
    // How far the composite curve moves saturation too.
    double refineSaturation = 0;

    static std::vector<CurvePoint> linear();
    static std::vector<CurvePoint> mediumContrast();
    static std::vector<CurvePoint> strongContrast();
    static bool isLinear(const std::vector<CurvePoint> &points);
    bool adjusts() const;
    // Photoshop's parametric curve: gamma bends, smoothed as Curves.
    double parametric(double tone) const;
    // Applied to red, green and blue alike.
    std::vector<float> toneTable() const;
    std::vector<float> channelTable(const std::vector<CurvePoint> &points) const;
    CameraRawCurveSettings nudged(CameraRawPointChannel channel, double tone, double delta) const;
    // Swift's region(for:): the parametric amount a tone moves.
    double &region(double tone);
    CameraRawCurveSettings normalized() const;
    friend bool operator==(const CameraRawCurveSettings &, const CameraRawCurveSettings &) = default;

private:
    static double bend(double tone, double lower, double low, double upper, double high);
};

// One picked colour and how far its adjustment reaches.
struct CameraRawPointColor {
    double hue = 0;
    double saturation = 0;
    double luminance = 0;
    double hueShift = 0;
    double saturationShift = 0;
    double luminanceShift = 0;
    double hueRange = 30;
    double saturationRange = 0.4;
    double luminanceRange = 0.4;
    bool visualize = false;
    CameraRawPointColor normalized() const;
    friend bool operator==(const CameraRawPointColor &, const CameraRawPointColor &) = default;
};

// Eight colour families, each hue, saturation, luminance −100 to 100.
struct CameraRawMixerSettings {
    static const std::array<QString, 8> names;
    static constexpr std::array<double, 8> centers{0, 30, 60, 120, 180, 240, 270, 300};
    std::array<double, 8> hue{};
    std::array<double, 8> saturation{};
    std::array<double, 8> luminance{};
    std::vector<CameraRawPointColor> points{};
    bool adjusts() const;
    // How much each family shares a hue; neighbours overlap.
    static std::array<double, 8> weights(double degrees);
    std::vector<float> mixerFloats() const;
    std::vector<float> pointFloats() const;
    CameraRawMixerSettings normalized() const;
    friend bool operator==(const CameraRawMixerSettings &, const CameraRawMixerSettings &) = default;
};

struct CameraRawGradeWheel {
    double hue = 0;
    double saturation = 0;
    double luminance = 0;
    CameraRawGradeWheel normalized() const;
    friend bool operator==(const CameraRawGradeWheel &, const CameraRawGradeWheel &) = default;
};

// Four wheels, their overlap and which end they favour.
struct CameraRawGradingSettings {
    CameraRawGradeWheel shadows;
    CameraRawGradeWheel midtones;
    CameraRawGradeWheel highlights;
    CameraRawGradeWheel global;
    // 0 to 100: more lets the tonal wheels overlap.
    double blending = 50;
    // −100 to 100: negative favours shadows, positive highlights.
    double balance = 0;
    std::array<CameraRawGradeWheel, 4> wheels() const;
    bool adjusts() const;
    std::vector<float> gradeFloats() const;
    CameraRawGradingSettings normalized() const;
    friend bool operator==(const CameraRawGradingSettings &, const CameraRawGradingSettings &) = default;
};
