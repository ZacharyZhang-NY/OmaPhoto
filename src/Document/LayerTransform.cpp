#include "Document/LayerTransform.h"
#include "Document/BrushStroke.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace {
bool sameSize(QSizeF lhs, QSizeF rhs)
{
    return lhs.width() == rhs.width() && lhs.height() == rhs.height();
}

struct Shift {
    double move = 0;
    std::optional<double> target;
};

Shift shift(const std::array<double, 3> &guides, const std::vector<double> &targets, double tolerance)
{
    Shift best;
    for (double guide : guides) {
        for (double target : targets) {
            const double move = target - guide;
            if (std::abs(move) > tolerance)
                continue;
            if (best.target && std::abs(best.move) <= std::abs(move))
                continue;
            best = {move, target};
        }
    }
    return best;
}
}

QString rawValue(LayerSampling sampling)
{
    switch (sampling) {
    case LayerSampling::nearest:
        return QStringLiteral("Nearest");
    case LayerSampling::smooth:
        return QStringLiteral("Smooth");
    case LayerSampling::high:
        return QStringLiteral("High quality");
    }
    throw std::logic_error("unknown LayerSampling");
}

std::optional<LayerSampling> layerSampling(const QString &rawValue)
{
    for (const LayerSampling sampling : {LayerSampling::nearest, LayerSampling::smooth, LayerSampling::high}) {
        if (rawValue == ::rawValue(sampling))
            return sampling;
    }
    return std::nullopt;
}

InterpolationQuality quality(LayerSampling sampling)
{
    switch (sampling) {
    case LayerSampling::nearest:
        return InterpolationQuality::none;
    case LayerSampling::smooth:
        return InterpolationQuality::low;
    case LayerSampling::high:
        return InterpolationQuality::high;
    }
    throw std::logic_error("unknown LayerSampling");
}

const std::array<QPointF, 8> LayerTransform::handles = {
    QPointF(0, 0), QPointF(0.5, 0), QPointF(1, 0), QPointF(1, 0.5),
    QPointF(1, 1), QPointF(0.5, 1), QPointF(0, 1), QPointF(0, 0.5)};

bool operator==(const LayerTransform &lhs, const LayerTransform &rhs)
{
    return lhs.origin.x() == rhs.origin.x() && lhs.origin.y() == rhs.origin.y()
        && sameSize(lhs.size, rhs.size) && lhs.rotation == rhs.rotation
        && lhs.flipX == rhs.flipX && lhs.flipY == rhs.flipY && lhs.sampling == rhs.sampling;
}

QPointF LayerTransform::center() const
{
    return {origin.x() + size.width() / 2, origin.y() + size.height() / 2};
}

double LayerTransform::radians() const
{
    return std::fmod(rotation, 360) * std::numbers::pi / 180;
}

bool LayerTransform::isValid() const
{
    const std::array<double, 5> values = {origin.x(), origin.y(), size.width(), size.height(), rotation};
    return std::all_of(values.begin(), values.end(), [](double value) { return std::isfinite(value); })
        && size.width() >= 1 && size.width() <= 300'000 && size.height() >= 1 && size.height() <= 300'000
        && std::abs(origin.x()) <= 1'000'000 && std::abs(origin.y()) <= 1'000'000;
}

QPointF LayerTransform::point(QPointF unit) const
{
    const double x = (unit.x() - 0.5) * size.width(), y = (unit.y() - 0.5) * size.height();
    const QPointF middle = center();
    return {middle.x() + x * std::cos(radians()) - y * std::sin(radians()),
            middle.y() + x * std::sin(radians()) + y * std::cos(radians())};
}

bool LayerTransform::contains(QPointF point) const
{
    const double x = point.x() - center().x(), y = point.y() - center().y();
    return std::abs(x * std::cos(radians()) + y * std::sin(radians())) <= size.width() / 2
        && std::abs(-x * std::sin(radians()) + y * std::cos(radians())) <= size.height() / 2;
}

double LayerTransform::scalePercent(QSizeF pixelSize) const
{
    return size.width() / std::max(1.0, pixelSize.width()) * 100;
}

LayerTransform LayerTransform::scaled(double toPercent, QSizeF pixelSize) const
{
    LayerTransform result = *this;
    result.size = {pixelSize.width() * toPercent / 100, pixelSize.height() * toPercent / 100};
    result.origin = {center().x() - result.size.width() / 2, center().y() - result.size.height() / 2};
    return result;
}

LayerTransform LayerTransform::rounded() const
{
    LayerTransform result = *this;
    result.origin = {std::round(origin.x()), std::round(origin.y())};
    result.size = {std::max(1.0, std::round(size.width())), std::max(1.0, std::round(size.height()))};
    result.rotation = std::round(rotation);
    return result;
}

QTransform LayerTransform::unitToDocument() const
{
    return BrushRaster::pixelToDocument(*this, 1, 1);
}

LayerTransform LayerTransform::placing(const QTransform &map) const
{
    const double sign = flipX ? -1 : 1;
    const double angle = std::atan2(map.m12() * sign, map.m11() * sign);
    const double along = -map.m21() * std::sin(angle) + map.m22() * std::cos(angle);
    const QPointF middle = map.map(QPointF(0.5, 0.5));
    LayerTransform result = *this;
    result.size = {std::hypot(map.m11(), map.m12()), std::abs(along)};
    const double degrees = angle * 180 / std::numbers::pi;
    result.rotation = degrees + std::round((rotation - degrees) / 360) * 360;
    result.flipY = along < 0;
    result.origin = {middle.x() - result.size.width() / 2, middle.y() - result.size.height() / 2};
    return result;
}

LayerTransform LayerTransform::following(const LayerTransform &old, const LayerTransform &updated) const
{
    if (old == updated)
        return *this;
    if (sameSize(old.size, updated.size) && old.rotation == updated.rotation
        && old.flipX == updated.flipX && old.flipY == updated.flipY) {
        LayerTransform moved = *this;
        moved.origin += updated.origin - old.origin;
        return moved;
    }
    return placing(unitToDocument() * old.unitToDocument().inverted() * updated.unitToDocument());
}

bool LayerTransform::samePlacement(const LayerTransform &other) const
{
    LayerTransform copy = *this;
    copy.sampling = other.sampling;
    return copy == other;
}

std::optional<Corners> TransformDrag::corners(QPointF to, bool shift) const
{
    if (!originalCorners)
        return std::nullopt;
    double dx = to.x() - start.x(), dy = to.y() - start.y();
    if (shift) {
        if (std::abs(dx) >= std::abs(dy))
            dy = 0;
        else
            dx = 0;
    }
    std::vector<int> moved;
    if (mode.kind == Kind::distort) {
        const int corner = mode.index / 2;
        moved = mode.index % 2 == 0 ? std::vector<int>{corner} : std::vector<int>{corner, (corner + 1) % 4};
    } else if (mode.kind == Kind::move) {
        moved = {0, 1, 2, 3};
    } else {
        return std::nullopt;
    }
    Corners result = *originalCorners;
    for (int corner : moved)
        result[corner] += QPointF(dx, dy);
    return result;
}

LayerTransform TransformDrag::updated(QPointF to, bool lockRatio, bool shift, bool option) const
{
    LayerTransform result = original;
    const double radians = original.radians();
    switch (mode.kind) {
    case Kind::distort:
        break;
    case Kind::move: {
        double dx = to.x() - start.x(), dy = to.y() - start.y();
        if (shift) {
            if (std::abs(dx) >= std::abs(dy))
                dy = 0;
            else
                dx = 0;
        }
        result.origin += QPointF(dx, dy);
        break;
    }
    case Kind::rotate: {
        const QPointF center = original.center();
        const double delta = std::atan2(to.y() - center.y(), to.x() - center.x())
            - std::atan2(start.y() - center.y(), start.x() - center.x());
        result.rotation += delta * 180 / std::numbers::pi;
        if (shift)
            result.rotation = std::round(result.rotation / 15) * 15;
        break;
    }
    case Kind::resize: {
        const QPointF handle = LayerTransform::handles[mode.index];
        const QPointF anchorUnit = option ? QPointF(0.5, 0.5) : QPointF(1 - handle.x(), 1 - handle.y());
        const QPointF anchor = original.point(anchorUnit);
        const QPointF initialHandle = original.point(handle);
        const double dx = initialHandle.x() + to.x() - start.x() - anchor.x();
        const double dy = initialHandle.y() + to.y() - start.y() - anchor.y();
        const double span = option ? 2 : 1;
        const double localX = (dx * std::cos(radians) + dy * std::sin(radians)) * span;
        const double localY = (-dx * std::sin(radians) + dy * std::cos(radians)) * span;
        const double sx = handle.x() * 2 - 1, sy = handle.y() * 2 - 1;
        const double originalWidth = original.size.width(), originalHeight = original.size.height();
        const double rawWidth = sx == 0 ? originalWidth : localX * sx;
        const double rawHeight = sy == 0 ? originalHeight : localY * sy;
        const bool mirroredX = rawWidth < 0, mirroredY = rawHeight < 0;
        double width = std::max(1.0, std::abs(rawWidth));
        double height = std::max(1.0, std::abs(rawHeight));
        if (lockRatio != shift) {
            double factor;
            if (sx == 0)
                factor = height / originalHeight;
            else if (sy == 0)
                factor = width / originalWidth;
            else
                factor = std::max(1 / std::min(originalWidth, originalHeight),
                                  (localX * sx * originalWidth + localY * sy * originalHeight)
                                      / (originalWidth * originalWidth + originalHeight * originalHeight));
            width = originalWidth * factor;
            height = originalHeight * factor;
        }
        result.size = {width, height};
        if (mirroredX)
            result.flipX = !result.flipX;
        if (mirroredY)
            result.flipY = !result.flipY;
        const double offsetX = (0.5 - anchorUnit.x()) * width * (mirroredX ? -1 : 1);
        const double offsetY = (0.5 - anchorUnit.y()) * height * (mirroredY ? -1 : 1);
        const QPointF center(anchor.x() + offsetX * std::cos(radians) - offsetY * std::sin(radians),
                             anchor.y() + offsetX * std::sin(radians) + offsetY * std::cos(radians));
        result.origin = {center.x() - width / 2, center.y() - height / 2};
        break;
    }
    }
    return result.isValid() ? result : original;
}

TransformSnap::Offset TransformSnap::offset(const QRectF &box, const std::vector<double> &xs,
                                            const std::vector<double> &ys, double tolerance)
{
    const Shift horizontal = shift({box.left(), box.center().x(), box.right()}, xs, tolerance);
    const Shift vertical = shift({box.top(), box.center().y(), box.bottom()}, ys, tolerance);
    return {QSizeF(horizontal.move, vertical.move), horizontal.target, vertical.target};
}
