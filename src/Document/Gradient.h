#pragma once
#include <QPointF>
#include <QString>
#include <array>
#include <cmath>
#include <memory>

class BrushStroke;

enum class GradientStyle { foregroundToBackground, foregroundToTransparent };
inline constexpr std::array allGradientStyles{GradientStyle::foregroundToBackground, GradientStyle::foregroundToTransparent};
QString rawValue(GradientStyle style);

// Linear runs start to end; radial centres on the start.
enum class GradientShape { linear, radial };
inline constexpr std::array allGradientShapes{GradientShape::linear, GradientShape::radial};
QString rawValue(GradientShape shape);

struct GradientSettings {
    GradientShape shape = GradientShape::linear;
    GradientStyle style = GradientStyle::foregroundToTransparent;
    bool reversed = false;
    double opacity = 1;
    friend bool operator==(const GradientSettings &, const GradientSettings &) = default;
};

// A pending gradient on a layer or mask, document pixels.
struct GradientEdit {
    // Shared: the canvas draws it while a commit renders.
    std::shared_ptr<BrushStroke> raster;
    QPointF start;
    QPointF end;
    bool hasLine() const { return std::hypot(end.x() - start.x(), end.y() - start.y()) >= 0.5; }
};
