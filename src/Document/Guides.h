#pragma once
#include "Document/ColorPalette.h"
#include <array>
#include <QColor>
#include <QString>
#include <QUuid>
#include <optional>
#include <vector>

// A user-placed alignment line, in document pixels.
struct CanvasGuide {
    enum class Axis { horizontal, vertical };
    QUuid id;
    Axis axis;
    // Y for a horizontal guide, X for a vertical one.
    double position;

    CanvasGuide offset(double x, double y) const;
    CanvasGuide scaled(double x, double y) const;
    // Mirrored when it crosses the flip, staying on content.
    CanvasGuide mirrored(bool horizontally, double center) const;
    friend bool operator==(const CanvasGuide &, const CanvasGuide &) = default;
};

// Swift's raw values, "horizontal" and "vertical".
QString rawValue(CanvasGuide::Axis axis);
std::optional<CanvasGuide::Axis> guideAxis(const QString &text);

// The non-printing grid: majors every `spacing` px, split in parts.
struct LayoutGrid {
    static constexpr int spacingLow = 2, spacingHigh = 4096;
    static constexpr int subdivisionLow = 1, subdivisionHigh = 64;
    // Plain members, Swift's `let`s, so the struct stays assignable.
    int spacing;
    // Never finer than a pixel.
    int subdivisions;
    explicit LayoutGrid(int spacing = 64, int subdivisions = 8);
    double step() const { return double(spacing) / subdivisions; }
    // Every line along an edge from the origin, whole pixels.
    std::vector<double> lines(double length) const;
    bool isMajor(double value) const;
    // The person's grid, kept across projects and launches.
    static LayoutGrid stored();
    void store() const;
    friend bool operator==(const LayoutGrid &, const LayoutGrid &) = default;
};

// How the grid is drawn, after Photoshop's Grid settings.
struct GridAppearance {
    enum class Preset { lightGray, lightBlue, lightRed, green, mediumBlue, yellow, magenta, cyan, black, custom };
    enum class Style { lines, dashedLines, dots };
    static constexpr int opacityLow = 1, opacityHigh = 100;
    Preset preset = Preset::lightGray;
    // Custom's colour, kept while another preset is chosen.
    PaletteColor customColor{0.7, 0.7, 0.7};
    Style style = Style::lines;
    // The majors' opacity, in percent.
    int opacity = 45;
    PaletteColor color() const;
    double majorAlpha() const;
    // A little over half the majors': 28% beside 45%.
    double subdivisionAlpha() const { return majorAlpha() * 28 / 45; }
    static GridAppearance stored();
    void store() const;
    friend bool operator==(const GridAppearance &, const GridAppearance &) = default;
};

inline constexpr std::array allGridPresets{GridAppearance::Preset::lightGray, GridAppearance::Preset::lightBlue, GridAppearance::Preset::lightRed,
                                           GridAppearance::Preset::green,     GridAppearance::Preset::mediumBlue, GridAppearance::Preset::yellow,
                                           GridAppearance::Preset::magenta,   GridAppearance::Preset::cyan,      GridAppearance::Preset::black,
                                           GridAppearance::Preset::custom};
inline constexpr std::array allGridStyles{GridAppearance::Style::lines, GridAppearance::Style::dashedLines, GridAppearance::Style::dots};
QString rawValue(GridAppearance::Preset preset);
QString rawValue(GridAppearance::Style style);
// None for Custom, which takes the appearance's own.
std::optional<PaletteColor> presetColor(GridAppearance::Preset preset);
// On and off in screen points; empty is solid.
std::vector<double> dashes(GridAppearance::Style style);

// A create or move under way; the document changes after.
struct GuideDrag {
    QUuid id;
    CanvasGuide::Axis axis;
    double position;
    bool isNew;
    std::optional<double> original;
    friend bool operator==(const GuideDrag &, const GuideDrag &) = default;
};
