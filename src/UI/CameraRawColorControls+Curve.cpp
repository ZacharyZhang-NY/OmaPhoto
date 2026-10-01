#include "UI/CameraRawColorControls.h"
#include <algorithm>
#include <array>
#include <cmath>

// Swift's curve graph drags, picked once at the press.
std::optional<CameraRawCurveControls::Drag> CameraRawCurveControls::parametricDrag(QPointF at, QSizeF size) const
{
    const CameraRawCurveSettings curve = raw().curve;
    const double tone = at.x() / std::max(size.width(), 1.0) * 100;
    const std::array<double, 3> splits{curve.shadowSplit, curve.darkSplit, curve.lightSplit};
    // Along the bottom, the nearest divider, the first of equals.
    if (at.y() > size.height() - 18) {
        size_t nearest = 0;
        for (size_t index = 1; index < splits.size(); ++index)
            if (std::abs(splits[index] - tone) < std::abs(splits[nearest] - tone))
                nearest = index;
        return Drag{.kind = Drag::Kind::divider, .index = nearest, .from = at};
    }
    double CameraRawCurveSettings::*key = tone < splits[0]   ? &CameraRawCurveSettings::shadows
                                          : tone < splits[1] ? &CameraRawCurveSettings::darks
                                          : tone < splits[2] ? &CameraRawCurveSettings::lights
                                                             : &CameraRawCurveSettings::highlights;
    return Drag{.kind = Drag::Kind::region, .key = key, .start = curve.*key, .from = at};
}

std::optional<CameraRawCurveControls::Drag> CameraRawCurveControls::pointDrag(QPointF at, QSizeF size)
{
    const double x = at.x() / std::max(size.width(), 1.0), y = 1 - at.y() / std::max(size.height(), 1.0);
    std::vector<CurvePoint> points = currentPoints();
    // On a point, that point; the first of equals.
    std::optional<size_t> near;
    for (size_t index = 0; index < points.size(); ++index)
        if (!near || std::hypot(points[index].x - x, points[index].y - y) < std::hypot(points[*near].x - x, points[*near].y - y))
            near = index;
    if (near && std::hypot(points[*near].x - x, points[*near].y - y) < 0.055) {
        m_selectedPoint = near;
        return Drag{.kind = Drag::Kind::point, .index = *near, .from = at};
    }
    // Else a new point; the ends bound its x.
    if (points.size() >= 16 || !std::all_of(points.begin(), points.end(), [x](const CurvePoint &each) { return std::abs(each.x - x) > 0.01; }))
        return std::nullopt;
    const CurvePoint point{x, y};
    points.push_back(point);
    std::stable_sort(points.begin(), points.end(), [](const CurvePoint &left, const CurvePoint &right) { return left.x < right.x; });
    store(points);
    const size_t index = size_t(std::find(points.begin(), points.end(), point) - points.begin());
    m_selectedPoint = index;
    return Drag{.kind = Drag::Kind::point, .index = index, .from = at};
}

void CameraRawCurveControls::beginDrag(QPointF at, QSizeF size)
{
    // Qt may lose a release: every press picks afresh.
    const bool parametric = !m_session.filterEdit() || m_session.filterEdit()->rawPanel.curvePage == CameraRawCurvePage::parametric;
    m_drag = parametric ? parametricDrag(at, size) : pointDrag(at, size);
    continueDrag(at, size);
}

void CameraRawCurveControls::continueDrag(QPointF at, QSizeF size)
{
    if (!m_drag)
        return;
    const double x = at.x() / std::max(size.width(), 1.0), y = 1 - at.y() / std::max(size.height(), 1.0);
    const Drag drag = *m_drag;
    switch (drag.kind) {
    case Drag::Kind::point: {
        std::vector<CurvePoint> points = currentPoints();
        if (drag.index >= points.size())
            return;
        points[drag.index].y = y;
        // Ends stay ends; the rest keep their order.
        if (drag.index > 0 && drag.index < points.size() - 1) {
            // Fix beyond Swift: just inside, so the repair keeps it.
            const double inside = 1e-9;
            points[drag.index].x = std::min(points[drag.index + 1].x - 0.01 - inside, std::max(points[drag.index - 1].x + 0.01 + inside, x));
        }
        store(points);
        return;
    }
    case Drag::Kind::divider: {
        const double value = x * 100;
        update([&drag, value](CameraRawSettings &settings) {
            // Below the next divider; normalizing does the rest.
            CameraRawCurveSettings &curve = settings.curve;
            if (drag.index == 0)
                curve.shadowSplit = std::min(value, curve.darkSplit - 2);
            else if (drag.index == 1)
                curve.darkSplit = std::min(curve.lightSplit - 2, value);
            else
                curve.lightSplit = value;
        });
        return;
    }
    case Drag::Kind::region: {
        const double amount = std::min(100.0, std::max(-100.0, drag.start - (at.y() - drag.from.y()) / std::max(size.height(), 1.0) * 200));
        update([&drag, amount](CameraRawSettings &settings) { settings.curve.*drag.key = std::round(amount); });
        return;
    }
    }
}

void CameraRawCurveControls::removePoint(QPointF at, QSizeF size)
{
    std::vector<CurvePoint> points = currentPoints();
    const double x = at.x() / size.width();
    // The nearest inner point by x, within 0.04; ends stay.
    std::optional<size_t> nearest;
    for (size_t index = 1; index + 1 < points.size(); ++index)
        if (!nearest || std::abs(points[index].x - x) < std::abs(points[*nearest].x - x))
            nearest = index;
    if (!nearest || !(std::abs(points[*nearest].x - x) < 0.04))
        return;
    points.erase(points.begin() + std::ptrdiff_t(*nearest));
    m_selectedPoint.reset();
    store(points);
}
