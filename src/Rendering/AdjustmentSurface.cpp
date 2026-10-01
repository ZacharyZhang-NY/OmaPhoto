#include "Rendering/AdjustmentSurface.h"
#include "Document/BrushStroke.h"
#include "Logging.h"
#include "Rendering/LayerRenderer.h"
#include <QPaintEngine>

void AdjustmentSurface::draw(QPainter &context, const std::function<void(QPainter &)> &body, double padding, qint64 pixelBudget)
{
    // The engine's device holds the pixels: a widget's window store.
    const QPaintDevice &target = *context.paintEngine()->paintDevice();
    const QRect device(0, 0, target.width(), target.height());
    const QTransform toDevice = context.deviceTransform();
    const QRect output = toDevice.mapRect(BrushRaster::visibleRect(context)).toAlignedRect().intersected(device);
    if (output.isEmpty())
        return;
    // Spatial adjustments read a halo; the clip keeps what shows.
    const QRectF shown = toDevice.inverted().mapRect(QRectF(output));
    const QRect area = padding > 0 ? toDevice.mapRect(shown.adjusted(-padding, -padding, padding, padding)).toAlignedRect() : output;
    // Device pixels; past the budget, painter units.
    double scale = 1;
    QSize size = area.size();
    if (qint64(size.width()) * size.height() > pixelBudget) {
        scale = 1 / LayerRenderer::deviceScale(context);
        size = QSize(std::max(1, qRound(area.width() * scale)), std::max(1, qRound(area.height() * scale)));
    }
    if (qint64(size.width()) * size.height() > pixelBudget) {
        qCWarning(lcRendering) << "an adjustment surface passes its pixel budget:" << area.size();
        return;
    }
    QImage surface(size, QImage::Format_RGBA8888_Premultiplied);
    if (surface.isNull()) {
        qCWarning(lcRendering) << "an adjustment surface could not be allocated:" << area.size();
        return;
    }
    surface.fill(0);
    {
        // The body draws in the painter's own coordinates.
        QPainter painter(&surface);
        painter.setTransform(context.deviceTransform() * QTransform::fromTranslate(-area.left(), -area.top()) * QTransform::fromScale(scale, scale));
        body(painter);
    }
    // Drawn back under the painter's own opacity, mode and clip.
    context.save();
    context.setWorldTransform(context.deviceTransform().inverted() * context.worldTransform());
    context.setRenderHint(QPainter::SmoothPixmapTransform, scale != 1);
    context.drawImage(QRectF(area), surface);
    context.restore();
}
