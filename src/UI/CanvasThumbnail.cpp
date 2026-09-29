#include "UI/CanvasThumbnail.h"
#include "Document/BrushStroke.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include "Rendering/LayerRenderer.h"
#include <QPainter>
#include <cmath>

namespace {
// A canvas-shaped picture; `draw` gets pixels and their scale.
QPixmap render(QSizeF canvas, double box, const std::function<void(QPainter &, QSize, double)> &draw)
{
    const QSize points = CanvasThumbnail::fittedSize(canvas, box);
    const QSize pixels = points * CanvasThumbnail::backingScale;
    QImage image;
    try {
        image = BrushRaster::context(pixels.width(), pixels.height(), false);
    } catch (const ExportError &error) {
        qCWarning(lcRendering) << "a thumbnail could not be allocated:" << pixels << error.what();
        QPixmap empty(pixels);
        empty.fill(Qt::transparent);
        empty.setDevicePixelRatio(CanvasThumbnail::backingScale);
        return empty;
    }
    {
        QPainter painter(&image);
        draw(painter, pixels, canvas.width() > 0 ? pixels.width() / canvas.width() : 1);
    }
    QPixmap result = QPixmap::fromImage(image);
    result.setDevicePixelRatio(CanvasThumbnail::backingScale);
    return result;
}

// The image where the transform puts it, as the canvas.
void place(const QImage &image, const LayerTransform &transform, double scale, QPainter &painter)
{
    LayerRenderer::draw(image, transform, QPointF(transform.center().x() * scale, transform.center().y() * scale), painter, {.scale = scale});
}
}

QSize CanvasThumbnail::fittedSize(QSizeF canvas, double box)
{
    if (!(canvas.width() > 0) || !(canvas.height() > 0) || !std::isfinite(canvas.width()) || !std::isfinite(canvas.height()))
        return QSize(int(box), int(box));
    const double scale = box / std::max(canvas.width(), canvas.height());
    return QSize(std::max(1, int(std::round(canvas.width() * scale))), std::max(1, int(std::round(canvas.height() * scale))));
}

QPixmap CanvasThumbnail::layer(const QImage &image, const LayerTransform &transform, QSizeF canvas, double box)
{
    return render(canvas, box, [&](QPainter &painter, QSize size, double scale) {
        painter.fillRect(QRect(QPoint(0, 0), size), QColor::fromRgbF(0.22, 0.22, 0.22));
        const double tile = 6 * backingScale;
        for (int row = 0; row < int(std::ceil(size.height() / tile)); ++row) {
            for (int column = 0; column < int(std::ceil(size.width() / tile)); ++column) {
                if ((row + column) % 2 == 0)
                    painter.fillRect(QRectF(column * tile, row * tile, tile, tile), QColor::fromRgbF(0.32, 0.32, 0.32));
            }
        }
        if (!image.isNull())
            place(image, transform, scale, painter);
    });
}

QPixmap CanvasThumbnail::mask(const QImage &image, const LayerTransform &transform, QSizeF canvas, double box)
{
    return render(canvas, box, [&](QPainter &painter, QSize size, double scale) {
        // White or black, as the canvas treats it, never gray.
        const double tone = LayerMask::background(image);
        painter.fillRect(QRect(QPoint(0, 0), size), QColor::fromRgbF(tone, tone, tone));
        place(image, transform, scale, painter);
    });
}
