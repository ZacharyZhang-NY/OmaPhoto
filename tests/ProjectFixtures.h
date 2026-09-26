#pragma once
#include "IO/ProjectStore.h"
#include "RenderFixtures.h"
#include <QDir>
#include <QFile>

// A 64x32 image: red on the left, clear right.
inline QImage halfRed()
{
    QImage image = BrushRaster::context(64, 32, false);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x)
            image.setPixel(x, y, qRgba(255, 0, 0, 255));
    }
    return image;
}

inline ImportedImage asset(const QImage &image, const QString &name)
{
    return ImportedImage(image, image, name);
}

// A hidden, turned, flipped image layer under a blank one.
inline ProjectSnapshot twoLayers()
{
    const QUuid imageID = QUuid::createUuid(), blankID = QUuid::createUuid();
    const LayerTransform turned{.origin = {-27.5, 88.25}, .size = {123, 47}, .rotation = 38, .flipX = true, .flipY = true,
                                .sampling = LayerSampling::nearest};
    const ProjectLayerRecord image{.id = imageID, .name = QStringLiteral("Paint & sky \U0001F324"), .isVisible = false, .transform = turned,
                                   .imageFile = uuidString(imageID) + ".png"};
    const ProjectLayerRecord blank{.id = blankID, .name = "Layer 2", .isVisible = true,
                                   .transform = {.origin = {0, 0}, .size = {100, 80}}, .imageFile = std::nullopt};
    return {.manifest = {.resolution = 300, .documentID = QUuid::createUuid(), .width = 100, .height = 80, .activeLayerID = blankID,
                         .layers = {image, blank}},
            .images = {{imageID, asset(halfRed(), "source")}}};
}

inline QByteArray contents(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("cannot read " + path.toStdString());
    return file.readAll();
}

inline void overwrite(const QString &path, const QByteArray &data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(data) != data.size())
        throw std::runtime_error("cannot write " + path.toStdString());
}

// The kind of ProjectError `body` throws, if it throws one.
template <typename Body>
std::optional<ProjectError::Kind> projectError(Body body)
{
    try {
        body();
    } catch (const ProjectError &error) {
        return error.kind;
    }
    return std::nullopt;
}
