#include "Rendering/AdjustmentSurface.h"
#include "Document/BrushStroke.h"
#include "Logging.h"
#include <QPaintEngine>

void AdjustmentSurface::draw(QPainter &context, const std::function<void(QPainter &)> &body, qint64 pixelBudget)
{
    // The engine's device holds the pixels: a widget's window store.
    const QPaintDevice &target = *context.paintEngine()->paintDevice();
    const QRect device(0, 0, target.width(), target.height());
    const QRect area = context.deviceTransform().mapRect(BrushRaster::visibleRect(context)).toAlignedRect().intersected(device);
    if (area.isEmpty())
        return;
    if (qint64(area.width()) * area.height() > pixelBudget) {
        qCWarning(lcRendering) << "an adjustment surface passes its pixel budget:" << area.size();
        return;
    }
    QImage surface(area.size(), QImage::Format_RGBA8888_Premultiplied);
    if (surface.isNull()) {
        qCWarning(lcRendering) << "an adjustment surface could not be allocated:" << area.size();
        return;
    }
    surface.fill(0);
    {
        // The body draws in the painter's own coordinates.
        QPainter painter(&surface);
        painter.setTransform(context.deviceTransform() * QTransform::fromTranslate(-area.left(), -area.top()));
        body(painter);
    }
    // Drawn back under the painter's own opacity, mode and clip.
    context.save();
    context.setWorldTransform(context.deviceTransform().inverted() * context.worldTransform());
    context.drawImage(QRectF(area), surface);
    context.restore();
}
