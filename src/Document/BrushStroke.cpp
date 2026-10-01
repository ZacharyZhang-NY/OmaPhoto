#include "Document/BrushStroke.h"
#include "Rendering/PoolMap.h"
#include "IO/ImageExporter.h"
#include <QtConcurrent>
#include <cmath>
#include <cstring>
#include <numeric>
#include <stdexcept>

QString rawValue(SpotHealingMode mode)
{
    switch (mode) {
    case SpotHealingMode::contentAware:
        return QStringLiteral("Content-Aware");
    case SpotHealingMode::createTexture:
        return QStringLiteral("Create Texture");
    case SpotHealingMode::proximityMatch:
        return QStringLiteral("Proximity Match");
    }
    throw std::logic_error("unknown spot healing mode");
}

QImage BrushRaster::context(int width, int height, bool mask)
{
    QImage surface(width, height, mask ? QImage::Format_Grayscale8 : QImage::Format_RGBA8888_Premultiplied);
    if (surface.isNull())
        throw ExportError(ExportError::Kind::render);
    surface.fill(0);
    return surface;
}

QImage BrushRaster::copy(const QImage &image)
{
    QImage result = context(image.width(), image.height(), false);
    if (image.format() != QImage::Format_RGBA8888_Premultiplied) {
        QPainter painter(&result);
        draw(image, QRectF(0, 0, image.width(), image.height()), painter);
        return result;
    }
    for (int y = 0; y < image.height(); ++y)
        std::memcpy(result.scanLine(y), image.constScanLine(y), size_t(image.width()) * 4);
    return result;
}

void BrushRaster::inBands(qsizetype count, const std::function<void(qsizetype, qsizetype)> &body)
{
    const int bands = count < 250'000 ? 1 : QThread::idealThreadCount() * 2;
    const qsizetype size = (count + bands - 1) / bands;
    try {
        std::vector<int> indices(static_cast<size_t>(bands));
        std::iota(indices.begin(), indices.end(), 0);
        PoolMap::blocking(indices, [&](int band) {
            const qsizetype start = band * size;
            if (start < count)
                body(start, std::min(size, count - start));
        });
    } catch (const std::bad_alloc &) {
        // Out of memory fails as a context would.
        throw ExportError(ExportError::Kind::render);
    }
}

void BrushRaster::draw(const QImage &image, const QRectF &rect, QPainter &context, const QRectF &source)
{
    context.save();
    context.setRenderHint(QPainter::SmoothPixmapTransform, false);
    context.setCompositionMode(QPainter::CompositionMode_Source);
    context.drawImage(rect, image, source.isNull() ? QRectF(image.rect()) : source);
    context.restore();
}

QImage BrushRaster::alphaView(const QImage &mask)
{
    if (mask.format() != QImage::Format_Grayscale8)
        throw std::logic_error("a layer mask must be 8-bit gray");
    return QImage(mask.constBits(), mask.width(), mask.height(), mask.bytesPerLine(), QImage::Format_Alpha8);
}

QRectF BrushRaster::visibleRect(const QPainter &context)
{
    const QPaintDevice &device = *context.device();
    // A widget's size is logical; an image's is device pixels.
    QTransform toDevice = context.combinedTransform();
    if (device.devType() == QInternal::Widget)
        toDevice *= QTransform::fromScale(1 / device.devicePixelRatio(), 1 / device.devicePixelRatio());
    const QRectF bounds = toDevice.inverted().mapRect(QRectF(0, 0, device.width(), device.height()));
    return context.hasClipping() ? bounds.intersected(context.clipBoundingRect()) : bounds;
}

QTransform BrushRaster::pixelToDocument(const LayerTransform &transform, int width, int height)
{
    const QPointF center = transform.center();
    QTransform map;
    map.translate(center.x(), center.y());
    map.rotateRadians(transform.radians());
    map.scale(transform.size.width() / width * (transform.flipX ? -1 : 1),
              transform.size.height() / height * (transform.flipY ? -1 : 1));
    map.translate(-width / 2.0, -height / 2.0);
    return map;
}

// Source-over of the colour at coverage × alpha, bytewise.
void BrushRaster::fill(const QColor &color, const QImage &coverage, const QRectF &rect, double alpha, QImage &context,
                       QPainter::CompositionMode mode, const QImage &clip)
{
    if (coverage.format() != QImage::Format_Grayscale8 || coverage.size() != rect.size().toSize() || rect.topLeft() != rect.topLeft().toPoint())
        throw std::logic_error("coverage must be gray and 1:1 with its rect");
    if (!clip.isNull() && (clip.format() != QImage::Format_Grayscale8 || clip.size() != coverage.size()))
        throw std::logic_error("a clip must be gray and sized as the coverage");
    // Erasing takes coverage out; a mask never erases.
    const bool erasing = mode == QPainter::CompositionMode_DestinationOut;
    if ((!erasing && mode != QPainter::CompositionMode_SourceOver) || (erasing && context.format() == QImage::Format_Grayscale8))
        throw std::logic_error("a brush fills source-over, or erases pixels");
    const int left = int(rect.left()), top = int(rect.top());
    const bool gray = context.format() == QImage::Format_Grayscale8;
    if (!gray && context.format() != QImage::Format_RGBA8888_Premultiplied)
        throw std::logic_error("a brush context is premultiplied RGBA or gray");
    // What each channel moves towards; erasing moves towards nothing.
    const double target[4] = {erasing ? 0 : color.redF() * 255, erasing ? 0 : color.greenF() * 255, erasing ? 0 : color.blueF() * 255,
                              erasing ? 0 : 255.0};
    const int channels = gray ? 1 : 4;
    for (int y = 0; y < coverage.height(); ++y) {
        const uchar *mask = coverage.constScanLine(y);
        // A clip scales the coverage, rounded once, as CoreGraphics composites.
        const uchar *within = clip.isNull() ? nullptr : clip.constScanLine(y);
        uchar *row = context.scanLine(top + y) + left * channels;
        for (int x = 0; x < coverage.width(); ++x) {
            const double k = mask[x] / 255.0 * alpha * (within ? within[x] / 255.0 : 1.0);
            if (k <= 0)
                continue;
            // Values are never negative: adding a half rounds them.
            uchar *pixel = row + x * channels;
            for (int c = 0; c < channels; ++c)
                pixel[c] = uchar(pixel[c] * (1 - k) + target[c] * k + 0.5);
        }
    }
}

QImage BrushRaster::shiftedByFraction(const QImage &image, double fx, double fy)
{
    QImage shifted = BrushRaster::context(image.width() + 1, image.height() + 1, false);
    // Weights in 1/65536: the pixel, left, above, above left.
    const qint64 here = std::lround((1 - fx) * (1 - fy) * 65536), left = std::lround(fx * (1 - fy) * 65536);
    const qint64 above = std::lround((1 - fx) * fy * 65536), corner = 65536 - here - left - above;
    uchar *const out = shifted.bits();
    const uchar *const in = image.constBits();
    const qsizetype outStride = shifted.bytesPerLine(), inStride = image.bytesPerLine();
    const int width = image.width(), height = image.height();
    std::vector<int> rows(size_t(shifted.height()));
    std::iota(rows.begin(), rows.end(), 0);
    PoolMap::blocking(rows, [&](int y) {
        const uchar *row = y < height ? in + y * inStride : nullptr, *up = y > 0 ? in + (y - 1) * inStride : nullptr;
        uchar *line = out + y * outStride;
        for (int x = 0; x <= width; ++x) {
            for (int c = 0; c < 4; ++c) {
                const qint64 sum = (row && x < width ? here * row[x * 4 + c] : 0) + (row && x > 0 ? left * row[(x - 1) * 4 + c] : 0)
                    + (up && x < width ? above * up[x * 4 + c] : 0) + (up && x > 0 ? corner * up[(x - 1) * 4 + c] : 0);
                line[x * 4 + c] = uchar((sum + 32768) >> 16);
            }
        }
    });
    return shifted;
}
