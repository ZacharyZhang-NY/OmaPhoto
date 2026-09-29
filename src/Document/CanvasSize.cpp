#include "Document/CanvasSize.h"
#include "Document/DocumentLimits.h"
#include <cmath>

QString rawValue(CanvasUnit unit)
{
    switch (unit) {
    case CanvasUnit::pixels:
        return QStringLiteral("Pixels");
    case CanvasUnit::percent:
        return QStringLiteral("Percent");
    case CanvasUnit::inches:
        return QStringLiteral("Inches");
    case CanvasUnit::centimeters:
        return QStringLiteral("Centimeters");
    }
    Q_UNREACHABLE();
}

CanvasSizeDraft::CanvasSizeDraft(qint64 width, qint64 height, double resolution)
    : originalWidth(width), originalHeight(height), resolution(resolution), width(double(width)), height(double(height))
{
}

bool CanvasSizeDraft::valid() const
{
    // NaN and infinity fail both range tests.
    return std::round(width) >= 1 && std::round(width) <= DocumentLimits::maxSide && std::round(height) >= 1 && std::round(height) <= DocumentLimits::maxSide;
}

double CanvasSizeDraft::displayed(bool widthAxis) const
{
    const double original = double(widthAxis ? originalWidth : originalHeight);
    const double pixels = (widthAxis ? width : height) - (relative ? original : 0);
    switch (unit) {
    case CanvasUnit::pixels:
        return pixels;
    case CanvasUnit::percent:
        return pixels / original * 100;
    case CanvasUnit::inches:
        return pixels / resolution;
    case CanvasUnit::centimeters:
        return pixels / resolution * 2.54;
    }
    Q_UNREACHABLE();
}

void CanvasSizeDraft::set(double value, bool widthAxis)
{
    const double original = double(widthAxis ? originalWidth : originalHeight);
    double pixels = value;
    if (unit == CanvasUnit::percent)
        pixels = value / 100 * original;
    else if (unit == CanvasUnit::inches)
        pixels = value * resolution;
    else if (unit == CanvasUnit::centimeters)
        pixels = value / 2.54 * resolution;
    const double total = pixels + (relative ? original : 0);
    if (widthAxis) {
        width = total;
        if (locked)
            height = total * double(originalHeight) / double(originalWidth);
    } else {
        height = total;
        if (locked)
            width = total * double(originalWidth) / double(originalHeight);
    }
}

QPointF CanvasSizeOptions::offset(qint64 fromWidth, qint64 oldHeight) const
{
    if (contentOffset)
        return *contentOffset;
    // Floor: growth lands right and bottom, cuts left and top.
    return QPointF(std::floor(double(width - fromWidth) * double(anchor % 3) / 2), std::floor(double(height - oldHeight) * double(anchor / 3) / 2));
}
