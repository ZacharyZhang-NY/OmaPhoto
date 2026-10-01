#pragma once
#include "Document/HueSaturation.h"
#include "Document/Selection.h"
#include <QImage>
#include <QSizeF>
#include <QUuid>
#include <optional>
#include <vector>

// Select › Color Range: pixels near the clicked colours, anywhere.
class ColorRangeEdit {
public:
    static constexpr double fuzzinessLow = 0, fuzzinessHigh = 200;
    // The panel's preview fits in this, in points.
    static constexpr QSizeF previewSize{292, 200};
    ColorRangeEdit(QImage image, std::optional<DocumentSelection> original) : image(std::move(image)), original(std::move(original)) {}
    double fuzziness = 40;
    bool invert = false;
    // The next click: start over, add or take away.
    HueSampleMode sampleMode = HueSampleMode::replace;
    // Shift (add) or Alt (take away) held, over the mode.
    std::optional<HueSampleMode> held;
    HueSampleMode effectiveMode() const { return held.value_or(sampleMode); }
    // The selection in black and white; null until a colour.
    QImage preview;
    // Straight sRGB colours, three bytes each.
    std::vector<uchar> include;
    std::vector<uchar> exclude;
    std::optional<QString> error;
    // The composite at document size: what colours are matched against.
    QImage image;
    std::optional<DocumentSelection> original;
    int generation = 0;
    // Swift's object identity: a result lands on its own edit.
    QUuid id = QUuid::createUuid();
    bool hasColors() const { return !include.empty(); }
};
