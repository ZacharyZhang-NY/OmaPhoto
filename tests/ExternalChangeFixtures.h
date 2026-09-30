#pragma once
#include "DialogDesk.h"
#include "Document/BrushStroke.h"
#include "Document/ProjectWorkspace.h"
#include "IO/ProjectController.h"
#include "IO/ProjectDigest.h"
#include "IO/ProjectStore.h"
#include <QFutureWatcher>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtTest>

// Shared by the external change tests: packages written elsewhere.

// A saved two-layer project: an image below a blank layer.
inline QString savedProject(const QTemporaryDir &root)
{
    EditorSession session;
    session.createDocument(8, 6);
    QImage image = BrushRaster::context(8, 6, false);
    image.fill(QColor(200, 120, 60));
    session.insert(ImportedImage(image, image, QStringLiteral("Image")));
    session.renameLayer(session.activeLayerID().value(), QStringLiteral("Base"));
    session.addBlankLayer();
    const QString path = root.filePath(QStringLiteral("Watched.comp"));
    ProjectStore::save(session.projectSnapshot().value(), path);
    return path;
}

// Rewrites the package as another app would: a new name.
inline void renameFirstLayerOnDisk(const QString &path, const QString &name)
{
    ProjectSnapshot snapshot = ProjectStore::load(path);
    snapshot.manifest.layers[0].name = name;
    ProjectStore::save(snapshot, path);
}

// Rewrites a file through a temporary, as a script does.
inline void rewrite(const QString &file, const QByteArray &bytes)
{
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit())
        throw std::runtime_error("could not rewrite " + file.toStdString());
}

inline QByteArray contents(const QString &file)
{
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly))
        throw std::runtime_error("could not read " + file.toStdString());
    return in.readAll();
}

inline bool answered(const std::function<void(std::function<void(bool)>)> &call)
{
    std::optional<bool> result;
    call([&](bool value) { result = value; });
    return QTest::qWaitFor([&] { return result.has_value(); }, 10'000) && result.value();
}

// Opens the project as the app does: watched, digest kept.
inline std::unique_ptr<ProjectController> opened(EditorSession &session, const QString &path)
{
    auto controller = std::make_unique<ProjectController>(session);
    if (!answered([&](auto done) { controller->open(path, done); }))
        throw std::runtime_error("the project did not open");
    return controller;
}

inline QString firstName(const EditorSession &session)
{
    return session.document().value().layers.front().name;
}

// Longer than the watcher's quiet time and a digest.
inline void settle()
{
    QTest::qWait(900);
}
