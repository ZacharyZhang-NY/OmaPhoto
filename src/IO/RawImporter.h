#pragma once
#include <QImage>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <memory>
#include <mutex>
#include <optional>

// What the camera recorded, before the look was decided.
struct RawDevelopSettings {
    // Stops of exposure either side of what was recorded.
    float exposure = 0;
    // White balance in Kelvin, from the camera's own reading.
    float temperature = 5000;
    // Green–magenta balance, from the camera's own reading.
    float tint = 0;
    // The tone curve: 1 LibRaw's full curve, 0 flat.
    float boost = 1;
    // The camera's own choice, where Reset returns.
    float asShotTemperature = 5000;
    float asShotTint = 0;

    bool isAsShot() const;
    void reset();
    friend bool operator==(const RawDevelopSettings &, const RawDevelopSettings &) = default;
};

// Swift's tuple: the file under the sheet, where it starts.
struct RawDevelopRequest {
    QString path;
    RawDevelopSettings settings;
};

class LibRaw;

// Swift's RawImporter: camera RAW developed through LibRaw.
namespace RawImporter {
// Every camera RAW the system names, by extension.
bool matches(const QString &path);
// The same by MIME name, for dropped pictures.
bool isRawType(const QString &mimeName);
// Their file patterns, for the importer's filter.
QStringList globs();
// The camera's white balance, where the sliders start.
std::optional<RawDevelopSettings> asShot(const QString &path);
// The developed image; `limit` caps the preview's long edge.
QImage develop(const QString &path, const RawDevelopSettings &settings, std::optional<double> limit = std::nullopt);
// The frame's size, oriented, without developing it.
std::optional<QSize> pixelSize(const QString &path);
// Planckian chromaticity, Kim et al., 1667–25000 K.
QPointF planckian(double kelvin);
// A white of this temperature and tint, CIE xy.
QPointF whitePoint(double kelvin, double tint);
// The temperature and tint of a white, the inverse.
std::pair<double, double> temperatureOf(QPointF xy);

// Swift's actor: one develop at a time, decode kept.
class Queue {
public:
    static Queue &shared();
    QImage develop(const QString &path, const RawDevelopSettings &settings, std::optional<double> limit);
    // Lets go of the decoded frame when the sheet closes.
    void release();

private:
    std::mutex m_mutex;
    QString m_path;
    std::shared_ptr<LibRaw> m_cached;
};
}
