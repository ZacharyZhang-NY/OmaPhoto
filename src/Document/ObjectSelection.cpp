#include "Document/ObjectSelection.h"
#include "Document/GuidedMatte.h"
#include "Document/EditorSession.h"
#include "Document/MagicWand.h"
#include "Document/SubjectRemoval.h"
#include "IO/ImageExporter.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <QtConcurrent>
#include <limits>

namespace {
double perpendicularDistance(QPointF point, QPointF a, QPointF b)
{
    const double dx = b.x() - a.x(), dy = b.y() - a.y();
    const double length = std::hypot(dx, dy);
    if (!(length > 0))
        return std::hypot(point.x() - a.x(), point.y() - a.y());
    return std::abs(dy * point.x() - dx * point.y() + b.x() * a.y() - b.y() * a.x()) / length;
}

// Swift's Douglas–Peucker on an open polyline.
std::vector<QPointF> simplifyOpen(const std::vector<QPointF> &points, size_t first, size_t last, double tolerance)
{
    if (last <= first + 1)
        return {points[first], points[last]};
    size_t farthest = first + 1;
    double greatest = 0;
    for (size_t index = first + 1; index < last; ++index) {
        const double distance = perpendicularDistance(points[index], points[first], points[last]);
        if (distance > greatest) {
            greatest = distance;
            farthest = index;
        }
    }
    if (!(greatest > tolerance))
        return {points[first], points[last]};
    std::vector<QPointF> left = simplifyOpen(points, first, farthest, tolerance);
    const std::vector<QPointF> right = simplifyOpen(points, farthest, last, tolerance);
    left.pop_back();
    left.insert(left.end(), right.begin(), right.end());
    return left;
}

std::vector<QPointF> simplifyClosed(const std::vector<QPointF> &points, double tolerance)
{
    if (points.size() < 4)
        return points;
    // Break at a stable extreme so the whole contour survives.
    const size_t start = size_t(std::min_element(points.begin(), points.end(), [](QPointF lhs, QPointF rhs) {
                                    return lhs.x() == rhs.x() ? lhs.y() < rhs.y() : lhs.x() < rhs.x();
                                }) - points.begin());
    std::vector<QPointF> open(points.begin() + std::ptrdiff_t(start), points.end());
    open.insert(open.end(), points.begin(), points.begin() + std::ptrdiff_t(start));
    open.push_back(open.front());
    open = simplifyOpen(open, 0, open.size() - 1, tolerance);
    // Its ends are the same point.
    open.pop_back();
    return open.size() >= 3 ? open : points;
}

std::vector<QPointF> chaikin(std::vector<QPointF> points, int iterations)
{
    for (int round = 0; round < iterations; ++round) {
        std::vector<QPointF> next;
        next.reserve(points.size() * 2);
        for (size_t index = 0; index < points.size(); ++index) {
            const QPointF a = points[index], b = points[(index + 1) % points.size()];
            next.push_back(a * 0.75 + b * 0.25);
            next.push_back(a * 0.25 + b * 0.75);
        }
        points = std::move(next);
    }
    return points;
}

bool any(const std::vector<uchar> &mask, int width, int height, int x, int y, bool set)
{
    for (int ny = std::max(0, y - 1); ny <= std::min(height - 1, y + 1); ++ny) {
        for (int nx = std::max(0, x - 1); nx <= std::min(width - 1, x + 1); ++nx) {
            if ((mask[size_t(ny) * size_t(width) + size_t(nx)] != 0) == set)
                return true;
        }
    }
    return false;
}

std::optional<QPainterPath> traced(const QImage &image, QPointF point, int edgeOffset, bool smoothEdges)
{
    const int width = image.width(), height = image.height();
    const int x = int(std::floor(point.x())), y = int(std::floor(point.y()));
    if (!std::isfinite(point.x()) || !std::isfinite(point.y()) || x < 0 || x >= width || y < 0 || y >= height)
        return std::nullopt;
    QImage foreground;
    try {
        foreground = SubjectRemoval::foreground(image);
    } catch (const SubjectRemovalError &error) {
        // No subject anywhere: Vision finds no instance.
        if (error.kind == SubjectRemovalError::Kind::noSubject)
            return std::nullopt;
        throw;
    }
    using ObjectSelection::grid;
    // Regions on the model's grid stand for Vision's instances.
    const QImage cells = foreground.scaled(grid, grid, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_Grayscale8);
    if (cells.isNull())
        throw ExportError(ExportError::Kind::render);
    std::vector<uchar> binary(size_t(grid) * grid);
    for (int row = 0; row < grid; ++row) {
        for (int column = 0; column < grid; ++column)
            binary[size_t(row) * grid + size_t(column)] = cells.constScanLine(row)[column] >= 128;
    }
    const int gx = std::clamp(int(point.x() / width * grid), 0, grid - 1), gy = std::clamp(int(point.y() / height * grid), 0, grid - 1);
    const std::optional<std::vector<uchar>> coarse = ObjectSelection::region(binary, grid, grid, gx, gy);
    if (!coarse)
        return std::nullopt;
    QImage small(grid, grid, QImage::Format_Grayscale8);
    if (small.isNull())
        throw ExportError(ExportError::Kind::render);
    for (int row = 0; row < grid; ++row) {
        for (int column = 0; column < grid; ++column)
            small.scanLine(row)[column] = (*coarse)[size_t(row) * grid + size_t(column)] ? 255 : 0;
    }
    // Swift's edge-preserving upsampling: the image guides the edge.
    const QImage up = small.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const QImage refined = GuidedMatte::refine(up, image, 5, std::numeric_limits<double>::max());
    std::vector<uchar> mask(size_t(width) * size_t(height));
    for (int row = 0; row < height; ++row) {
        const uchar *line = refined.constScanLine(row);
        for (int column = 0; column < width; ++column)
            mask[size_t(row) * size_t(width) + size_t(column)] = line[column] >= 128 ? 255 : 0;
    }
    mask = ObjectSelection::adjusted(std::move(mask), width, height, edgeOffset);
    const std::optional<QPainterPath> outline = MagicWand::outline(mask, width, height);
    if (!outline)
        return std::nullopt;
    return smoothEdges ? ObjectSelection::smoothed(*outline) : *outline;
}
}

std::optional<QPainterPath> ObjectSelection::select(const QImage &image, QPointF point, int edgeOffset, bool smoothEdges)
{
    try {
        return traced(image, point, edgeOffset, smoothEdges);
    } catch (const std::bad_alloc &) {
        // Masks the size of the image found no memory.
        throw ExportError(ExportError::Kind::render);
    }
}

std::optional<std::vector<uchar>> ObjectSelection::region(const std::vector<uchar> &mask, int width, int height, int x, int y)
{
    const auto at = [width](int column, int row) { return size_t(row) * size_t(width) + size_t(column); };
    if (!mask[at(x, y)])
        return std::nullopt;
    std::vector<uchar> result(mask.size());
    std::deque<std::pair<int, int>> pending{{x, y}};
    // Eight neighbours join a region, the cell itself included.
    while (!pending.empty()) {
        const auto [column, row] = pending.front();
        pending.pop_front();
        for (int ny = std::max(0, row - 1); ny <= std::min(height - 1, row + 1); ++ny) {
            for (int nx = std::max(0, column - 1); nx <= std::min(width - 1, column + 1); ++nx) {
                if (mask[at(nx, ny)] && !result[at(nx, ny)]) {
                    result[at(nx, ny)] = 1;
                    pending.emplace_back(nx, ny);
                }
            }
        }
    }
    return result;
}

std::vector<uchar> ObjectSelection::adjusted(std::vector<uchar> mask, int width, int height, int edgeOffset)
{
    const int steps = std::min(10, std::abs(edgeOffset));
    for (int step = 0; step < steps; ++step) {
        std::vector<uchar> result = mask;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const size_t index = size_t(y) * size_t(width) + size_t(x);
                if (edgeOffset > 0 && mask[index])
                    result[index] = any(mask, width, height, x, y, false) ? 0 : 255;
                else if (edgeOffset < 0 && !mask[index] && any(mask, width, height, x, y, true))
                    result[index] = 255;
            }
        }
        mask = std::move(result);
    }
    return mask;
}

QPainterPath ObjectSelection::smoothed(const QPainterPath &path)
{
    std::vector<std::vector<QPointF>> subpaths;
    std::vector<QPointF> current;
    const auto finish = [&] {
        // Qt's close repeats the start, where a CGPath's adds none.
        if (!current.empty() && current.front() == current.back())
            current.pop_back();
        if (current.size() >= 3)
            subpaths.push_back(current);
    };
    for (int index = 0; index < path.elementCount(); ++index) {
        const QPainterPath::Element element = path.elementAt(index);
        if (element.isMoveTo()) {
            finish();
            current = {element};
        } else if (element.isLineTo()) {
            current.push_back(element);
        } else if (element.type == QPainterPath::CurveToDataElement && (index + 1 == path.elementCount() || path.elementAt(index + 1).type != QPainterPath::CurveToDataElement)) {
            // A curve's end point, as Swift keeps it.
            current.push_back(element);
        }
    }
    finish();
    QPainterPath result;
    result.setFillRule(path.fillRule());
    for (const std::vector<QPointF> &subpath : subpaths) {
        const std::vector<QPointF> points = chaikin(simplifyClosed(subpath, 1.6), 3);
        result.moveTo(points.front());
        for (size_t index = 1; index < points.size(); ++index)
            result.lineTo(points[index]);
        result.closeSubpath();
    }
    return result;
}

void EditorSession::setWandMode(WandMode mode)
{
    m_wandMode = mode;
    notify();
}

void EditorSession::setObjectSelectionSettings(const ObjectSelectionSettings &settings)
{
    m_objectSelectionSettings = settings;
    notify();
}

void EditorSession::selectObject(QPointF point, SelectionMode mode, std::function<void()> done)
{
    // The caller resumes from the event loop, as after await.
    const auto finish = [this, done] {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
    };
    const std::optional<CanvasDocument> &document = m_document;
    // Swift's positive bounds refuse a point that is no number.
    const bool inside = document && point.x() >= 0 && point.y() >= 0 && point.x() < document->width && point.y() < document->height;
    const std::optional<QImage> sample = canEditSelection() && !m_selectionMoveOrigin && inside
        ? selectionSample(*document, m_objectSelectionSettings.sampleAllLayers)
        : std::nullopt;
    if (!sample) {
        finish();
        return;
    }
    setIsProjectBusy(true);
    m_wanding = Wanding{mode, document->id, std::move(done), QStringLiteral("Object Selection")};
    m_wand.setFuture(QtConcurrent::run([sample = *sample, point, offset = m_objectSelectionSettings.edgeOffset, smooth = m_selectionAntialiased]() -> Wanded {
        try {
            return Wanded{ObjectSelection::select(sample, point, offset, smooth), std::nullopt};
        } catch (const SubjectRemovalError &error) {
            return Wanded{std::nullopt, QString::fromUtf8(error.what())};
        } catch (const MagicWandError &error) {
            return Wanded{std::nullopt, QString::fromUtf8(error.what())};
        } catch (const ExportError &error) {
            return Wanded{std::nullopt, QString::fromUtf8(error.what())};
        }
    }));
}
