#include "IO/RawImporter.h"
#include "IO/ImageExporter.h"
#include "IO/ImageImporter.h"
#include "Logging.h"
#include "Rendering/PoolMap.h"
#include <QColorSpace>
#include <QFile>
#include <QMimeDatabase>
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <libraw.h>

namespace {
[[noreturn]] void refuse(const QString &path, const char *step, int code)
{
    qCWarning(lcIO).noquote() << "cannot develop" << path << step << libraw_strerror(code);
    throw ImageImportError(ImageImportError::Kind::unreadable);
}

// The file opened and identified; unpacked when asked.
std::shared_ptr<LibRaw> opened(const QString &path, bool unpack)
{
    auto raw = std::make_shared<LibRaw>();
    if (const int code = raw->open_file(QFile::encodeName(path).constData()); code != LIBRAW_SUCCESS)
        refuse(path, "open", code);
    if (unpack)
        if (const int code = raw->unpack(); code != LIBRAW_SUCCESS)
            refuse(path, "unpack", code);
    return raw;
}

QPointF uv(QPointF xy)
{
    const double d = -2 * xy.x() + 12 * xy.y() + 3;
    return QPointF(4 * xy.x() / d, 6 * xy.y() / d);
}

QPointF xyOf(QPointF uv)
{
    const double d = 2 * uv.x() - 8 * uv.y() + 4;
    return QPointF(3 * uv.x() / d, 2 * uv.y() / d);
}

// The locus's unit normal in uv, pointing greener.
QPointF normal(double kelvin)
{
    const QPointF tangent = uv(RawImporter::planckian(kelvin + 1)) - uv(RawImporter::planckian(kelvin - 1));
    const double length = std::hypot(tangent.x(), tangent.y());
    QPointF up(-tangent.y() / length, tangent.x() / length);
    return up.y() < 0 ? -up : up;
}

using Matrix = std::array<std::array<double, 3>, 3>;

// XYZ and linear sRGB under D65, both ways.
constexpr Matrix xyzToSrgb{{{3.2404542, -1.5371385, -0.4985314}, {-0.9692660, 1.8760108, 0.0415560}, {0.0556434, -0.2040259, 1.0572252}}};
constexpr Matrix srgbToXyz{{{0.4124564, 0.3575761, 0.1804375}, {0.2126729, 0.7151522, 0.0721750}, {0.0193339, 0.1191920, 0.9503041}}};

std::array<double, 3> times(const Matrix &m, const std::array<double, 3> &v)
{
    std::array<double, 3> out{};
    for (size_t row = 0; row < 3; ++row)
        out[row] = m[row][0] * v[0] + m[row][1] * v[1] + m[row][2] * v[2];
    return out;
}

Matrix inverse(const Matrix &m)
{
    const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
        + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    Matrix out;
    for (size_t i = 0; i < 3; ++i)
        for (size_t j = 0; j < 3; ++j) {
            const size_t r0 = (j + 1) % 3, r1 = (j + 2) % 3, c0 = (i + 1) % 3, c1 = (i + 2) % 3;
            out[i][j] = (m[r0][c0] * m[r1][c1] - m[r0][c1] * m[r1][c0]) / det;
        }
    return out;
}

// LibRaw's camera → sRGB matrix, filled for every camera.
Matrix rgbCam(const libraw_colordata_t &color)
{
    Matrix m;
    for (size_t row = 0; row < 3; ++row)
        for (size_t column = 0; column < 3; ++column)
            m[row][column] = color.rgb_cam[row][column];
    return m;
}

// The raw camera response to a white: dcraw's matrices undone.
std::array<double, 3> response(const libraw_colordata_t &color, QPointF xy)
{
    const std::array<double, 3> xyz{xy.x() / xy.y(), 1, (1 - xy.x() - xy.y()) / xy.y()};
    std::array<double, 3> camera = times(inverse(rgbCam(color)), times(xyzToSrgb, xyz));
    for (size_t channel = 0; channel < 3; ++channel)
        camera[channel] /= color.pre_mul[channel];
    return camera;
}

// The white a raw camera neutral stands for.
QPointF whiteOf(const libraw_colordata_t &color, std::array<double, 3> neutral)
{
    for (size_t channel = 0; channel < 3; ++channel)
        neutral[channel] *= color.pre_mul[channel];
    const std::array<double, 3> xyz = times(srgbToXyz, times(rgbCam(color), neutral));
    const double sum = xyz[0] + xyz[1] + xyz[2];
    return QPointF(xyz[0] / sum, xyz[1] / sum);
}

// Three colours only: dcraw's matrices hold no fourth to invert.
void requireThreeColours(const LibRaw &raw, const QString &path)
{
    if (raw.imgdata.idata.colors == 3)
        return;
    qCWarning(lcIO).noquote() << "cannot develop" << path << "a sensor of" << raw.imgdata.idata.colors << "colours";
    throw ImageImportError(ImageImportError::Kind::unsupported);
}

// Green-anchored multipliers; the smallest, which LibRaw divides out.
double setWhite(LibRaw &raw, const QString &path, const RawDevelopSettings &settings)
{
    // A process overwrites `color`; unpack's copy stays the camera's.
    const std::array<double, 3> camera = response(raw.imgdata.rawdata.color, RawImporter::whitePoint(settings.temperature, settings.tint));
    if (*std::min_element(camera.begin(), camera.end()) <= 0) {
        qCWarning(lcIO).noquote() << "cannot develop" << path << "at" << settings.temperature << "K, tint" << settings.tint << ": the camera sees no such white";
        throw ImageImportError(ImageImportError::Kind::whiteBalance);
    }
    float *const multipliers = raw.imgdata.params.user_mul;
    for (size_t channel = 0; channel < 3; ++channel)
        multipliers[channel] = float(camera[1] / camera[channel]);
    return *std::min_element(multipliers, multipliers + 3);
}

double srgb(double linear)
{
    return linear <= 0.0031308 ? 12.92 * linear : 1.055 * std::pow(linear, 1 / 2.4) - 0.055;
}

// LibRaw's default curve: BT.709, gamma 0.45 and slope 4.5.
double bt709(double linear)
{
    return linear < 0.018 ? 4.5 * linear : 1.099 * std::pow(linear, 0.45) - 0.099;
}

QImage render(LibRaw &raw, const QString &path, const RawDevelopSettings &settings, std::optional<double> limit)
{
    requireThreeColours(raw, path);
    libraw_output_params_t &params = raw.imgdata.params;
    // Linear sRGB in 16 bits: a power of one.
    params.output_color = 1;
    params.output_bps = 16;
    params.gamm[0] = 1;
    params.no_auto_bright = 1;
    params.adjust_maximum_thr = 0;
    params.use_camera_wb = 0;
    params.use_auto_wb = 0;
    // LibRaw clips at white in camera space: highlights stay neutral.
    const double smallest = setWhite(raw, path, settings);
    // A half-size process leaves `sizes` halved; unpack's copy stays.
    const int longest = std::max(raw.imgdata.rawdata.sizes.width, raw.imgdata.rawdata.sizes.height);
    params.half_size = limit && longest > *limit ? 1 : 0;
    if (const int code = raw.dcraw_process(); code != LIBRAW_SUCCESS)
        refuse(path, "process", code);
    int code = LIBRAW_SUCCESS;
    const std::unique_ptr<libraw_processed_image_t, void (*)(libraw_processed_image_t *)> made(raw.dcraw_make_mem_image(&code), LibRaw::dcraw_clear_mem);
    if (!made)
        refuse(path, "output", code);
    QImage image(made->width, made->height, QImage::Format_RGBA8888_Premultiplied);
    if (image.isNull())
        throw ExportError(ExportError::Kind::render);
    image.setColorSpace(QColorSpace::SRgb);
    const double gain = std::pow(2.0, double(settings.exposure)) * smallest;
    const auto *samples = reinterpret_cast<const quint16 *>(made->data);
    uchar *const bits = image.bits();
    const qsizetype stride = image.bytesPerLine();
    std::vector<int> rows(size_t(image.height()));
    std::iota(rows.begin(), rows.end(), 0);
    PoolMap::blocking(rows, [&](int y) {
        for (int x = 0; x < image.width(); ++x) {
            uchar *out = bits + y * stride + x * 4;
            for (int channel = 0; channel < 3; ++channel) {
                const double linear = std::min(samples[(qsizetype(y) * image.width() + x) * 3 + channel] / 65535.0 * gain, 1.0);
                const double flat = srgb(linear);
                out[channel] = uchar(std::lround(255 * (flat + settings.boost * (bt709(linear) - flat))));
            }
            out[3] = 255;
        }
    });
    if (!limit || std::max(image.width(), image.height()) <= *limit)
        return image;
    const QImage fitted = image.scaled(QSize(int(*limit), int(*limit)), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (fitted.isNull())
        throw ExportError(ExportError::Kind::render);
    return fitted;
}
}

bool RawDevelopSettings::isAsShot() const
{
    return exposure == 0 && boost == 1 && temperature == asShotTemperature && tint == asShotTint;
}

void RawDevelopSettings::reset()
{
    exposure = 0;
    boost = 1;
    temperature = asShotTemperature;
    tint = asShotTint;
}

bool RawImporter::matches(const QString &path)
{
    return QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchExtension).inherits(QStringLiteral("image/x-dcraw"));
}

bool RawImporter::isRawType(const QString &mimeName)
{
    return QMimeDatabase().mimeTypeForName(mimeName).inherits(QStringLiteral("image/x-dcraw"));
}

QStringList RawImporter::globs()
{
    QStringList globs;
    for (const QMimeType &type : QMimeDatabase().allMimeTypes())
        if (type.inherits(QStringLiteral("image/x-dcraw")))
            globs << type.globPatterns();
    globs.sort();
    return globs;
}

QPointF RawImporter::planckian(double kelvin)
{
    const double t = std::clamp(kelvin, 1667.0, 25000.0);
    const double x = t <= 4000 ? -0.2661239e9 / (t * t * t) - 0.2343589e6 / (t * t) + 0.8776956e3 / t + 0.179910
                               : -3.0258469e9 / (t * t * t) + 2.1070379e6 / (t * t) + 0.2226347e3 / t + 0.240390;
    const double y = t <= 2222   ? -1.1063814 * x * x * x - 1.34811020 * x * x + 2.18555832 * x - 0.20219683
                     : t <= 4000 ? -0.9549476 * x * x * x - 1.37418593 * x * x + 2.09137015 * x - 0.16748867
                                 : 3.0817580 * x * x * x - 5.87338670 * x * x + 3.75112997 * x - 0.37001483;
    return QPointF(x, y);
}

QPointF RawImporter::whitePoint(double kelvin, double tint)
{
    // Adobe's tint: 3000 per Duv, positive a greener white.
    return xyOf(uv(planckian(kelvin)) + normal(kelvin) * (tint / 3000));
}

std::pair<double, double> RawImporter::temperatureOf(QPointF xy)
{
    const QPointF target = uv(xy);
    const auto distance = [&](double mired) {
        const QPointF on = uv(planckian(1e6 / mired));
        return std::hypot(target.x() - on.x(), target.y() - on.y());
    };
    // Coarse steps in mireds, then golden sections round the best.
    double best = 40;
    for (double mired = 40; mired <= 600; mired += 1)
        if (distance(mired) < distance(best))
            best = mired;
    double low = best - 1, high = best + 1;
    const double ratio = (std::sqrt(5.0) - 1) / 2;
    for (int step = 0; step < 60; ++step) {
        const double a = high - ratio * (high - low), b = low + ratio * (high - low);
        if (distance(a) < distance(b))
            high = b;
        else
            low = a;
    }
    const double kelvin = 1e6 / ((low + high) / 2);
    const QPointF offset = target - uv(planckian(kelvin));
    const QPointF up = normal(kelvin);
    return {kelvin, 3000 * (offset.x() * up.x() + offset.y() * up.y())};
}

std::optional<RawDevelopSettings> RawImporter::asShot(const QString &path)
{
    try {
        const std::shared_ptr<LibRaw> raw = opened(path, false);
        const libraw_colordata_t &color = raw->imgdata.color;
        requireThreeColours(*raw, path);
        // Without the camera's reading, dcraw's daylight.
        const float *shot = color.cam_mul[0] > 0 ? color.cam_mul : color.pre_mul;
        const auto [kelvin, tint] = temperatureOf(whiteOf(color, {1.0 / shot[0], 1.0 / shot[1], 1.0 / shot[2]}));
        RawDevelopSettings settings;
        settings.temperature = settings.asShotTemperature = float(kelvin);
        settings.tint = settings.asShotTint = float(tint);
        return settings;
    } catch (const ImageImportError &) {
        return std::nullopt;
    }
}

QImage RawImporter::develop(const QString &path, const RawDevelopSettings &settings, std::optional<double> limit)
{
    const std::shared_ptr<LibRaw> raw = opened(path, true);
    return render(*raw, path, settings, limit);
}

std::optional<QSize> RawImporter::pixelSize(const QString &path)
{
    try {
        const std::shared_ptr<LibRaw> raw = opened(path, false);
        // It fails only when called before the file is identified.
        raw->adjust_sizes_info_only();
        return QSize(raw->imgdata.sizes.iwidth, raw->imgdata.sizes.iheight);
    } catch (const ImageImportError &) {
        return std::nullopt;
    }
}

RawImporter::Queue &RawImporter::Queue::shared()
{
    static Queue queue;
    return queue;
}

QImage RawImporter::Queue::develop(const QString &path, const RawDevelopSettings &settings, std::optional<double> limit)
{
    const std::lock_guard lock(m_mutex);
    if (!limit)
        return RawImporter::develop(path, settings, std::nullopt);
    // One file's unpacked frame is kept; a move only processes.
    if (!m_cached || m_path != path) {
        m_cached = opened(path, true);
        m_path = path;
    }
    return render(*m_cached, path, settings, limit);
}

void RawImporter::Queue::release()
{
    const std::lock_guard lock(m_mutex);
    m_cached.reset();
}
