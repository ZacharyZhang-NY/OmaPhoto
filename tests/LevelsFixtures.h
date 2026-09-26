#pragma once
#include "Document/EditorSession.h"
#include <QtTest>
#include <array>
#include <memory>
#include <vector>

// Shared by the Levels tests: pixels, ramp and commit.
inline QImage image(const std::vector<std::array<uchar, 4>> &pixels)
{
    QImage result(int(pixels.size()), 1, QImage::Format_RGBA8888_Premultiplied);
    for (size_t index = 0; index < pixels.size(); ++index)
        std::copy(pixels[index].begin(), pixels[index].end(), result.bits() + index * 4);
    return result;
}

// Premultiplied RGBA bytes, row after row.
inline std::vector<uchar> bytes(const QImage &image)
{
    const QImage drawn = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    std::vector<uchar> result;
    for (int y = 0; y < drawn.height(); ++y)
        result.insert(result.end(), drawn.constScanLine(y), drawn.constScanLine(y) + drawn.width() * 4);
    return result;
}

inline const std::vector<std::array<uchar, 4>> ramp{{0, 0, 0, 255}, {64, 64, 64, 255}, {128, 128, 128, 255},
                                                   {255, 255, 255, 255}, {64, 32, 0, 128}, {0, 0, 0, 0}};

// Swift's session: the ramp as a 6 by 1 layer.
inline std::unique_ptr<EditorSession> rampSession()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(6, 1);
    const QImage source = image(ramp);
    session->insert(ImportedImage(source, source, QStringLiteral("Ramp")));
    return session;
}

// 3000 by 2000 red: long enough runs to queue behind.
inline std::unique_ptr<EditorSession> bigSession()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(3000, 2000);
    QImage red(3000, 2000, QImage::Format_RGBA8888_Premultiplied);
    red.fill(Qt::red);
    session->insert(ImportedImage(red, red, QStringLiteral("Red")));
    return session;
}

inline LevelsJob job(const QImage &image, const LevelsSettings &settings = {}, std::optional<SelectionClip> selection = std::nullopt)
{
    return LevelsJob{image, settings, std::move(selection), QTransform()};
}

// Swift's `await commitLevels()`: back once the step has landed.
inline void commit(EditorSession &session)
{
    bool done = false;
    session.commitLevels([&done] { done = true; });
    QTRY_VERIFY(done);
}
