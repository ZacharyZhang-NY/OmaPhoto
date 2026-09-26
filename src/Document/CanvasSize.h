#pragma once
#include <QPointF>
#include <QString>
#include <QtGlobal>
#include <optional>

enum class CanvasUnit { pixels, percent, inches, centimeters };
// The name users see.
QString rawValue(CanvasUnit unit);

// What the canvas size sheet edits, in final pixels.
struct CanvasSizeDraft {
    CanvasSizeDraft(qint64 width, qint64 height, double resolution);

    qint64 originalWidth;
    qint64 originalHeight;
    double resolution;
    double width;
    double height;
    bool relative = false;
    bool locked = false;
    CanvasUnit unit = CanvasUnit::pixels;

    bool valid() const;
    double displayed(bool widthAxis) const;
    void set(double value, bool widthAxis);
};

struct CanvasExtensionColor {
    double red;
    double green;
    double blue;
};

struct CanvasSizeOptions {
    qint64 width;
    qint64 height;
    // Row-major, top-left through bottom-right.
    qint64 anchor = 4;
    std::optional<CanvasExtensionColor> fill = std::nullopt;
    // Crop supplies its own translation of the content.
    std::optional<QPointF> contentOffset = std::nullopt;

    QPointF offset(qint64 fromWidth, qint64 oldHeight) const;
};
