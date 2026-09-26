#pragma once
#include "Rendering/LayerRenderer.h"
#include "Rendering/LiveMaskRenderer.h"
#include "RenderFixtures.h"
#include <QHash>
#include <array>

struct Layer {
    QImage image;
    LayerTransform transform;
    double opacity = 1;
    LayerBlendMode blendMode = LayerBlendMode::normal;
    QImage mask = QImage();
    std::optional<QUuid> source = std::nullopt;
    std::optional<QUuid> parent = std::nullopt;
};

// A 2x2 red image with the given alphas, as Swift's.
inline QImage asset(std::array<int, 4> alpha, int color = 255)
{
    QImage image = BrushRaster::context(2, 2, false);
    for (int index = 0; index < 4; ++index)
        image.setPixel(index % 2, index / 2, qRgba(color * alpha[index] / 255, 0, 0, alpha[index]));
    return image;
}

// A 3x2 image with six alphas; full red, or black.
inline QImage wide(std::array<int, 6> alpha, int color = 255)
{
    QImage image = BrushRaster::context(3, 2, false);
    for (int index = 0; index < 6; ++index)
        image.setPixel(index % 3, index / 3, qRgba(color * alpha[index] / 255, 0, 0, alpha[index]));
    return image;
}

inline std::array<int, 4> alphas(const QImage &image)
{
    return {qAlpha(image.pixel(0, 0)), qAlpha(image.pixel(1, 0)), qAlpha(image.pixel(0, 1)), qAlpha(image.pixel(1, 1))};
}

inline std::array<int, 4> reds(const QImage &image)
{
    return {qRed(image.pixel(0, 0)), qRed(image.pixel(1, 0)), qRed(image.pixel(0, 1)), qRed(image.pixel(1, 1))};
}

// One render's layers, their draw counts, and the renderer's callbacks.
struct Scene {
    QHash<QUuid, Layer> layers;
    QHash<QUuid, int> drawn;

    QUuid add(Layer layer)
    {
        const QUuid id = QUuid::createUuid();
        layers.insert(id, std::move(layer));
        return id;
    }
    LiveMaskRenderer renderer(qint64 budget = LiveMaskRenderer::pixelBudget)
    {
        return LiveMaskRenderer([this](QUuid id) { return layers[id].source; },
                                [this](QUuid id, QPainter &painter, const QImage &clip) {
                                    const Layer &layer = layers[id];
                                    drawn[id] += 1;
                                    LayerRenderer::draw(layer.image, layer.transform, layer.transform.center(), painter,
                                                        {.opacity = layer.opacity, .blendMode = layer.blendMode, .mask = layer.mask, .clip = clip});
                                },
                                budget);
    }
    QImage render(const std::vector<QUuid> &ids, QImage surface = BrushRaster::context(2, 2, false))
    {
        LiveMaskRenderer live = renderer();
        live.prepareStacks(ids, [this](QUuid id) { return layers[id].parent; }, [this](QUuid id) { return layers[id].blendMode; });
        QPainter painter(&surface);
        for (const QUuid &id : ids)
            live.drawComposite(id, painter);
        painter.end();
        return surface;
    }
};

inline Layer placed(const QImage &image)
{
    return {.image = image, .transform = placedAt({0, 0}, {2, 2})};
}
