#pragma once
#include "IO/ProjectStore.h"
#include <QImage>
#include <QRect>
#include <array>
#include <optional>

// What Trim compares each edge against.
enum class TrimBasedOn { transparentPixels, topLeftPixelColor, bottomRightPixelColor };
inline constexpr std::array allTrimBasedOn{TrimBasedOn::transparentPixels, TrimBasedOn::topLeftPixelColor, TrimBasedOn::bottomRightPixelColor};
// Swift's rawValue: the sheet's words.
QString rawValue(TrimBasedOn basedOn);

struct TrimOptions {
    TrimBasedOn basedOn = TrimBasedOn::transparentPixels;
    bool top = true;
    bool bottom = true;
    bool left = true;
    bool right = true;
    // Per channel, premultiplied: how far a pixel may stray.
    quint8 tolerance = 0;
    bool trimsAny() const;
    friend bool operator==(const TrimOptions &, const TrimOptions &) = default;
};

namespace ImageTrim {
// The rect to keep, or none when nothing would remain.
std::optional<QRect> calculateTrimRect(const QImage &image, const TrimOptions &options);
// The rendered canvas trimmed by the canvas resizer, or none.
std::optional<ProjectSnapshot> trim(const ProjectSnapshot &snapshot, const TrimOptions &options);
}
