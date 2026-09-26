#include "Document/HueSaturation.h"
#include "Document/BrushStroke.h"
#include "Document/LayerTransform.h"
#include "Rendering/RasterSnapshot.h"
#include <QPainter>
#include <cmath>
#include <stdexcept>

QString rawValue(ColorRange range)
{
    switch (range) {
    case ColorRange::master: return QStringLiteral("Master");
    case ColorRange::reds: return QStringLiteral("Reds");
    case ColorRange::yellows: return QStringLiteral("Yellows");
    case ColorRange::greens: return QStringLiteral("Greens");
    case ColorRange::cyans: return QStringLiteral("Cyans");
    case ColorRange::blues: return QStringLiteral("Blues");
    case ColorRange::magentas: return QStringLiteral("Magentas");
    }
    throw std::logic_error("no such colour range");
}

HueBand defaultBand(ColorRange range)
{
    switch (range) {
    case ColorRange::master: return {0, 0, 360, 360};
    case ColorRange::reds: return {315, 345, 15, 45};
    case ColorRange::yellows: return {15, 45, 75, 105};
    case ColorRange::greens: return {75, 105, 135, 165};
    case ColorRange::cyans: return {135, 165, 195, 225};
    case ColorRange::blues: return {195, 225, 255, 285};
    case ColorRange::magentas: return {255, 285, 315, 345};
    }
    throw std::logic_error("no such colour range");
}

namespace {
// Swift's truncatingRemainder, brought into 0 to 360.
double wrap(double value)
{
    const double remainder = std::fmod(value, 360);
    return remainder < 0 ? remainder + 360 : remainder;
}
}

double HueBand::forward(double from, double to)
{
    const double delta = std::fmod(to - from, 360);
    return delta < 0 ? delta + 360 : delta;
}

double HueBand::weight(double hue) const
{
    const double span = forward(falloffStart, falloffEnd);
    // Master covers everything.
    if (!(span > 0))
        return 1;
    const double position = forward(falloffStart, hue);
    if (!(position <= span))
        return 0;
    const double rampIn = forward(falloffStart, rangeStart), plateauEnd = forward(falloffStart, rangeEnd);
    if (position < rampIn)
        return rampIn > 0 ? position / rampIn : 1;
    if (position <= plateauEnd)
        return 1;
    const double rampOut = span - plateauEnd;
    return rampOut > 0 ? (span - position) / rampOut : 1;
}

std::array<double, 4> HueBand::handles() const
{
    return {falloffStart, rangeStart, rangeEnd, falloffEnd};
}

HueBand HueBand::centered(double hue) const
{
    const double core = forward(rangeStart, rangeEnd), leading = forward(falloffStart, rangeStart), trailing = forward(rangeEnd, falloffEnd);
    const double start = wrap(hue - core / 2);
    return {wrap(start - leading), start, wrap(start + core), wrap(start + core + trailing)};
}

void HueBand::include(double hue)
{
    if (!(weight(hue) < 1))
        return;
    const double shoulderIn = forward(falloffStart, rangeStart), shoulderOut = forward(rangeEnd, falloffEnd);
    if (forward(hue, rangeStart) <= forward(rangeEnd, hue)) {
        rangeStart = hue;
        falloffStart = hue - shoulderIn;
    } else {
        rangeEnd = hue;
        falloffEnd = hue + shoulderOut;
    }
    normalize();
}

void HueBand::exclude(double hue)
{
    if (!(weight(hue) > 0))
        return;
    const double shoulderIn = forward(falloffStart, rangeStart), shoulderOut = forward(rangeEnd, falloffEnd);
    if (forward(falloffStart, hue) <= forward(hue, falloffEnd)) {
        falloffStart = hue + 1;
        rangeStart = hue + 1 + shoulderIn;
    } else {
        falloffEnd = hue - 1;
        rangeEnd = hue - 1 - shoulderOut;
    }
    normalize();
}

void HueBand::normalize()
{
    falloffStart = wrap(falloffStart);
    rangeStart = wrap(rangeStart);
    rangeEnd = wrap(rangeEnd);
    falloffEnd = wrap(falloffEnd);
    // Under a full circle.
    if (forward(falloffStart, falloffEnd) > 350)
        falloffEnd = wrap(falloffStart + 350);
}

void HueBand::setHandle(int index, double degrees)
{
    HueBand updated = *this;
    const double value = std::fmod(std::fmod(degrees, 360) + 360, 360);
    (index == 0 ? updated.falloffStart : index == 1 ? updated.rangeStart : index == 2 ? updated.rangeEnd : updated.falloffEnd) = value;
    const double span = forward(updated.falloffStart, updated.falloffEnd), toStart = forward(updated.falloffStart, updated.rangeStart),
                 toEnd = forward(updated.falloffStart, updated.rangeEnd);
    if (span > 1 && span <= 350 && toStart <= toEnd && toEnd <= span)
        *this = updated;
}

QString rawValue(HueSampleMode mode)
{
    switch (mode) {
    case HueSampleMode::replace: return QStringLiteral("Sample");
    case HueSampleMode::add: return QStringLiteral("Add");
    case HueSampleMode::remove: return QStringLiteral("Remove");
    }
    throw std::logic_error("no such sample mode");
}

QString help(HueSampleMode mode)
{
    switch (mode) {
    case HueSampleMode::replace: return QStringLiteral("Click the image to center this range on that color");
    case HueSampleMode::add: return QStringLiteral("Click the image to widen this range to include that color");
    case HueSampleMode::remove: return QStringLiteral("Click the image to narrow this range to exclude that color");
    }
    throw std::logic_error("no such sample mode");
}

HueSaturationSettings::HueSaturationSettings(double hue, double saturation, double lightness, bool colorize, ColorRange range)
    : range(range), colorize(colorize)
{
    for (const ColorRange each : allColorRanges)
        bands.emplace(each, defaultBand(each));
    adjustments[range] = RangeAdjustment{hue, saturation, lightness};
}

double HueSaturationSettings::hue() const
{
    return adjustments.contains(range) ? adjustments.at(range).hue : 0;
}

void HueSaturationSettings::setHue(double value)
{
    adjustments[range].hue = value;
}

double HueSaturationSettings::saturation() const
{
    return adjustments.contains(range) ? adjustments.at(range).saturation : 0;
}

void HueSaturationSettings::setSaturation(double value)
{
    adjustments[range].saturation = value;
}

double HueSaturationSettings::lightness() const
{
    return adjustments.contains(range) ? adjustments.at(range).lightness : 0;
}

void HueSaturationSettings::setLightness(double value)
{
    adjustments[range].lightness = value;
}

HueBand HueSaturationSettings::band() const
{
    return bands.contains(range) ? bands.at(range) : defaultBand(range);
}

void HueSaturationSettings::setBand(const HueBand &value)
{
    bands[range] = value;
}

HueSaturationSettings HueSaturationSettings::colorizeStart()
{
    return HueSaturationSettings(0, 25, 0, true);
}

bool HueSaturationSettings::isIdentity() const
{
    return !colorize && std::all_of(adjustments.begin(), adjustments.end(), [](const auto &entry) { return entry.second == RangeAdjustment(); });
}

double HueSaturationSettings::weight(ColorRange colorRange, double hue) const
{
    if (colorRange == ColorRange::master)
        return 1;
    const double inBand = (bands.contains(colorRange) ? bands.at(colorRange) : defaultBand(colorRange)).weight(hue);
    return invertRange && colorRange == range ? 1 - inBand : inBand;
}

HueSaturationEdit::HueSaturationEdit(QUuid layerID, const ImportedImage &original, std::optional<SelectionClip> selection,
                                     const LayerTransform &transform)
    : layerID(layerID), original(original), selection(std::move(selection)),
      pixelToDocument(BrushRaster::pixelToDocument(transform, original.size().width(), original.size().height()))
{
    const QSize size = original.size();
    const double factor = std::min(1.0, double(previewLimit) / std::max(size.width(), size.height()));
    if (factor < 1) {
        const int width = std::max(1, int(size.width() * factor)), height = std::max(1, int(size.height() * factor));
        previewSource = BrushRaster::context(width, height, false);
        QPainter painter(&previewSource);
        const QRectF rect(0, 0, width, height);
        if (original.raster)
            original.raster->draw(rect, painter);
        else
            BrushRaster::draw(original.image(), rect, painter);
        previewPixelToDocument = BrushRaster::pixelToDocument(transform, width, height);
    } else {
        previewSource = original.image();
        previewPixelToDocument = pixelToDocument;
    }
}

std::optional<QImage> HueSaturationEdit::previewImage(QUuid layer) const
{
    return layer == layerID ? m_preparedPreview : std::nullopt;
}
