#include "Document/Curves.h"
#include "Document/BrushStroke.h"
#include "IO/ProjectStore.h"
#include <QPainter>
#include <algorithm>
#include <array>
#include <cmath>
extern "C" {
#include "LevelsPixels.h"
}

bool CurvesSettings::isValid() const
{
    // Fixed ends and rising x keep x in range.
    return channels.size() == 4 && std::all_of(channels.begin(), channels.end(), [](const std::vector<CurvePoint> &points) {
        return points.size() >= 2 && points.size() <= 32 && points.front().x == 0 && points.back().x == 255
            && std::all_of(points.begin(), points.end(), [](const CurvePoint &point) { return point.y >= 0 && point.y <= 255; })
            && std::adjacent_find(points.begin(), points.end(), [](const CurvePoint &a, const CurvePoint &b) { return !(a.x < b.x); }) == points.end();
    });
}

double CurvesSettings::value(double x, size_t channel) const
{
    const std::vector<CurvePoint> &p = channels.at(channel);
    // Swift's lastIndex(where:), else the first segment.
    size_t last = 0;
    for (size_t index = 0; index < p.size(); ++index) {
        if (p[index].x <= x)
            last = index;
    }
    const size_t i = std::min(p.size() - 2, last);
    std::vector<double> d;
    for (size_t index = 0; index + 1 < p.size(); ++index)
        d.push_back((p[index + 1].y - p[index].y) / (p[index + 1].x - p[index].x));
    const auto slope = [&](size_t j) {
        if (j == 0)
            return d.front();
        if (j == p.size() - 1)
            return d.back();
        if (d[j - 1] * d[j] <= 0)
            return 0.0;
        return 2 / (1 / d[j - 1] + 1 / d[j]);
    };
    const double h = p[i + 1].x - p[i].x, t = std::min(1.0, std::max(0.0, (x - p[i].x) / h));
    const double y = (2 * t * t * t - 3 * t * t + 1) * p[i].y + (t * t * t - 2 * t * t + t) * h * slope(i)
        + (-2 * t * t * t + 3 * t * t) * p[i + 1].y + (t * t * t - t * t) * h * slope(i + 1);
    return std::min(255.0, std::max(0.0, y));
}

QImage CurvesSettings::apply(const QImage &image) const
{
    if (!isValid())
        throw ProjectError(ProjectError::Kind::invalid);
    QImage context = BrushRaster::context(image.width(), image.height(), false);
    {
        QPainter painter(&context);
        BrushRaster::draw(image, QRectF(0, 0, image.width(), image.height()), painter);
    }
    // Each colour's curve, then the composite's.
    std::array<float, 3 * 256> tables;
    for (size_t channel = 1; channel <= 3; ++channel) {
        for (size_t x = 0; x < 256; ++x)
            tables[(channel - 1) * 256 + x] = float(value(value(double(x), channel), 0) / 255);
    }
    levels_apply(context.bits(), size_t(image.width()) * size_t(image.height()), tables.data());
    return context;
}
