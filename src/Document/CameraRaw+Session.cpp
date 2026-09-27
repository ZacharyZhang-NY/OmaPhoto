#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include <QFutureWatcher>
#include <QPainter>
#include <QtConcurrent>
#include <cmath>

// Swift's CameraRaw.swift session extension: samples, drags, guides.
namespace {
// One pixel's premultiplied bytes, or none past the image.
std::optional<std::array<int, 4>> pixelAt(const QImage &image, QPointF pixel)
{
    if (!(pixel.x() >= 0 && pixel.y() >= 0 && pixel.x() < image.width() && pixel.y() < image.height()))
        return std::nullopt;
    QImage one = BrushRaster::context(1, 1, false);
    {
        QPainter painter(&one);
        BrushRaster::draw(image, QRectF(0, 0, 1, 1), painter, QRectF(std::floor(pixel.x()), std::floor(pixel.y()), 1, 1));
    }
    const uchar *bytes = one.constBits();
    return std::array<int, 4>{bytes[0], bytes[1], bytes[2], bytes[3]};
}

std::array<double, 3> straight(const std::array<int, 4> &bytes)
{
    return {std::min(1.0, bytes[0] / double(bytes[3])), std::min(1.0, bytes[1] / double(bytes[3])), std::min(1.0, bytes[2] / double(bytes[3]))};
}

double hueDegrees(double red, double green, double blue)
{
    const double high = std::max({red, green, blue}), low = std::min({red, green, blue}), chroma = high - low;
    if (!(chroma > 1e-6))
        return 0;
    const double hue = high == red ? (green - blue) / chroma : high == green ? 2 + (blue - red) / chroma : 4 + (red - green) / chroma;
    const double degrees = hue * 60;
    return degrees < 0 ? degrees + 360 : degrees;
}
}

void EditorSession::setCameraRawPanel(const CameraRawPanel &panel)
{
    if (!m_filterEdit)
        return;
    m_filterEdit->rawPanel = panel;
    notify();
}

void EditorSession::sampleCameraRawWhiteBalance(QPointF point)
{
    if (!m_filterEdit || m_filterEdit->kind != FilterKind::cameraRaw || !m_filterEdit->rawPanel.samplesWhiteBalance || m_filterEdit->committing
        || !m_document || !(point.x() >= 0 && point.y() >= 0 && point.x() < m_document->size().width() && point.y() < m_document->size().height()))
        return;
    const FilterEdit &edit = *m_filterEdit;
    try {
        const std::optional<std::array<int, 4>> bytes = pixelAt(edit.original.image(), edit.mapping.inverted().map(point));
        if (!bytes || (*bytes)[3] == 0)
            return;
        const std::array<double, 3> rgb = straight(*bytes);
        const std::optional<CameraRawSettings::Balance> solved = CameraRawSettings::neutralizeStraight(rgb[0], rgb[1], rgb[2]);
        if (!solved)
            return;
        FilterSettings settings = edit.settings;
        settings.cameraRaw.temperature = solved->temperature;
        settings.cameraRaw.tint = solved->tint;
        settings.cameraRaw.whiteBalance = CameraRawWhiteBalance::custom;
        updateFilter(settings, edit.preview);
    } catch (const ExportError &error) {
        setBrushError(QString::fromUtf8(error.what()));
    }
}

void EditorSession::applyCameraRawAutoWhiteBalance(std::function<void()> done)
{
    const auto finish = [this, done] {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
    };
    if (!m_filterEdit || m_filterEdit->kind != FilterKind::cameraRaw || m_filterEdit->committing) {
        finish();
        return;
    }
    FilterSettings settings = m_filterEdit->settings;
    settings.cameraRaw.whiteBalance = CameraRawWhiteBalance::automatic;
    updateFilter(settings, m_filterEdit->preview);
    // A worker takes the average; it lands if Auto stands.
    auto *watcher = new QFutureWatcher<std::optional<CameraRawSettings::Balance>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, id = m_filterEdit->id, finish] {
        watcher->deleteLater();
        const std::optional<CameraRawSettings::Balance> solved = watcher->result();
        if (solved && m_filterEdit && m_filterEdit->id == id && !m_filterEdit->committing
            && m_filterEdit->settings.cameraRaw.whiteBalance == CameraRawWhiteBalance::automatic) {
            FilterSettings value = m_filterEdit->settings;
            value.cameraRaw.temperature = solved->temperature;
            value.cameraRaw.tint = solved->tint;
            updateFilter(value, m_filterEdit->preview);
        }
        finish();
    });
    // A painted layer flattens here; a failure scans nothing.
    watcher->setFuture(QtConcurrent::run([original = m_filterEdit->original]() -> std::optional<CameraRawSettings::Balance> {
        try {
            return CameraRawSettings::autoBalance(original.image());
        } catch (const ExportError &error) {
            qCWarning(lcApp) << "Camera Raw's Auto found no memory:" << error.what();
            return std::nullopt;
        }
    }));
}

void EditorSession::sampleCameraRawDefringe(QPointF point)
{
    if (!m_filterEdit || m_filterEdit->kind != FilterKind::cameraRaw || !m_filterEdit->rawPanel.samplesDefringe || m_filterEdit->committing
        || !m_document || !(point.x() >= 0 && point.y() >= 0 && point.x() < m_document->size().width() && point.y() < m_document->size().height()))
        return;
    const FilterEdit &edit = *m_filterEdit;
    try {
        const std::optional<std::array<int, 4>> bytes = pixelAt(edit.original.image(), edit.mapping.inverted().map(point));
        if (!bytes || (*bytes)[3] == 0)
            return;
        const std::array<double, 3> rgb = straight(*bytes);
        const double hue = hueDegrees(rgb[0], rgb[1], rgb[2]);
        FilterSettings settings = edit.settings;
        CameraRawOpticsSettings &optics = settings.cameraRaw.optics;
        const double purpleCenter = 290, greenCenter = 90, span = 25;
        if (std::abs(hue - purpleCenter) < std::abs(hue - greenCenter)) {
            optics.purpleHueLow = hue - span;
            optics.purpleHueHigh = hue + span;
            if (optics.purpleAmount == 0)
                optics.purpleAmount = 50;
        } else {
            optics.greenHueLow = hue - span;
            optics.greenHueHigh = hue + span;
            if (optics.greenAmount == 0)
                optics.greenAmount = 50;
        }
        updateFilter(settings, edit.preview);
    } catch (const ExportError &error) {
        setBrushError(QString::fromUtf8(error.what()));
    }
}

void EditorSession::updateCameraRawReadout(QPointF point)
{
    if (!m_filterEdit || m_filterEdit->kind != FilterKind::cameraRaw)
        return;
    FilterEdit &edit = *m_filterEdit;
    const QImage &image = edit.preparedPreview ? *edit.preparedPreview : edit.previewSource;
    std::optional<std::array<int, 3>> readout;
    try {
        const std::optional<std::array<int, 4>> bytes = pixelAt(image, edit.previewMapping.inverted().map(point));
        if (bytes && (*bytes)[3] > 0) {
            const int alpha = (*bytes)[3];
            readout = std::array<int, 3>{};
            for (size_t channel = 0; channel < 3; ++channel)
                (*readout)[channel] = std::min(255, ((*bytes)[channel] * 255 + alpha / 2) / alpha);
        }
    } catch (const ExportError &error) {
        // Swift's `try?`: no memory keeps the last readout.
        qCWarning(lcApp) << "Camera Raw's readout found no memory:" << error.what();
        return;
    }
    if (edit.rawPanel.readout == readout)
        return;
    edit.rawPanel.readout = readout;
    notify();
}

void EditorSession::beginCameraRawDrag(QPointF point)
{
    if (!m_filterEdit || m_filterEdit->kind != FilterKind::cameraRaw)
        return;
    const std::optional<CameraRawSample> sample = cameraRawSample(point);
    if (!sample)
        return;
    m_filterEdit->rawPanel.drag = CameraRawDrag{point.y(), m_filterEdit->settings.cameraRaw, sample->tone, sample->hue};
    notify();
}

void EditorSession::dragCameraRaw(QPointF point)
{
    if (!m_filterEdit || !m_filterEdit->rawPanel.drag)
        return;
    const FilterEdit &edit = *m_filterEdit;
    const CameraRawDrag &drag = *edit.rawPanel.drag;
    const double delta = (drag.startY - point.y()) * 0.35;
    FilterSettings settings = edit.settings;
    settings.cameraRaw = drag.settings;
    const CameraRawPanel &panel = edit.rawPanel;
    if (panel.targetsCurve) {
        if (panel.curvePage == CameraRawCurvePage::parametric) {
            CameraRawCurveSettings before = drag.settings.curve;
            settings.cameraRaw.curve.region(drag.tone) = std::min(100.0, std::max(-100.0, before.region(drag.tone) + delta));
        } else {
            settings.cameraRaw.curve = drag.settings.curve.nudged(panel.pointChannel, drag.tone, delta / 100);
        }
    } else if (panel.targetsMixer) {
        const std::array<double, 8> weights = CameraRawMixerSettings::weights(drag.hue);
        std::array<double, 8> &target = panel.mixerTab == CameraRawMixerTab::hue          ? settings.cameraRaw.mixer.hue
                                      : panel.mixerTab == CameraRawMixerTab::saturation ? settings.cameraRaw.mixer.saturation
                                                                                        : settings.cameraRaw.mixer.luminance;
        const std::array<double, 8> &from = panel.mixerTab == CameraRawMixerTab::hue          ? drag.settings.mixer.hue
                                          : panel.mixerTab == CameraRawMixerTab::saturation ? drag.settings.mixer.saturation
                                                                                            : drag.settings.mixer.luminance;
        for (size_t index = 0; index < 8; ++index)
            if (weights[index] > 0)
                target[index] = std::min(100.0, std::max(-100.0, from[index] + delta * weights[index]));
    }
    updateFilter(settings, edit.preview);
}

void EditorSession::sampleCameraRawPointColor(QPointF point)
{
    if (!m_filterEdit || !m_filterEdit->rawPanel.samplesPointColor)
        return;
    const std::optional<CameraRawSample> sample = cameraRawSample(point);
    if (!sample)
        return;
    FilterEdit &edit = *m_filterEdit;
    FilterSettings settings = edit.settings;
    std::vector<CameraRawPointColor> &points = settings.cameraRaw.mixer.points;
    CameraRawPointColor color{.hue = sample->hue, .saturation = sample->saturation, .luminance = sample->luminance};
    const int index = edit.rawPanel.pointIndex;
    if (index >= 0 && size_t(index) < points.size()) {
        color.hueShift = points[size_t(index)].hueShift;
        color.saturationShift = points[size_t(index)].saturationShift;
        color.luminanceShift = points[size_t(index)].luminanceShift;
        points[size_t(index)] = color;
    } else if (points.size() < 8) {
        points.push_back(color);
        edit.rawPanel.pointIndex = int(points.size()) - 1;
    }
    updateFilter(settings, edit.preview);
}

void EditorSession::beginCameraRawGeometryGuide(QPointF point)
{
    if (!m_filterEdit || m_filterEdit->kind != FilterKind::cameraRaw || !m_filterEdit->rawPanel.drawingGeometryGuide)
        return;
    const std::optional<QPointF> normalized = cameraRawNormalizedPoint(point);
    if (!normalized)
        return;
    m_filterEdit->rawPanel.guideDraft = std::pair(*normalized, *normalized);
    notify();
}

void EditorSession::continueCameraRawGeometryGuide(QPointF point)
{
    if (!m_filterEdit || !m_filterEdit->rawPanel.drawingGeometryGuide || !m_filterEdit->rawPanel.guideDraft)
        return;
    const std::optional<QPointF> normalized = cameraRawNormalizedPoint(point);
    if (!normalized)
        return;
    m_filterEdit->rawPanel.guideDraft->second = *normalized;
    notify();
}

void EditorSession::commitCameraRawGeometryGuide()
{
    if (!m_filterEdit || !m_filterEdit->rawPanel.guideDraft)
        return;
    FilterEdit &edit = *m_filterEdit;
    const auto [start, end] = *std::exchange(edit.rawPanel.guideDraft, std::nullopt);
    FilterSettings settings = edit.settings;
    settings.cameraRaw.geometry.guides.push_back({start.x(), start.y(), end.x(), end.y()});
    settings.cameraRaw.geometry.upright = CameraRawUprightMode::guided;
    updateFilter(settings, edit.preview);
}

std::optional<QPointF> EditorSession::cameraRawNormalizedPoint(QPointF point) const
{
    if (!m_filterEdit)
        return std::nullopt;
    const QImage &image = m_filterEdit->previewSource;
    const QPointF pixel = m_filterEdit->previewMapping.inverted().map(point);
    if (!(pixel.x() >= 0 && pixel.y() >= 0 && pixel.x() < image.width() && pixel.y() < image.height()))
        return std::nullopt;
    return QPointF(pixel.x() / image.width(), pixel.y() / image.height());
}

std::optional<EditorSession::CameraRawSample> EditorSession::cameraRawSample(QPointF point) const
{
    if (!m_filterEdit)
        return std::nullopt;
    const QImage &image = m_filterEdit->preparedPreview ? *m_filterEdit->preparedPreview : m_filterEdit->previewSource;
    std::optional<std::array<int, 4>> bytes;
    try {
        bytes = pixelAt(image, m_filterEdit->previewMapping.inverted().map(point));
    } catch (const ExportError &error) {
        // Swift's `try?`: no memory, no sample.
        qCWarning(lcApp) << "Camera Raw's sample found no memory:" << error.what();
        return std::nullopt;
    }
    if (!bytes || (*bytes)[3] == 0)
        return std::nullopt;
    const auto [red, green, blue] = straight(*bytes);
    const double high = std::max({red, green, blue}), low = std::min({red, green, blue}), chroma = high - low;
    double hue = 0;
    if (chroma > 1e-6) {
        hue = (high == red ? (green - blue) / chroma : high == green ? 2 + (blue - red) / chroma : 4 + (red - green) / chroma) / 6;
        if (hue < 0)
            hue += 1;
    }
    return CameraRawSample{0.2126 * red + 0.7152 * green + 0.0722 * blue, hue * 360, high == 0 ? 0 : chroma / high, (high + low) / 2};
}
