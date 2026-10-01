#pragma once
#include "Rendering/EditorCanvas.h"
#include "IO/ImageExporter.h"
#include "RenderFixtures.h"
#include "SessionFixtures.h"
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <QtTest>

// Compositor 1.4's GPU canvas tests, held against the CPU canvas.
struct ContractScene {
    EditorSession session;
    CanvasView canvas{session};
    ContractScene(int width, int height, int canvasWidth, int canvasHeight)
    {
        session.createDocument(width, height);
        session.setShowsTransformControls(false);
        canvas.resize(canvasWidth, canvasHeight);
        session.viewport.resize(QSizeF(canvasWidth, canvasHeight), 1, QSizeF(width, height));
    }
    QImage shot() { return canvas.grab().toImage().convertToFormat(QImage::Format_ARGB32); }
    QPointF origin() { return session.viewport.viewPoint(QPointF(0, 0), session.document().value().size()); }
    QImage exported() { return ImageExporter::render(session.projectSnapshot().value()).image.convertToFormat(QImage::Format_ARGB32); }
    // What the canvas shows at each document pixel's centre.
    QImage documentShot()
    {
        canvas.synchronizeDisplay();
        const QImage view = shot();
        const QSizeF size = session.document().value().size();
        const double zoom = session.viewport.pointsPerPixel();
        QImage result(size.toSize(), QImage::Format_ARGB32);
        for (int y = 0; y < result.height(); ++y) {
            for (int x = 0; x < result.width(); ++x)
                result.setPixel(x, y, view.pixel(int(origin().x() + (x + 0.5) * zoom), int(origin().y() + (y + 0.5) * zoom)));
        }
        return result;
    }
    // The export over the checkerboard, as the canvas shows it.
    QImage exportedOnCanvas()
    {
        const QImage exported = ImageExporter::render(session.projectSnapshot().value()).image;
        const double zoom = session.viewport.pointsPerPixel();
        QImage result(exported.size(), QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < result.height(); ++y) {
            for (int x = 0; x < result.width(); ++x) {
                const bool light = (int(std::floor((x + 0.5) * zoom / 10)) + int(std::floor((y + 0.5) * zoom / 10))) % 2 == 0;
                result.setPixel(x, y, QColor::fromRgbF(light ? 0.35 : 0.30, light ? 0.35 : 0.30, light ? 0.35 : 0.30).rgb());
            }
        }
        QPainter painter(&result);
        painter.drawImage(QRectF(result.rect()), exported);
        painter.end();
        return result.convertToFormat(QImage::Format_ARGB32);
    }
    int gapToExport(const QPainterPath &skip = QPainterPath()) { return gap(documentShot(), exportedOnCanvas(), skip); }
    // The worst channel gap inside the border, `skip` aside.
    static int gap(const QImage &one, const QImage &other, const QPainterPath &skip = QPainterPath())
    {
        int worst = 0;
        for (int y = 1; y < one.height() - 1; ++y) {
            for (int x = 1; x < one.width() - 1; ++x) {
                if (skip.contains(QPointF(x + 0.5, y + 0.5)))
                    continue;
                const QRgb a = one.pixel(x, y), b = other.pixel(x, y);
                worst = std::max({worst, std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b))});
            }
        }
        return worst;
    }
    // A band round a path, where overlays draw.
    static QPainterPath band(const QPainterPath &path, double width = 6)
    {
        QPainterPathStroker stroker;
        stroker.setWidth(width);
        return stroker.createStroke(path);
    }
};

// A mask dark on the left, light on the right.
inline ImportedImage rampMask(int width, int height)
{
    QImage mask(width, height, QImage::Format_Grayscale8);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x)
            mask.scanLine(y)[x] = uchar(x * 255 / (width - 1));
    }
    return LayerMask::assetFrom(mask);
}
