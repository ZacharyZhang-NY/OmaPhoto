#pragma once
#include "Document/CameraRaw.h"
#include "Document/Curves.h"
#include "Document/ImageAdjustments.h"
#include "Document/LayerTransform.h"
#include "Document/Selection.h"
#include "IO/ImageImporter.h"
#include <QImage>
#include <QRandomGenerator>
#include <QTransform>
#include <QUuid>
#include <array>
#include <cmath>

struct ImageLayer;

// The Filter and Image menus' filters.
enum class FilterKind {
    gaussianBlur, motionBlur, addNoise, vignette, bloomGlow, tonalContrast, lensCorrection, cameraRaw, removeBackground, contentAwareFill, curves,
    exposure, gradientMap, grain, blackWhite, colorBalance
};
// The menu's title, Swift's rawValue.
QString rawValue(FilterKind kind);
// Made without settings: its preview is its result.
bool isAutomatic(FilterKind kind);
// The Image menu's colour adjustments, not under Filter.
bool isImageAdjustment(FilterKind kind);
// Swift's allCases, the menus' order.
inline constexpr std::array allFilterKinds{FilterKind::gaussianBlur,     FilterKind::motionBlur,       FilterKind::addNoise,
                                           FilterKind::vignette,         FilterKind::bloomGlow,        FilterKind::tonalContrast,
                                           FilterKind::lensCorrection,   FilterKind::cameraRaw,        FilterKind::removeBackground,
                                           FilterKind::contentAwareFill,
                                           FilterKind::curves,           FilterKind::exposure,         FilterKind::gradientMap,
                                           FilterKind::grain,            FilterKind::blackWhite,       FilterKind::colorBalance};

// Remove Background's mask: the model's alone, or refined.
enum class BackgroundQuality { basic, advanced };
QString rawValue(BackgroundQuality quality);
inline constexpr std::array allBackgroundQualities{BackgroundQuality::basic, BackgroundQuality::advanced};

// Every filter's settings; each filter reads only its own.
struct FilterSettings {
    // Gaussian Blur's radius in layer pixels: the blur's sigma.
    double radius = 1;
    // Motion Blur's angle, counterclockwise from horizontal, as Photoshop's.
    double angle = 0;
    double distance = 10;
    // Add Noise's strength, Photoshop's percentage.
    double amount = 10;
    bool gaussian = false;
    // The same noise on every channel: brightness alone.
    bool monochromatic = false;
    // Vignette: the edge colour, its strength and falloff.
    double vignetteAmount = 35;
    AdjustmentColor vignetteColor = AdjustmentColor(0, 0, 0);
    double vignetteMidpoint = 50;
    double vignetteRoundness = 100;
    double vignetteFeather = 60;
    double vignetteHighlights = 25;
    // Bloom / Glow: strength, and blur radius in layer pixels.
    double bloomAmount = 40;
    double bloomRadius = 24;
    // Tonal Contrast: one detail radius, a strength per tone.
    double tonalAmount = 50;
    double tonalRadius = 16;
    double tonalShadows = 40;
    double tonalMidtones = 60;
    double tonalHighlights = 30;
    // Positive straightens barrel distortion, negative pincushion.
    double distortion = 0;
    CurvesSettings curves{};
    ExposureSettings exposure{};
    GradientMapSettings gradientMap{};
    GrainSettings grain{};
    BlackWhiteSettings blackWhite{};
    ColorBalanceSettings colorBalance{};
    CameraRawSettings cameraRaw{};
    BackgroundQuality backgroundQuality = BackgroundQuality::basic;
    // How far, in pixels, the mask is pulled onto edges.
    double refineEdges = 12;
    // Pushes the mask's grays apart, 0 to 100, clearing haze.
    double matteContrast = 25;
    // Contracts (negative) or grows the mask's edge, in pixels.
    double shiftEdge = 0;
    FilterSettings normalized() const;
    friend bool operator==(const FilterSettings &, const FilterSettings &) = default;
};

// What a filter runs on: grid, settings, selection, placement.
struct FilterJob {
    FilterKind kind;
    QImage image;
    FilterSettings settings;
    // Pixels in `image` a layer pixel: smaller previews blur less.
    double scale;
    std::optional<SelectionClip> selection;
    QTransform mapping;
    // Add Noise's pattern: the same seed, the same grain.
    quint32 seed = 0;
    // Camera Raw's preview-only views; a commit leaves them off.
    std::optional<CameraRawClipping> cameraRawClipping = std::nullopt;
    bool showsShadowClipping = false;
    bool showsHighlightClipping = false;
    // The point colour shown alone, or −1 for the grade.
    int visualizesPointColor = -1;
    bool showsSharpenMask = false;
    // A live adjustment's noise field, fixed in its region's pixels.
    QPointF noiseOrigin{};
    // Vignette on an empty layer: the canvas it fills.
    std::optional<QRectF> canvas = std::nullopt;
};

// Swift's PixelFilter.
namespace PixelFilter {
struct Trimmed {
    QImage image;
    LayerTransform transform;
};

// CIMotionBlur's radius a streak pixel: an even streak's spread.
inline const double motionRadiusPerPixel = 1 / std::sqrt(12.0);
// Remove Distortion at ±100 moves the corners this share.
inline constexpr double lensStrength = 0.35;
// The pixels that are there, placed where they were.
Trimmed trimmed(const QImage &image, const LayerTransform &placed);
// The filter over the job's image, through its selection.
QImage run(const FilterJob &job);
}

// An open filter: the layer's pixels, preview copy, grown grid.
class FilterEdit {
public:
    // Previews render from a copy no longer than this.
    static constexpr double previewLimit = 2048;
    // `area`: a document area the grid must cover (Content-Aware Fill).
    FilterEdit(FilterKind kind, const ImageLayer &layer, std::optional<SelectionClip> selection, const FilterSettings &settings,
               std::optional<QRectF> area);
    // The room a blur needs round the layer, in pixels.
    static double blurMargin(FilterKind kind, const FilterSettings &settings);
    // Pads the grid for the blur; it only ever grows.
    void growForBlur();
    std::optional<QImage> previewImage(QUuid id) const;
    // The settings as rendered: a hidden Camera Raw group drops.
    FilterSettings renderSettings() const;
    FilterJob previewJob() const;

    // Swift's object identity: a preview lands on its own edit.
    const QUuid id = QUuid::createUuid();
    const FilterKind kind;
    const QUuid layerID;
    const ImportedImage original;
    const LayerTransform transform;
    const std::optional<SelectionClip> selection;
    QTransform mapping;
    QImage previewSource;
    double previewScale = 1;
    QTransform previewMapping;
    // The layer padded out, and the transform placing it.
    std::optional<QImage> grownImage;
    std::optional<LayerTransform> grownTransform;
    // How far the padding reaches past every side.
    double grownMargin = 0;
    FilterSettings settings;
    bool preview = true;
    bool committing = false;
    std::optional<QString> previewError;
    bool preparing = false;
    // Add Noise's grain, fixed while the panel is open.
    const quint32 seed = QRandomGenerator::global()->generate();
    std::optional<QImage> preparedPreview;
    // The settings the prepared preview was made with.
    std::optional<FilterSettings> preparedSettings;
    // Moves with each new grid: an older render lands nothing.
    quint64 previewSourceVersion = 0;
    std::optional<FilterJob> pending;
    CameraRawPanel rawPanel;
    // Vignette on an empty layer: the canvas it fills.
    std::optional<QRectF> canvas;
    // The layer had no pixels: the filter began clear.
    bool startedEmpty = false;

private:
    void grow(const QRectF &extent);
    // Swift's prepared and prepare: mapping and preview copy.
    void prepare(const ImportedImage &source, const LayerTransform &placed);
};
