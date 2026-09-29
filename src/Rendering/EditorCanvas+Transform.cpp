#include "Rendering/EditorCanvas.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <numbers>

namespace {
// Whether `upper` is painted above `lower`: later is higher.
bool above(const std::vector<ImageLayer> &layers, QUuid upper, QUuid lower)
{
    const auto index = [&](QUuid id) -> std::optional<size_t> {
        for (size_t at = 0; at < layers.size(); ++at)
            if (layers[at].id == id)
                return at;
        return std::nullopt;
    };
    // Both are drawn: the pointer's layer, the visible active one.
    return index(upper).value() > index(lower).value();
}

// Four arrows about `center`, each `reach` long, traced clockwise.
QPainterPath fourArrowPath(QPointF center, double reach, double shaft, double head, double headLength)
{
    const std::array<QPointF, 6> arm = {QPointF(-shaft, -reach + headLength), QPointF(-head, -reach + headLength), QPointF(0, -reach),
                                        QPointF(head, -reach + headLength), QPointF(shaft, -reach + headLength), QPointF(shaft, -shaft)};
    QPainterPath path;
    for (int turn = 0; turn < 4; ++turn) {
        for (QPointF point : arm) {
            for (int step = 0; step < turn; ++step)
                point = QPointF(-point.y(), point.x());
            const QPointF location = center + point;
            if (path.isEmpty())
                path.moveTo(location);
            else
                path.lineTo(location);
        }
    }
    path.closeSubpath();
    return path;
}

QPixmap cursorPixmap(QSize size, double ratio)
{
    QPixmap pixmap(size * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    return pixmap;
}
}

// Photoshop's Move pointer: the arrow with a four-way badge.
QCursor CanvasView::moveCursor(double ratio)
{
    QPixmap pixmap = cursorPixmap(QSize(36, 36), ratio);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF tip(4, 3);
    const QPainterPath arrow = arrowPath().translated(tip);
    painter.setPen(QPen(Qt::white, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(arrow);
    painter.fillPath(arrow, Qt::black);
    // Heads narrow and apart: four distinct arrows at badge size.
    const QPainterPath badge = fourArrowPath(tip + QPointF(14.5, 17.5), 6.5, 0.75, 2, 2.5);
    painter.setPen(QPen(Qt::white, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(badge);
    painter.fillPath(badge, Qt::black);
    return QCursor(pixmap, 4, 3);
}

// The arrow in white: a Ctrl-drag moves the corner alone.
QCursor CanvasView::distortCursor(double ratio)
{
    QPixmap pixmap = cursorPixmap(QSize(28, 32), ratio);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPainterPath arrow = arrowPath().translated(QPointF(4, 3));
    painter.setPen(QPen(Qt::black, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(arrow);
    painter.fillPath(arrow, Qt::white);
    return QCursor(pixmap, 4, 3);
}

// A pointing hand with Swift's dashed box beside it.
QCursor CanvasView::loadSelectionCursor(double ratio)
{
    QPixmap pixmap = cursorPixmap(QSize(28, 32), ratio);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath hand;
    hand.setFillRule(Qt::WindingFill);
    hand.addRoundedRect(QRectF(8.5, 1.5, 4.5, 14), 2.2, 2.2);
    hand.addRoundedRect(QRectF(4.5, 11, 14.5, 12), 4, 4);
    painter.setPen(QPen(Qt::black, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(hand);
    painter.fillPath(hand, Qt::white);
    const QPointF tip(11, 2);
    const QRectF box(tip + QPointF(6.5, 16.5), QSizeF(8, 6));
    painter.setPen(QPen(Qt::white, 2.5));
    painter.drawRect(box);
    QPen dashed(Qt::black, 1);
    dashed.setCapStyle(Qt::FlatCap);
    dashed.setDashPattern({2, 1.5});
    painter.setPen(dashed);
    painter.drawRect(box);
    return QCursor(pixmap, int(tip.x()), int(tip.y()));
}

// Two arrows chasing round a circle, haloed like the symbol.
QCursor CanvasView::rotationCursor(double ratio)
{
    QPixmap pixmap = cursorPixmap(QSize(24, 24), ratio);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF ring(5, 5, 14, 14);
    QPainterPath shape;
    for (const double start : {20.0, 200.0}) {
        shape.arcMoveTo(ring, start);
        shape.arcTo(ring, start, 130);
        // The head at the arc's end, turned along it.
        const QPointF end = shape.currentPosition();
        const double tangent = (start + 130 + 90) * std::numbers::pi / 180;
        const QPointF along(std::cos(tangent), -std::sin(tangent));
        const QPointF across(-along.y(), along.x());
        shape.moveTo(end + across * 3 - along * 1.5);
        shape.lineTo(end + along * 2);
        shape.lineTo(end - across * 3 - along * 1.5);
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::white, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(shape);
    painter.setPen(QPen(Qt::black, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(shape);
    return QCursor(pixmap, 12, 12);
}

// The layer a press off the handles drags; Ctrl picks.
std::optional<CanvasView::PressTarget> CanvasView::transformPressLayer(QPointF pixel, Qt::KeyboardModifiers modifiers) const
{
    const std::optional<CanvasDocument> &document = m_session.document();
    if (!(m_session.canEditLayers() || m_session.transformEdit()) || !document)
        return std::nullopt;
    std::optional<QUuid> underPointer;
    const std::vector<ImageLayer> rendered = document->renderLayers();
    for (auto layer = rendered.rbegin(); layer != rendered.rend() && !underPointer; ++layer) {
        if (layer->asset && layer->transform.contains(pixel))
            underPointer = layer->id;
    }
    std::optional<ImageLayer> active = m_session.activeLayer();
    if (active && !(active->asset && !active->isGroup && document->effectiveVisibleIDs().contains(active->id)))
        active = std::nullopt;
    const bool picks = !m_session.transformEdit();
    const bool control = modifiers.testFlag(Qt::ControlModifier);
    if (control && picks && underPointer)
        return PressTarget{*underPointer, true};
    // A group's box drags them all; outside, unless auto-select picks.
    if (m_session.transformsAsGroup() && m_session.activeLayerID()) {
        const std::optional<LayerTransform> box = m_session.transformEdit() ? std::optional(m_session.transformEdit()->draft) : m_session.groupTransformBox();
        if ((box && box->contains(pixel)) || !(picks && m_session.transformAutoSelect()) || !underPointer)
            return PressTarget{*m_session.activeLayerID(), false};
    }
    if (active && m_session.editedTransform(*active).contains(pixel)) {
        // Auto Select prefers a layer above, as over a background.
        if (picks && m_session.transformAutoSelect() && underPointer && above(rendered, *underPointer, active->id))
            return PressTarget{*underPointer, true};
        return PressTarget{active->id, false};
    }
    if (picks && (m_session.transformAutoSelect() || control) && underPointer)
        return PressTarget{*underPointer, true};
    if (active)
        return PressTarget{active->id, false};
    return std::nullopt;
}

bool CanvasView::pressMovesLayer(QPointF point, Qt::KeyboardModifiers modifiers) const
{
    const std::optional<CanvasDocument> &document = m_session.document();
    return document && transformPressLayer(m_session.viewport.documentPoint(point, document->size()), modifiers).has_value();
}

// The Move tool's cursor; Alt offers a copy.
QCursor CanvasView::transformCursor(QPointF point, Qt::KeyboardModifiers modifiers) const
{
    if (m_dragCursor)
        return *m_dragCursor;
    if (m_spaceHeld)
        return QCursor(Qt::OpenHandCursor);
    if (m_session.isProjectBusy() || m_session.isImporting())
        return QCursor(Qt::ArrowCursor);
    const bool duplicate = modifiers.testFlag(Qt::AltModifier);
    const double ratio = devicePixelRatio();
    const std::optional<TransformOverlayGeometry> geometry = m_overlay.geometry();
    const std::optional<TransformDrag::Mode> hit = geometry ? geometry->hit(point) : std::nullopt;
    if (!hit) {
        // Guides lie under the handles, over a layer's drag.
        if (const std::optional<CanvasGuide> guide = m_session.hitGuide(point))
            return QCursor(guide->axis == CanvasGuide::Axis::vertical ? Qt::SizeHorCursor : Qt::SizeVerCursor);
        if (!pressMovesLayer(point, modifiers))
            return QCursor(Qt::ArrowCursor);
        return duplicate ? duplicateCursor(ratio) : moveCursor(ratio);
    }
    if (hit->kind == TransformDrag::Kind::rotate)
        return rotationCursor(ratio);
    // Distorting (Ctrl held, or already distorted) moves corners freely.
    const std::optional<TransformEdit> &edit = m_session.transformEdit();
    const bool distorting = (edit && edit->corners) || modifiers.testFlag(Qt::ControlModifier);
    return distorting ? distortCursor(ratio) : QCursor(geometry->resizeCursor(hit->index));
}

void CanvasView::beginTransformDrag(QPointF point, Qt::KeyboardModifiers modifiers)
{
    const std::optional<CanvasDocument> &document = m_session.document();
    if (!(m_session.canEditLayers() || m_session.transformEdit()) || !document)
        return;
    const QPointF pixel = m_session.viewport.documentPoint(point, document->size());
    const std::optional<TransformOverlayGeometry> geometry = m_overlay.geometry();
    std::optional<TransformDrag::Mode> mode = geometry ? geometry->hit(point) : std::nullopt;
    if (!mode) {
        const std::optional<PressTarget> target = transformPressLayer(pixel, modifiers);
        if (!target)
            return;
        // Ctrl+Shift adds the picked layer; Ctrl alone selects it.
        if (target->picked && modifiers.testFlag(Qt::ControlModifier) && modifiers.testFlag(Qt::ShiftModifier))
            m_session.extendSelection(target->id);
        else if (target->picked)
            m_session.selectLayer(target->id);
        mode = TransformDrag::Mode{TransformDrag::Kind::move};
    }
    m_duplicatesTransformOnDrag = mode->kind == TransformDrag::Kind::move && modifiers.testFlag(Qt::AltModifier);
    if (!m_session.transformEdit())
        m_session.beginTransform(false);
    if (!m_session.transformEdit())
        return;
    // Ctrl-dragging a handle distorts; once distorted, handles keep distorting.
    if (mode->kind == TransformDrag::Kind::resize && (modifiers.testFlag(Qt::ControlModifier) || m_session.transformEdit()->corners)) {
        m_session.beginDistort();
        if (m_session.transformEdit()->corners)
            mode = TransformDrag::Mode{TransformDrag::Kind::distort, mode->index};
    }
    m_transformDrag = TransformDrag{m_session.transformEdit()->draft, pixel, *mode, m_session.transformEdit()->corners};
    const double ratio = devicePixelRatio();
    if (mode->kind == TransformDrag::Kind::resize)
        m_dragCursor = QCursor(geometry->resizeCursor(mode->index));
    else if (mode->kind == TransformDrag::Kind::rotate)
        m_dragCursor = rotationCursor(ratio);
    else if (mode->kind == TransformDrag::Kind::distort)
        m_dragCursor = distortCursor(ratio);
    else
        m_dragCursor = m_duplicatesTransformOnDrag ? duplicateCursor(ratio) : moveCursor(ratio);
    updateCursor();
}

void CanvasView::dragTransform(QPointF point, Qt::KeyboardModifiers modifiers)
{
    const QPointF pixel = m_session.viewport.documentPoint(point, m_session.document().value().size());
    if (m_duplicatesTransformOnDrag) {
        m_duplicatesTransformOnDrag = false;
        m_session.beginDuplicateTransform();
    }
    if (const std::optional<Corners> corners = m_transformDrag->corners(pixel, modifiers.testFlag(Qt::ShiftModifier))) {
        m_session.previewCorners(*corners);
        synchronizeDisplay();
        return;
    }
    // Drags land on whole pixels and degrees; typing stays exact.
    LayerTransform draft = m_transformDrag->updated(pixel, m_session.locksTransformRatio(), modifiers.testFlag(Qt::ShiftModifier),
                                                    modifiers.testFlag(Qt::AltModifier)).rounded();
    const std::optional<TransformEdit> &edit = m_session.transformEdit();
    // A move snaps to canvas and layers; Ctrl frees it.
    if (m_transformDrag->mode.kind == TransformDrag::Kind::move && !modifiers.testFlag(Qt::ControlModifier) && edit) {
        QSet<QUuid> moving;
        if (edit->group) {
            for (auto original = edit->group->originals.constBegin(); original != edit->group->originals.constEnd(); ++original)
                moving.insert(original.key());
        } else {
            moving.insert(edit->layerID);
        }
        draft = m_session.snappedMove(draft, moving, TransformSnap::distance / std::max(m_session.viewport.pointsPerPixel(), 0.0001));
    }
    m_session.previewTransform(draft);
    synchronizeDisplay();
}

// The release: a drag's edit commits, Ctrl+T's waits.
void CanvasView::endTransformDrag()
{
    m_duplicatesTransformOnDrag = false;
    m_transformDrag.reset();
    m_dragCursor.reset();
    if (m_session.transformEdit() && !m_session.transformEdit()->persistent)
        m_session.commitTransform();
    updateCursor();
}

// Losing the keys mid-drag puts the layer back.
void CanvasView::cancelTransformDrag()
{
    if (!m_transformDrag)
        return;
    m_duplicatesTransformOnDrag = false;
    m_session.previewTransform(m_transformDrag->original);
    if (m_session.transformEdit() && !m_session.transformEdit()->persistent)
        m_session.cancelTransform();
    m_transformDrag.reset();
    m_dragCursor.reset();
    updateCursor();
}

bool CanvasView::beginLiveTextEdit(QPointF point)
{
    if (!m_session.document() || !m_session.canEditLayers())
        return false;
    const CanvasDocument &document = *m_session.document();
    const QPointF pixel = m_session.viewport.documentPoint(point, document.size());
    const QSet<QUuid> visible = document.effectiveVisibleIDs();
    std::optional<QUuid> found;
    for (auto layer = document.layers.rbegin(); !found && layer != document.layers.rend(); ++layer) {
        if (visible.contains(layer->id) && layer->liveText() && layer->transform.contains(pixel))
            found = layer->id;
    }
    if (!found)
        return false;
    // Ends an opacity drag and a blend preview, as Swift's.
    m_session.commitTransform();
    m_session.selectLayer(*found);
    m_session.editActiveText();
    synchronizeInlineText();
    return true;
}
