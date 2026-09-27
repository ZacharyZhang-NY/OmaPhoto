#pragma once
#include <QImage>
#include <QPointF>
#include <QString>
#include <array>
#include <vector>

// Swift's CameraRawGeometryCalibration.swift: Geometry, Calibration.
enum class CameraRawUprightMode { off, guided };
enum class CameraRawProjection { perspective, rectilinear };

// A guide line, 0 to 1 from the lower left.
struct CameraRawGeometryGuide {
    double startX = 0;
    double startY = 0;
    double endX = 0;
    double endY = 0;
    friend bool operator==(const CameraRawGeometryGuide &, const CameraRawGeometryGuide &) = default;
};

struct CameraRawGeometrySettings {
    CameraRawUprightMode upright = CameraRawUprightMode::off;
    CameraRawProjection projection = CameraRawProjection::perspective;
    double vertical = 0;
    double horizontal = 0;
    double rotate = 0;
    double aspect = 0;
    double scale = 0;
    double offsetX = 0;
    double offsetY = 0;
    bool constrainCrop = false;
    std::vector<CameraRawGeometryGuide> guides{};
    bool adjusts() const;
    CameraRawGeometrySettings normalized() const;
    // Perspective and turn; Constrain Crop trims empty edges.
    QImage apply(const QImage &image) const;
    friend bool operator==(const CameraRawGeometrySettings &, const CameraRawGeometrySettings &) = default;

private:
    bool usesGuides() const;
    std::array<double, 3> effectiveCorrections() const;
    std::array<QPointF, 4> outputCorners(int width, int height, double vertical, double horizontal, double rotation) const;
};

enum class CameraRawProcessVersion { version1, version2, version3, version4, version5, version6 };
QString rawValue(CameraRawProcessVersion version);
// What this process does to the calibration sliders.
QString summary(CameraRawProcessVersion version);

struct CameraRawCalibrationSettings {
    CameraRawProcessVersion process = CameraRawProcessVersion::version6;
    double shadowTint = 0;
    double redHue = 0;
    double redSaturation = 0;
    double greenHue = 0;
    double greenSaturation = 0;
    double blueHue = 0;
    double blueSaturation = 0;
    bool adjusts() const;
    CameraRawCalibrationSettings normalized() const;
    friend bool operator==(const CameraRawCalibrationSettings &, const CameraRawCalibrationSettings &) = default;
};
