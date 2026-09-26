#pragma once
#include <QImage>
#include <QPainterPath>
#include <QPointF>
#include <QString>
#include <optional>
#include <stdexcept>
#include <vector>

// Pixels either side of the click, averaged into the reference.
enum class WandSampleSize { point, threeByThree, fiveByFive };
QString title(WandSampleSize size);
int radius(WandSampleSize size);

// The Magic Wand's options-bar settings.
struct WandSettings {
    // How far each channel may differ, 0–255, and still match.
    int tolerance = 32;
    WandSampleSize sampleSize = WandSampleSize::point;
    // Only similar pixels connected to the clicked one.
    bool contiguous = true;
    // The visible composite rather than the active layer.
    bool sampleAllLayers = false;
    friend bool operator==(const WandSettings &, const WandSettings &) = default;
};

class MagicWandError : public std::runtime_error {
public:
    enum class Kind { tooDetailed, memory };
    explicit MagicWandError(Kind kind);
    const Kind kind;
};

// Selects pixels like a clicked one; the C kernel matches.
namespace MagicWand {
// The outline of pixels matching the click; nil when none.
std::optional<QPainterPath> select(const QImage &image, QPointF point, const WandSettings &settings);
// The outline of a mask's nonzero pixels along their edges.
std::optional<QPainterPath> outline(const std::vector<uchar> &mask, int width, int height);
}
