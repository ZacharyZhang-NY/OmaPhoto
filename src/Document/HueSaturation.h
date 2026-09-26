#pragma once
#include "Document/Selection.h"
#include "IO/ImageImporter.h"
#include <QImage>
#include <QTransform>
#include <QUuid>
#include <array>
#include <map>
#include <optional>
#include <vector>

struct LayerTransform;

// The six colour ranges and Master, as Photoshop's Cmd+U.
enum class ColorRange { master, reds, yellows, greens, cyans, blues, magentas };
inline constexpr std::array<ColorRange, 7> allColorRanges{ColorRange::master, ColorRange::reds,  ColorRange::yellows, ColorRange::greens,
                                                          ColorRange::cyans,  ColorRange::blues, ColorRange::magentas};
// Swift's colorRanges: every range but Master.
inline constexpr std::array<ColorRange, 6> colorRanges{ColorRange::reds,  ColorRange::yellows, ColorRange::greens,
                                                       ColorRange::cyans, ColorRange::blues,   ColorRange::magentas};
QString rawValue(ColorRange range);

// A hue band in degrees, wrapping at 360.
struct HueBand {
    double falloffStart;
    double rangeStart;
    double rangeEnd;
    double falloffEnd;
    // Degrees forward from one hue to another, 0 to 360.
    static double forward(double from, double to);
    // Full inside the range, ramping through each shoulder.
    double weight(double hue) const;
    std::array<double, 4> handles() const;
    // Centred on a hue, keeping the core and shoulders.
    HueBand centered(double hue) const;
    // Widens to hold the hue, moving the nearer edge.
    void include(double hue);
    // Narrows to leave the hue out, shoulder included.
    void exclude(double hue);
    // Moves a handle unless the four would cross.
    void setHandle(int index, double degrees);
    friend bool operator==(const HueBand &, const HueBand &) = default;

private:
    void normalize();
};

// Photoshop's starting band for a range.
HueBand defaultBand(ColorRange range);

// Which eyedropper the panel arms.
enum class HueSampleMode { replace, add, remove };
inline constexpr std::array<HueSampleMode, 3> allHueSampleModes{HueSampleMode::replace, HueSampleMode::add, HueSampleMode::remove};
QString rawValue(HueSampleMode mode);
QString help(HueSampleMode mode);

// A targeted-adjustment drag: its range and starting values.
struct HueTargetDrag {
    ColorRange range;
    double hue;
    double saturation;
};

struct RangeAdjustment {
    double hue = 0;
    double saturation = 0;
    double lightness = 0;
    friend bool operator==(const RangeAdjustment &, const RangeAdjustment &) = default;
};

// Each range keeps its own values; Master applies everywhere.
struct HueSaturationSettings {
    explicit HueSaturationSettings(double hue = 0, double saturation = 0, double lightness = 0, bool colorize = false,
                                   ColorRange range = ColorRange::master);
    ColorRange range;
    bool colorize;
    // Applies the range outside its band instead.
    bool invertRange = false;
    std::map<ColorRange, RangeAdjustment> adjustments;
    std::map<ColorRange, HueBand> bands;
    // Swift's computed properties: the selected range's values.
    double hue() const;
    void setHue(double value);
    double saturation() const;
    void setSaturation(double value);
    double lightness() const;
    void setLightness(double value);
    HueBand band() const;
    void setBand(const HueBand &value);
    // Photoshop's start when Colorize is switched on.
    static HueSaturationSettings colorizeStart();
    bool isIdentity() const;
    // Master everywhere; the others through their bands.
    double weight(ColorRange colorRange, double hue) const;
    friend bool operator==(const HueSaturationSettings &, const HueSaturationSettings &) = default;
};

struct HueSaturationJob {
    QImage image;
    HueSaturationSettings settings;
    std::optional<SelectionClip> selection;
    QTransform pixelToDocument;
    // Previews skip the panel's thumbnail.
    bool thumbnail = true;
};

struct AdjustedPixels {
    QImage image;
    std::optional<QImage> thumbnail;
};

// Swift's HueSaturationFilter: a colour cube built from the settings.
namespace HueSaturationFilter {
inline constexpr int dimension = 33;
// Swift's tuple: how every range moves one hue.
struct HueResponse {
    double shift = 0;
    double saturation = 0;
    double lightness = 0;
};
struct Color {
    double red;
    double green;
    double blue;
};
// Throws ExportError when out of memory.
AdjustedPixels run(const HueSaturationJob &job);
// Once a degree, 0 to 360.
std::vector<HueResponse> hueResponse(const HueSaturationSettings &settings);
// RGBA floats, red fastest, then green, then blue.
std::vector<float> cube(const HueSaturationSettings &settings);
Color adjust(double red, double green, double blue, const HueSaturationSettings &settings);
Color adjust(double red, double green, double blue, const HueSaturationSettings &settings, const std::vector<HueResponse> &response);
// The hue a spectrum swatch becomes.
double shiftedHue(double hue, const HueSaturationSettings &settings);
}

// One open Hue/Saturation dialog over a layer's pixels.
class HueSaturationEdit {
public:
    // Past 8000 pixels a side the preview copy is smaller.
    static constexpr int previewLimit = 8000;
    HueSaturationEdit(QUuid layerID, const ImportedImage &original, std::optional<SelectionClip> selection, const LayerTransform &transform);
    std::optional<QImage> previewImage(QUuid layer) const;
    void setPreview(std::optional<QImage> image) { m_preparedPreview = std::move(image); }

    // Swift's object identity: results land on their own edit.
    const QUuid id = QUuid::createUuid();
    const QUuid layerID;
    const ImportedImage original;
    const std::optional<SelectionClip> selection;
    const QTransform pixelToDocument;
    QImage previewSource;
    QTransform previewPixelToDocument;
    HueSaturationSettings settings;
    bool preview = true;

private:
    std::optional<QImage> m_preparedPreview;
};
