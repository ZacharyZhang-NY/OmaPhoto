#include "Document/CameraRawGeometryCalibration.h"
#include "Document/BrushStroke.h"
#include "Document/CameraRaw.h"
#include "Document/ImageAdjustments.h"
#include "IO/ImageExporter.h"
#include <QPainter>
#include <QPolygonF>
#include <QTransform>
#include <cmath>
#include <numbers>
extern "C" {
#include "AdjustPixels.h"
#include "BrushPixels.h"
}

using ImageAdjustmentPixels::clamp;

namespace {
bool readable(const CameraRawGeometryGuide &guide)
{
    return std::hypot(guide.endX - guide.startX, guide.endY - guide.startY) > 0.01;
}

// Swift's guidedCorrections: the first line levels, the second tilts.
std::array<double, 3> guided(const std::vector<CameraRawGeometryGuide> &guides)
{
    if (guides.empty())
        return {0, 0, 0};
    const CameraRawGeometryGuide &first = guides.front();
    const double dx = first.endX - first.startX, dy = first.endY - first.startY;
    if (std::hypot(dx, dy) <= 1e-4)
        return {0, 0, 0};
    double rotate = -std::atan2(dy, dx) * 180 / std::numbers::pi;
    if (rotate > 45)
        rotate -= 90;
    else if (rotate < -45)
        rotate += 90;
    double vertical = 0, horizontal = 0;
    if (guides.size() > 1) {
        const CameraRawGeometryGuide &second = guides[1];
        const double sx = second.endX - second.startX, sy = second.endY - second.startY;
        if (std::hypot(sx, sy) > 1e-4) {
            const double angle = std::atan2(sy, sx) * 180 / std::numbers::pi;
            vertical = std::abs(angle) > 45 ? (angle > 0 ? 25 : -25) : 0;
            horizontal = std::abs(angle) <= 45 ? (angle > 0 ? 25 : -25) : 0;
        }
    }
    return {vertical, horizontal, rotate};
}
}

bool CameraRawGeometrySettings::usesGuides() const
{
    return upright == CameraRawUprightMode::guided && std::any_of(guides.begin(), guides.end(), readable);
}

bool CameraRawGeometrySettings::adjusts() const
{
    return usesGuides() || vertical != 0 || horizontal != 0 || rotate != 0 || aspect != 0 || scale != 0 || offsetX != 0 || offsetY != 0;
}

CameraRawGeometrySettings CameraRawGeometrySettings::normalized() const
{
    CameraRawGeometrySettings result = *this;
    result.vertical = clamp(vertical, -100, 100, 0);
    result.horizontal = clamp(horizontal, -100, 100, 0);
    result.rotate = clamp(rotate, -45, 45, 0);
    result.aspect = clamp(aspect, -100, 100, 0);
    result.scale = clamp(scale, -100, 100, 0);
    result.offsetX = clamp(offsetX, -100, 100, 0);
    result.offsetY = clamp(offsetY, -100, 100, 0);
    result.guides.clear();
    std::copy_if(guides.begin(), guides.end(), std::back_inserter(result.guides), readable);
    return result;
}

std::array<double, 3> CameraRawGeometrySettings::effectiveCorrections() const
{
    if (upright == CameraRawUprightMode::off)
        return {vertical, horizontal, rotate};
    const std::array<double, 3> extra = guided(guides);
    return {vertical + extra[0], horizontal + extra[1], rotate + extra[2]};
}

// Core Image's corners, y measured up from the bottom.
std::array<QPointF, 4> CameraRawGeometrySettings::outputCorners(int width, int height, double vertical, double horizontal, double rotation) const
{
    const double w = width, h = height;
    const double strength = projection == CameraRawProjection::perspective ? 1.0 : 0.55;
    const double v = vertical / 100 * w * 0.18 * strength, hz = horizontal / 100 * h * 0.18 * strength;
    const double aspectScale = 1 + aspect / 200, zoom = 1 + scale / 100;
    const double shiftX = offsetX / 100 * w * 0.15, shiftY = offsetY / 100 * h * 0.15;
    std::array<QPointF, 4> corners{QPointF(-v + shiftX, h + shiftY), QPointF(w + v + shiftX, h + shiftY), QPointF(w + hz + shiftX, -shiftY),
                                   QPointF(-hz + shiftX, -shiftY)};
    const QPointF center(w / 2 + shiftX, h / 2 + shiftY);
    const double radians = rotation * std::numbers::pi / 180, cosine = std::cos(radians), sine = std::sin(radians);
    for (QPointF &corner : corners) {
        const double dx = corner.x() - center.x(), dy = corner.y() - center.y();
        corner = QPointF(center.x() + dx * cosine - dy * sine, center.y() + dx * sine + dy * cosine);
    }
    if (aspectScale != 1)
        for (QPointF &corner : corners)
            corner = QPointF(center.x() + (corner.x() - center.x()) * aspectScale, center.y() + (corner.y() - center.y()) / aspectScale);
    if (zoom != 1)
        for (QPointF &corner : corners)
            corner = center + (corner - center) * zoom;
    return corners;
}

QImage CameraRawGeometrySettings::apply(const QImage &image) const
{
    const CameraRawGeometrySettings settings = normalized();
    const int width = image.width(), height = image.height();
    if (!settings.adjusts() || width <= 0 || height <= 0)
        return image;
    const auto [vertical, horizontal, rotate] = settings.effectiveCorrections();
    const std::array<QPointF, 4> corners = settings.outputCorners(width, height, vertical, horizontal, rotate);
    // Core Image counts y upward: top-down, a corner's y flips.
    QPolygonF target;
    for (const QPointF &corner : corners)
        target << QPointF(corner.x(), height - corner.y());
    QTransform warp;
    // Normalized settings keep the quad whole; else a bug.
    if (!QTransform::quadToQuad(QPolygonF(QRectF(0, 0, width, height)).mid(0, 4), target, warp))
        throw std::logic_error("the geometry's corners fold");
    QImage result = BrushRaster::context(width, height, false);
    {
        QPainter painter(&result);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        painter.setTransform(warp);
        painter.drawImage(QRectF(0, 0, width, height), image);
    }
    if (!settings.constrainCrop)
        return result;
    std::array<size_t, 4> edges{};
    brush_alpha_bounds(result.constBits(), size_t(width), size_t(height), size_t(result.bytesPerLine()), edges.data());
    const QRect crop(int(edges[0]), int(edges[1]), int(edges[2] - edges[0]), int(edges[3] - edges[1]));
    if (crop.width() < 1 || crop.height() < 1 || (crop.width() >= width && crop.height() >= height))
        return result;
    const QImage cropped = result.copy(crop);
    if (cropped.isNull())
        throw ExportError(ExportError::Kind::render);
    QImage fitted = BrushRaster::context(width, height, false);
    const double fit = std::min(double(width) / crop.width(), double(height) / crop.height());
    QPainter painter(&fitted);
    BrushRaster::draw(cropped, QRectF((width - crop.width() * fit) / 2, (height - crop.height() * fit) / 2, crop.width() * fit, crop.height() * fit),
                      painter);
    return fitted;
}

QString rawValue(CameraRawProcessVersion version)
{
    return QStringLiteral("Version %1").arg(int(version) + 1);
}

QString summary(CameraRawProcessVersion version)
{
    switch (version) {
    case CameraRawProcessVersion::version1:
        return QStringLiteral("Earliest response. Hue, saturation, and shadow tint move about half as far as Version 6.");
    case CameraRawProcessVersion::version2:
        return QStringLiteral("A little stronger than Version 1. The sliders below still fall well short of the current look.");
    case CameraRawProcessVersion::version3:
        return QStringLiteral("Firmer color than Version 2. Primary shifts stay gentler than the current process.");
    case CameraRawProcessVersion::version4:
        return QStringLiteral("The 2012 response. Calibration reaches most of the strength used by Version 6.");
    case CameraRawProcessVersion::version5:
        return QStringLiteral("Close to the current process, with slightly softer primary and shadow shifts.");
    case CameraRawProcessVersion::version6:
        return QStringLiteral("Current default. The calibration sliders below apply at full strength.");
    }
    throw std::logic_error("unknown process version");
}

bool CameraRawCalibrationSettings::adjusts() const
{
    return shadowTint != 0 || redHue != 0 || redSaturation != 0 || greenHue != 0 || greenSaturation != 0 || blueHue != 0 || blueSaturation != 0;
}

CameraRawCalibrationSettings CameraRawCalibrationSettings::normalized() const
{
    return {process,
            clamp(shadowTint, -100, 100, 0),
            clamp(redHue, -100, 100, 0),
            clamp(redSaturation, -100, 100, 0),
            clamp(greenHue, -100, 100, 0),
            clamp(greenSaturation, -100, 100, 0),
            clamp(blueHue, -100, 100, 0),
            clamp(blueSaturation, -100, 100, 0)};
}

void CameraRawSettings::applyCalibration(uchar *pixels, int width, int height, qsizetype stride) const
{
    const CameraRawCalibrationSettings primaries = calibration.normalized();
    if (!primaries.adjusts())
        return;
    adjust_camera_raw_calibration(pixels, size_t(width), size_t(height), size_t(stride), primaries.shadowTint, primaries.redHue,
                                  primaries.redSaturation, primaries.greenHue, primaries.greenSaturation, primaries.blueHue,
                                  primaries.blueSaturation, int(primaries.process) + 1);
}
