#pragma once
#include "Document/BrushStroke.h"
#include "Document/LayerMask.h"
#include "Rendering/LayerRenderer.h"
#include <QtTest>

// Shared by the brush tests: strokes, previews, pixels.
inline BrushSettings brush(double diameter, double hardness, double red, double green, double blue, double opacity = 1)
{
    return BrushSettings{.diameter = diameter, .hardness = hardness, .red = red, .green = green, .blue = blue, .opacity = opacity};
}

// A document-pixel clone, copied from `offset` away.
inline BrushStroke::Clone documentClone(const QImage &image, QSizeF offset = QSizeF(0, 0))
{
    return {image, QRectF(-offset.width(), -offset.height(), image.width(), image.height()), false, {}};
}

inline ImageLayer blankLayer(int width, int height)
{
    return ImageLayer(QStringLiteral("Layer 1"), QSizeF(width, height));
}

// The stroke as the canvas shows it, canvas-sized.
inline QImage preview(const BrushStroke &stroke, QSizeF canvas)
{
    QImage context = BrushRaster::context(int(canvas.width()), int(canvas.height()), false);
    QPainter painter(&context);
    const ImageLayer &layer = stroke.layer;
    const std::optional<ImportedImage> &asset = layer.asset;
    const QImage image = asset && !asset.value().raster ? asset.value().image() : QImage();
    LayerRenderer::drawBrushPreview(image, stroke.paintTransform, stroke.paintTransform.center(), painter,
                                    {.opacity = layer.opacity, .blendMode = layer.blendMode, .mask = layer.mask ? layer.mask.value().enabledImage().value_or(QImage()) : QImage()},
                                    {.patches = stroke.patches(), .pixelWidth = stroke.width, .pixelHeight = stroke.height, .paintingMask = stroke.isMask,
                                     .sourceRect = stroke.sourceRect, .raster = asset ? asset.value().raster : nullptr});
    painter.end();
    return context;
}

inline std::vector<int> pixel(const QImage &image, int x, int y)
{
    const QImage bytes = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    const uchar *at = bytes.constScanLine(y) + x * 4;
    return {at[0], at[1], at[2], at[3]};
}

inline int alpha(const QImage &image, int x, int y)
{
    return pixel(image, x, y)[3];
}

// A red hard 20 px brush, Swift's default session settings.
inline BrushSettings red(double diameter = 20, double hardness = 1)
{
    return brush(diameter, hardness, 1, 0, 0);
}

inline std::unique_ptr<BrushStroke> stroke(int width, int height, const BrushSettings &settings)
{
    return std::make_unique<BrushStroke>(blankLayer(width, height), false, settings, QSizeF(width, height));
}

// A committed asset as the canvas shows it.
inline QImage render(const ImportedImage &asset, const LayerTransform &transform, QSizeF canvas)
{
    QImage context = BrushRaster::context(int(canvas.width()), int(canvas.height()), false);
    QPainter painter(&context);
    LayerRenderer::draw(asset.image(), transform, transform.center(), painter, {});
    return context;
}

inline ImportedImage gray(uchar value)
{
    QImage image = BrushRaster::context(1, 1, true);
    image.fill(value);
    return LayerMask::assetFrom(image);
}
