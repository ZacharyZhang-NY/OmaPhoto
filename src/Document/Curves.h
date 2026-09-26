#pragma once
#include "Document/Levels.h"
#include <QImage>
#include <vector>

struct CurvePoint {
    double x;
    double y;
    friend bool operator==(const CurvePoint &, const CurvePoint &) = default;
};

// Swift's CurvesSettings: a curve a channel, the composite first.
struct CurvesSettings {
    LevelsChannel channel = LevelsChannel::rgb;
    std::vector<std::vector<CurvePoint>> channels = std::vector<std::vector<CurvePoint>>(4, {{0, 0}, {255, 255}});
    bool isValid() const;
    // Shape-preserving cubic Hermite: no overshoot between handles.
    double value(double x, size_t channel) const;
    // Throws ProjectError when invalid, ExportError when out of memory.
    QImage apply(const QImage &image) const;
    friend bool operator==(const CurvesSettings &, const CurvesSettings &) = default;
};
