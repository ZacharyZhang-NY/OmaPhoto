#pragma once
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

// The non-printing grid: majors every 64 px, eight subdivisions.
namespace LayoutGrid {
inline constexpr double spacing = 64;
inline constexpr int subdivisions = 8;
inline constexpr double step = spacing / subdivisions;
// Every line along a document edge, whole pixels.
std::vector<double> lines(double length);
bool isMajor(double value);
}

// A create or move under way; the document changes after.
struct GuideDrag {
    QUuid id;
    CanvasGuide::Axis axis;
    double position;
    bool isNew;
    std::optional<double> original;
    friend bool operator==(const GuideDrag &, const GuideDrag &) = default;
};
