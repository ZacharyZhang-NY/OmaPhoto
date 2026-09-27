#include "Document/EditorSession.h"

LayerTransform LayerTransform::mirrored(bool horizontally, double axis) const
{
    LayerTransform result = *this;
    // The middle crosses the line; picture and angle turn over.
    if (horizontally) {
        result.flipX = !flipX;
        result.origin.setX(2 * axis - center().x() - size.width() / 2);
    } else {
        result.flipY = !flipY;
        result.origin.setY(2 * axis - center().y() - size.height() / 2);
    }
    result.rotation = -rotation;
    return result;
}

// A layer about its middle; a group about its box.
void EditorSession::flipLayers(bool horizontally)
{
    commitTransform();
    if (!canTransform())
        return;
    std::vector<ImageLayer> members;
    double axis = 0;
    if (transformsAsGroup()) {
        const LayerTransform box = groupTransformBox().value();
        members = groupTransformMembers();
        axis = horizontally ? box.center().x() : box.center().y();
    } else {
        const ImageLayer layer = activeLayer().value();
        members = {layer};
        axis = horizontally ? layer.transform.center().x() : layer.transform.center().y();
    }
    QSet<QUuid> ids;
    for (const ImageLayer &member : members)
        ids.insert(member.id);
    beginEdit(horizontally ? QStringLiteral("Flip Horizontal") : QStringLiteral("Flip Vertical"));
    for (ImageLayer &layer : m_document->layers) {
        if (!ids.contains(layer.id))
            continue;
        const LayerTransform flipped = layer.transform.mirrored(horizontally, axis);
        // A linked mask flips along; an unlinked one stays.
        if (layer.mask)
            layer.mask->placement = layer.mask->placementMovingLayer(layer.transform, flipped);
        layer.transform = flipped;
    }
    endEdit();
}

// One step; Swift's finishOpacityEdit ran in commitTransform already.
void EditorSession::flipCanvas(bool horizontally)
{
    commitTransform();
    cancelCrop();
    if (!canEditLayers())
        return;
    const QSizeF size = m_document->size();
    const double axis = horizontally ? size.width() / 2 : size.height() / 2;
    beginEdit(horizontally ? QStringLiteral("Flip Canvas Horizontal") : QStringLiteral("Flip Canvas Vertical"));
    for (ImageLayer &layer : m_document->layers) {
        layer.transform = layer.transform.mirrored(horizontally, axis);
        if (layer.mask && layer.mask->placement)
            layer.mask->placement = layer.mask->placement->mirrored(horizontally, axis);
    }
    if (m_document->selection) {
        const QTransform mirror = horizontally ? QTransform(-1, 0, 0, 1, size.width(), 0) : QTransform(1, 0, 0, -1, 0, size.height());
        m_document->selection->path = mirror.map(m_document->selection->path);
    }
    for (CanvasGuide &guide : m_document->guides)
        guide = guide.mirrored(horizontally, axis);
    endEdit();
}
