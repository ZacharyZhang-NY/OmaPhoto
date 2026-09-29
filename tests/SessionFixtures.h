#pragma once
#include "Document/EditorSession.h"
#include <functional>
#include <memory>

// Swift's tests write into `session.document`; here a snapshot goes in.
inline void rewrite(EditorSession &session, const std::function<void(ProjectSnapshot &)> &change)
{
    ProjectSnapshot snapshot = session.projectSnapshot().value();
    change(snapshot);
    session.installProject(snapshot, QStringLiteral("fixture.comp"));
}

inline ProjectLayerRecord &record(ProjectSnapshot &snapshot, QUuid id)
{
    for (ProjectLayerRecord &layer : snapshot.manifest.layers) {
        if (layer.id == id)
            return layer;
    }
    throw std::runtime_error("no such record");
}

// Gives the layer this mask and its record a file.
inline void setMask(ProjectSnapshot &snapshot, QUuid id, const ImportedImage &mask)
{
    record(snapshot, id).maskFile = uuidString(id) + ".mask.png";
    snapshot.masks.insert_or_assign(id, mask);
}

inline ImageLayer layerWith(const EditorSession &session, QUuid id)
{
    for (const ImageLayer &layer : session.document().value().layers) {
        if (layer.id == id)
            return layer;
    }
    throw std::runtime_error("no such layer");
}

// Two by two, red, nearest: masks map pixel for pixel.
inline std::unique_ptr<EditorSession> redSession()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(2, 2);
    QImage image(2, 2, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::red);
    session->insert(ImportedImage(image, image, "Red"));
    const QUuid id = session->activeLayerID().value();
    rewrite(*session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform.sampling = LayerSampling::nearest; });
    return session;
}

// Coverage 255, 0 over 128, 255.
inline ImportedImage coverage()
{
    QImage image(2, 2, QImage::Format_Grayscale8);
    const uchar values[] = {255, 0, 128, 255};
    for (int index = 0; index < 4; ++index)
        image.scanLine(index / 2)[index % 2] = values[index];
    return LayerMask::assetFrom(image);
}

// New text whose box starts at `corner`, unlike a click.
inline void beginTextAt(EditorSession &session, QPointF corner)
{
    session.beginText(corner, true);
    TextDraft draft = session.textDraft().value();
    draft.origin = corner;
    session.setTextDraft(draft);
}
