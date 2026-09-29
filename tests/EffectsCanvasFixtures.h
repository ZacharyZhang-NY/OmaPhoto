#pragma once
#include "Document/LayerEffects+Renderer.h"
#include "IO/ImageExporter.h"
#include "Rendering/EditorCanvas.h"
#include "RenderFixtures.h"
#include "SessionFixtures.h"
#include <QtTest>

// A square with a stroke effect, on a white canvas.
namespace {
const QColor yellow(255, 255, 0);

LayerEffects outline(double size)
{
    LayerEffects effects;
    effects.stroke = StrokeEffect{.size = size, .red = 1, .green = 1, .blue = 0, .opacity = 1};
    return effects;
}

// White document, a square 25..35 by 15..25, at 100%.
struct Scene {
    EditorSession session;
    CanvasView canvas{session};
    QUuid square;
    explicit Scene(QRgb colour = qRgba(0, 0, 255, 255))
    {
        session.createDocument(60, 40);
        session.setShowsTransformControls(false);
        canvas.resize(300, 200);
        session.viewport.resize(QSizeF(300, 200), 1, QSizeF(60, 40));
        session.zoom(1);
        const QImage white = solid(60, 40, qRgba(255, 255, 255, 255));
        session.insert(ImportedImage(white, white, QStringLiteral("White")));
        const QImage pixels = solid(10, 10, colour);
        session.insert(ImportedImage(pixels, pixels, QStringLiteral("Square")), QPointF(30, 20));
        square = session.activeLayerID().value();
    }
    QImage shot() { return canvas.grab().toImage().convertToFormat(QImage::Format_ARGB32); }
    // The colour shown at a document pixel's centre.
    QColor at(QPoint pixel)
    {
        return shot().pixelColor(shown(QPointF(pixel) + QPointF(0.5, 0.5)));
    }
    // The view pixel holding a document point.
    QPoint shown(QPointF point)
    {
        const QPointF view = session.viewport.viewPoint(point, QSizeF(60, 40));
        return QPoint(int(std::floor(view.x())), int(std::floor(view.y())));
    }
    // Drawn once, then the worker's preview awaited.
    EffectsPreviewCache::Result landed()
    {
        shot();
        if (!QTest::qWaitFor([&] { return session.effectsPreviews.rendered(square).has_value(); }, 5000))
            throw std::runtime_error("no preview landed");
        return session.effectsPreviews.rendered(square).value();
    }
    // The worst channel gap to the export, pixel by pixel.
    int gapToExport()
    {
        const QImage exported = ImageExporter::render(session.projectSnapshot().value()).image.convertToFormat(QImage::Format_ARGB32);
        const QImage image = shot();
        int worst = 0;
        for (int y = 0; y < 40; ++y) {
            for (int x = 0; x < 60; ++x) {
                const QRgb expected = exported.pixel(x, y);
                const QRgb actual = image.pixel(shown(QPointF(x + 0.5, y + 0.5)));
                worst = std::max({worst, std::abs(qRed(expected) - qRed(actual)), std::abs(qGreen(expected) - qGreen(actual)),
                                  std::abs(qBlue(expected) - qBlue(actual))});
            }
        }
        return worst;
    }
};
}
