#include "Document/Crop.h"
#include "Document/DocumentLimits.h"
#include "Document/Distort.h"
#include "Document/EditorSession.h"
#include "IO/CanvasResizer.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore.h"
#include <QtConcurrent>
#include <cmath>
#include <new>

QRectF CropGeometry::snapped(const QRectF &rect)
{
    const QRectF standard = rect.normalized();
    const double x = std::round(standard.left()), y = std::round(standard.top());
    return QRectF(x, y, std::max(1.0, std::round(standard.right()) - x), std::max(1.0, std::round(standard.bottom()) - y));
}

// Swift's isFinite terms are the ranges' own: NaN passes none.
bool CropGeometry::valid(const QRectF &rect)
{
    const QRectF standard = rect.normalized();
    return standard.width() >= 1 && standard.width() <= DocumentLimits::maxSide && standard.height() >= 1 && standard.height() <= DocumentLimits::maxSide
        && std::abs(standard.left()) <= 1'000'000 && std::abs(standard.top()) <= 1'000'000;
}

QRectF CropGeometry::create(QPointF start, QPointF end, std::optional<double> ratio, bool symmetric)
{
    double dx = end.x() - start.x(), dy = end.y() - start.y();
    if (ratio) {
        if (std::abs(dx) > std::abs(dy) * *ratio)
            dy = (dy < 0 ? -1 : 1) * std::abs(dx) / *ratio;
        else
            dx = (dx < 0 ? -1 : 1) * std::abs(dy) * *ratio;
    }
    if (symmetric)
        return snapped(QRectF(start.x() - std::abs(dx), start.y() - std::abs(dy), std::abs(dx) * 2, std::abs(dy) * 2));
    return snapped(QRectF(std::min(start.x(), start.x() + dx), std::min(start.y(), start.y() + dy), std::abs(dx), std::abs(dy)));
}

QRectF CropDrag::updated(QPointF point, std::optional<double> ratio, bool symmetric) const
{
    switch (mode.kind) {
    case Kind::create: return CropGeometry::create(start, point, ratio, symmetric);
    case Kind::move: return CropGeometry::snapped(original.translated(point - start));
    case Kind::resize: {
        const TransformDrag drag{.original = LayerTransform{.origin = original.topLeft(), .size = original.size()},
                                 .start = start,
                                 .mode = {TransformDrag::Kind::resize, mode.index},
                                 .originalCorners = std::nullopt};
        const LayerTransform next = drag.updated(point, ratio.has_value(), false, symmetric);
        return CropGeometry::snapped(QRectF(next.origin, next.size));
    }
    }
    throw std::logic_error("unknown crop drag");
}

namespace {
std::optional<double> nearest(double value, const std::vector<double> &targets, double tolerance)
{
    std::optional<double> best;
    for (const double target : targets) {
        if (std::abs(target - value) > tolerance || (best && std::abs(*best - value) <= std::abs(target - value)))
            continue;
        best = target;
    }
    return best;
}

// The smallest shift that lands an edge on a target.
double shift(std::initializer_list<double> edges, const std::vector<double> &targets, double tolerance)
{
    std::optional<double> best;
    for (const double edge : edges) {
        const std::optional<double> target = nearest(edge, targets, tolerance);
        if (target && (!best || std::abs(*target - edge) < std::abs(*best)))
            best = *target - edge;
    }
    return best.value_or(0);
}
}

// Moves snap their closest edges; the rest, the dragged side's.
QRectF CropSnap::apply(QRectF rect, const CropDrag &drag, QPointF point, std::optional<double> ratio, bool symmetric) const
{
    if (!(tolerance > 0))
        return rect;
    bool horizontal = true, vertical = true;
    switch (drag.mode.kind) {
    case CropDrag::Kind::move: return rect.translated(shift({rect.left(), rect.right()}, xs, tolerance), shift({rect.top(), rect.bottom()}, ys, tolerance));
    // A fixed ratio snaps moves alone, so it stays exact.
    case CropDrag::Kind::create:
        if (ratio)
            return rect;
        break;
    case CropDrag::Kind::resize:
        if (ratio)
            return rect;
        horizontal = LayerTransform::handles[size_t(drag.mode.index)].x() != 0.5;
        vertical = LayerTransform::handles[size_t(drag.mode.index)].y() != 0.5;
        break;
    }
    QRectF result = rect;
    if (horizontal) {
        if (std::abs(point.x() - result.left()) <= std::abs(point.x() - result.right())) {
            if (const std::optional<double> x = nearest(result.left(), xs, tolerance); x && *x < result.right())
                result = QRectF(*x, result.top(), result.right() - *x, result.height());
        } else if (const std::optional<double> x = nearest(result.right(), xs, tolerance); x && *x > result.left()) {
            result.setWidth(*x - result.left());
        }
    }
    if (vertical) {
        if (std::abs(point.y() - result.top()) <= std::abs(point.y() - result.bottom())) {
            if (const std::optional<double> y = nearest(result.top(), ys, tolerance); y && *y < result.bottom())
                result = QRectF(result.left(), *y, result.width(), result.bottom() - *y);
        } else if (const std::optional<double> y = nearest(result.bottom(), ys, tolerance); y && *y > result.top()) {
            result.setHeight(*y - result.top());
        }
    }
    if (symmetric) {
        // The snapped edge sets the half; its opposite mirrors it.
        const QPointF centre = drag.mode.kind == CropDrag::Kind::create ? drag.start : drag.original.center();
        if (horizontal) {
            const double half = point.x() >= centre.x() ? result.right() - centre.x() : centre.x() - result.left();
            if (half >= 0.5)
                result = QRectF(centre.x() - half, result.top(), half * 2, result.height());
        }
        if (vertical) {
            const double half = point.y() >= centre.y() ? result.bottom() - centre.y() : centre.y() - result.top();
            if (half >= 0.5)
                result = QRectF(result.left(), centre.y() - half, result.width(), half * 2);
        }
    }
    return result;
}

QRectF TransformSnap::box(const LayerTransform &transform)
{
    const Corners corners = DistortWarp::corners(transform);
    QPointF low = corners[0], high = corners[0];
    for (const QPointF corner : corners) {
        low = {std::min(low.x(), corner.x()), std::min(low.y(), corner.y())};
        high = {std::max(high.x(), corner.x()), std::max(high.y(), corner.y())};
    }
    return QRectF(low, high);
}

// View > Snap To's targets, centres included.
SnapGuides EditorSession::transformSnapTargets(const QSet<QUuid> &moving) const
{
    return alignmentSnapTargets(moving, true);
}

// `draft` nudged onto a nearby edge or middle, within `tolerance`.
LayerTransform EditorSession::snappedMove(const LayerTransform &draft, const QSet<QUuid> &moving, double tolerance)
{
    if (!m_snappingEnabled) {
        snapGuides = {};
        return draft;
    }
    const SnapGuides targets = transformSnapTargets(moving);
    const TransformSnap::Offset snap = TransformSnap::offset(TransformSnap::box(draft), targets.xs, targets.ys, tolerance);
    snapGuides = {snap.x ? std::vector<double>{*snap.x} : std::vector<double>{}, snap.y ? std::vector<double>{*snap.y} : std::vector<double>{}};
    if (snap.offset.isNull())
        return draft;
    LayerTransform snapped = draft;
    snapped.origin += QPointF(snap.offset.width(), snap.offset.height());
    return snapped;
}

// A resize handle's point, nudged onto targets; upright only.
QPointF EditorSession::snappedResizePoint(QPointF point, const TransformDrag &drag, bool proportional, const QSet<QUuid> &moving,
                                          double tolerance, const std::function<LayerTransform(QPointF)> &update)
{
    if (!m_snappingEnabled || drag.mode.kind != TransformDrag::Kind::resize || drag.original.radians() != 0) {
        snapGuides = {};
        return point;
    }
    const QPointF handle = LayerTransform::handles[size_t(drag.mode.index)];
    const SnapGuides targets = transformSnapTargets(moving);
    // The dragged handle's place tells its edge apart.
    const QPointF at = drag.original.point(handle) + point - drag.start;
    const auto edge = [at](const LayerTransform &transform, bool horizontal) {
        const QRectF box(transform.origin, transform.size);
        if (horizontal)
            return std::abs(box.left() - at.x()) <= std::abs(box.right() - at.x()) ? box.left() : box.right();
        return std::abs(box.top() - at.y()) <= std::abs(box.bottom() - at.y()) ? box.top() : box.bottom();
    };
    struct Snap {
        bool horizontal;
        double target;
    };
    const LayerTransform draft = update(point);
    std::vector<Snap> snaps;
    if (handle.x() != 0.5)
        if (const std::optional<double> x = nearest(edge(draft, true), targets.xs, tolerance))
            snaps.push_back({true, *x});
    if (handle.y() != 0.5)
        if (const std::optional<double> y = nearest(edge(draft, false), targets.ys, tolerance))
            snaps.push_back({false, *y});
    // Proportional, the nearer edge snaps; the ratio moves the other.
    if (proportional && snaps.size() == 2) {
        const auto distance = [&](const Snap &snap) { return std::abs(snap.target - edge(draft, snap.horizontal)); };
        snaps = {distance(snaps[1]) < distance(snaps[0]) ? snaps[1] : snaps[0]};
    }
    // Edges follow the pointer linearly: one pixel measures the step.
    QPointF result = point;
    for (const Snap &snap : snaps) {
        const double before = edge(update(result), snap.horizontal);
        const QPointF nudged = result + (snap.horizontal ? QPointF(1, 0) : QPointF(0, 1));
        const double perPixel = edge(update(nudged), snap.horizontal) - before;
        if (std::abs(perPixel) <= 0.01)
            continue;
        const double shift = (snap.target - before) / perPixel;
        result += snap.horizontal ? QPointF(shift, 0) : QPointF(0, shift);
    }
    snapGuides = {};
    for (const Snap &snap : snaps)
        (snap.horizontal ? snapGuides.xs : snapGuides.ys).push_back(snap.target);
    return result;
}

// View > Snap To's targets, without the centres.
SnapGuides EditorSession::cropSnapTargets() const
{
    return alignmentSnapTargets({}, false);
}

QPointF EditorSession::snappedPoint(QPointF point, double tolerance)
{
    if (!m_snappingEnabled) {
        snapGuides = {};
        return point;
    }
    const SnapGuides targets = cropSnapTargets();
    // The first of the nearest, as Swift's `min`.
    const auto nearest = [tolerance](double value, const std::vector<double> &lines) {
        std::optional<double> best;
        for (const double line : lines)
            if (std::abs(line - value) <= tolerance && (!best || std::abs(line - value) < std::abs(*best - value)))
                best = line;
        return best;
    };
    const std::optional<double> x = nearest(point.x(), targets.xs), y = nearest(point.y(), targets.ys);
    snapGuides = {x ? std::vector<double>{*x} : std::vector<double>{}, y ? std::vector<double>{*y} : std::vector<double>{}};
    return QPointF(x.value_or(point.x()), y.value_or(point.y()));
}

QSizeF EditorSession::snappedSelectionOffset(QSizeF offset, double tolerance, bool horizontal, bool vertical)
{
    if (!m_snappingEnabled || !m_selectionMoveOrigin) {
        snapGuides = {};
        return offset;
    }
    const QSizeF whole(std::round(offset.width()), std::round(offset.height()));
    const QRectF box = m_selectionMoveOrigin->path.boundingRect().translated(whole.width(), whole.height());
    const SnapGuides targets = cropSnapTargets();
    const TransformSnap::Offset snap =
        TransformSnap::offset(box, horizontal ? targets.xs : std::vector<double>{}, vertical ? targets.ys : std::vector<double>{}, tolerance);
    snapGuides = {snap.x ? std::vector<double>{*snap.x} : std::vector<double>{}, snap.y ? std::vector<double>{*snap.y} : std::vector<double>{}};
    return whole + snap.offset;
}

std::optional<QRectF> EditorSession::visibleCropRect() const
{
    if (m_tool != NavigationTool::crop || !m_document)
        return std::nullopt;
    return m_cropRect ? m_cropRect : QRectF(QPointF(0, 0), m_document->size());
}

std::optional<double> EditorSession::cropRatio() const
{
    if (m_cropRatioChoice == QStringLiteral("Original"))
        return m_document ? std::optional(double(m_document->width) / double(m_document->height)) : std::nullopt;
    if (m_cropRatioChoice == QStringLiteral("1:1"))
        return 1;
    if (m_cropRatioChoice == QStringLiteral("4:3"))
        return 4.0 / 3;
    if (m_cropRatioChoice == QStringLiteral("3:4"))
        return 3.0 / 4;
    if (m_cropRatioChoice == QStringLiteral("16:9"))
        return 16.0 / 9;
    if (m_cropRatioChoice == QStringLiteral("9:16"))
        return 9.0 / 16;
    return std::nullopt;
}

void EditorSession::cancelCrop()
{
    setCropRect(std::nullopt);
}

// The frame takes the ratio about its middle, width kept.
void EditorSession::changeCropRatio()
{
    const std::optional<QRectF> rect = visibleCropRect();
    const std::optional<double> ratio = cropRatio();
    if (!rect || !ratio)
        return;
    const double height = rect->width() / *ratio;
    const QRectF next = CropGeometry::snapped(QRectF(rect->left(), rect->center().y() - height / 2, rect->width(), height));
    if (CropGeometry::valid(next))
        setCropRect(next);
}

// The canvas cut to the frame off the UI thread.
void EditorSession::commitCrop(std::function<void()> done)
{
    bool started = false;
    if (canStartProjectOperation() && m_cropRect && CropGeometry::valid(*m_cropRect)) {
        // Swift reads a CGRect's width and minX standardised.
        const QRectF rect = m_cropRect->normalized();
        const CanvasSizeOptions options{.width = qint64(rect.width()), .height = qint64(rect.height()), .contentOffset = QPointF(-rect.left(), -rect.top())};
        // Taken before anything changes: running out changes nothing.
        try {
            std::optional<ProjectSnapshot> snapshot = projectSnapshot();
            if (snapshot) {
                m_cropCommit.setFuture(QtConcurrent::run([snapshot = std::move(*snapshot), options]() -> Cropped {
                    try {
                        return Cropped{CanvasResizer::resize(snapshot, options), std::nullopt};
                    } catch (const ProjectError &error) {
                        return Cropped{std::nullopt, QString::fromUtf8(error.what())};
                    } catch (const ExportError &error) {
                        return Cropped{std::nullopt, QString::fromUtf8(error.what())};
                    }
                }));
                started = true;
            }
        } catch (const std::bad_alloc &) {
            setCropError(QString::fromUtf8(ExportError(ExportError::Kind::render).what()));
        }
    }
    if (!started) {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
        return;
    }
    m_committingCrop = std::move(done);
    setIsProjectBusy(true);
}

void EditorSession::finishCropCommit()
{
    const std::function<void()> done = std::exchange(m_committingCrop, {});
    try {
        // Moved out: a large project's copy could fail.
        const Cropped result = m_cropCommit.future().takeResult();
        if (result.snapshot) {
            applyDocumentSize(*result.snapshot, QStringLiteral("Crop"));
            // Only a crop that landed lets its frame go.
            m_cropRect.reset();
        } else {
            setCropError(result.failure);
        }
    } catch (const std::bad_alloc &) {
        setCropError(QString::fromUtf8(ExportError(ExportError::Kind::render).what()));
    }
    setIsProjectBusy(false);
    if (done)
        QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
}
