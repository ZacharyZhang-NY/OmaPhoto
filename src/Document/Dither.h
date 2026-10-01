#pragma once
#include "Document/ImageAdjustments.h"
#include <QImage>
#include <QString>
#include <array>
#include <vector>

// Filter › Dither's looks, in the panel's order and DitherPixels.h's.
enum class DitherStyle { atkinson, floydSteinberg, bayer2, bayer4, bayer8, dots, lines, diamonds, patterns, ascii };
inline constexpr std::array allDitherStyles{DitherStyle::atkinson, DitherStyle::floydSteinberg, DitherStyle::bayer2, DitherStyle::bayer4,
                                            DitherStyle::bayer8,   DitherStyle::dots,           DitherStyle::lines,  DitherStyle::diamonds,
                                            DitherStyle::patterns, DitherStyle::ascii};
QString rawValue(DitherStyle style);
// Swift's groups: diffusion, ordered, halftone, marks.
int ditherGroup(DitherStyle style);
// Each pixel's rounding error passes to its neighbours.
bool diffuses(DitherStyle style);
// Diffusion and ordered styles have tones; the rest draw marks.
bool hasTones(DitherStyle style);
bool isHalftone(DitherStyle style);
bool drawsMarks(DitherStyle style);

// A chunky pixel: a square, or a dot on dark.
enum class DitherPixelShape { square, dot };
inline constexpr std::array allDitherPixelShapes{DitherPixelShape::square, DitherPixelShape::dot};
QString rawValue(DitherPixelShape shape);

enum class DitherColors { blackWhite, twoColors, original };
inline constexpr std::array allDitherColors{DitherColors::blackWhite, DitherColors::twoColors, DitherColors::original};
QString rawValue(DitherColors colors);

// Swift's DitherSettings: the filter's look, in layer pixels.
struct DitherSettings {
    static constexpr double pixelSizeLow = 1, pixelSizeHigh = 32;
    static constexpr double cellSizeLow = 4, cellSizeHigh = 64;
    static constexpr double textSizeLow = 6, textSizeHigh = 64;
    static constexpr double levelsLow = 2, levelsHigh = 8;
    static QString defaultCharacters() { return QStringLiteral(" .:-=+*#%@"); }
    DitherStyle style = DitherStyle::atkinson;
    // Each dithered pixel covers this many layer pixels a side.
    double pixelSize = 2;
    DitherPixelShape pixelShape = DitherPixelShape::square;
    // Halftone cells, in dithered pixels.
    double cellSize = 8;
    // ASCII's line height in layer pixels; letters about 0.6 wide.
    double textSize = 14;
    // The halftone screen's angle, in degrees.
    double angle = 45;
    // Tones per channel; 2 is 1-bit.
    double levels = 2;
    // How much error passes on, 0–100%.
    double diffusion = 100;
    // −100…100: more ink or less, flatter or punchier.
    double density = 0;
    double contrast = 0;
    DitherColors colors = DitherColors::blackWhite;
    AdjustmentColor dark = AdjustmentColor(0, 0, 0);
    AdjustmentColor light = AdjustmentColor(1, 1, 1);
    // Marks stand for the light tones, lit on the dark.
    bool lightOnDark = true;
    // ASCII's characters, sorted by their ink.
    QString characters = defaultCharacters();
    DitherSettings normalized() const;
    QImage apply(const QImage &image) const;
    friend bool operator==(const DitherSettings &, const DitherSettings &) = default;

    // Each distinct character's coverage map, least ink first.
    struct Glyphs {
        std::vector<uint8_t> maps;
        std::vector<float> coverage;
        int width = 1;
        int height = 1;
    };
    // A monospaced cell: a line tall, a letter wide.
    static Glyphs glyphs(const QString &characters, int lineHeight);

private:
    QImage dither(const QImage &image) const;
};
