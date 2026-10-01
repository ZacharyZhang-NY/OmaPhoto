#include "Document/SmudgeLiquify.h"
#include "Document/EditorSession.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore.h"
#include "Rendering/LayerRenderer.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

QString rawValue(BrushToolMode mode)
{
    switch (mode) {
    case BrushToolMode::paint:
        return QStringLiteral("Paint");
    case BrushToolMode::erase:
        return QStringLiteral("Erase");
    }
    throw std::logic_error("unknown brush mode");
}

QString rawValue(BlurToolMode mode)
{
    switch (mode) {
    case BlurToolMode::liquify:
        return QStringLiteral("Liquify");
    case BlurToolMode::blur:
        return QStringLiteral("Blur");
    case BlurToolMode::smudge:
        return QStringLiteral("Smudge");
    }
    throw std::logic_error("unknown smear mode");
}

WarpStroke::WarpStroke(const ImageLayer &layer, const QImage &image, const LayerTransform &transform, QSizeF canvas, BlurToolMode mode,
                       const BrushSettings &settings)
    : layer(layer), mode(mode), diameter(std::max(2.0, settings.diameter)), hardness(std::min(0.98, std::max(0.0, settings.hardness))),
      strength(std::min(1.0, std::max(0.01, settings.opacity))), width(int(canvas.width())), height(int(canvas.height())),
      m_context(BrushRaster::context(width, height, false))
{
    QPainter painter(&m_context);
    LayerRenderer::draw(image, transform, transform.center(), painter, {});
}

int WarpStroke::radius() const
{
    return int(std::ceil(diameter / 2));
}

float WarpStroke::weight(float u) const
{
    // Swift's positive guards: a number that is none moves nothing.
    if (!(u < 1))
        return 0;
    const float h = float(hardness);
    if (!(u > h))
        return 1;
    const float t = (1 - u) / (1 - h);
    return t * t * (3 - 2 * t);
}

void WarpStroke::append(QPointF point)
{
    if (!m_last) {
        m_last = point;
        if (mode == BlurToolMode::smudge)
            pickUp(point);
        return;
    }
    const QPointF from = *m_last;
    const double distance = std::hypot(point.x() - from.x(), point.y() - from.y());
    const double spacing = std::max(1.0, diameter * (mode == BlurToolMode::smudge ? 0.08 : 0.025));
    if (distance < spacing)
        return;
    const int steps = int(std::ceil(distance / spacing));
    QPointF previous = from;
    for (int step = 1; step <= steps; ++step) {
        const double t = double(step) / double(steps);
        const QPointF next(from.x() + (point.x() - from.x()) * t, from.y() + (point.y() - from.y()) * t);
        if (mode == BlurToolMode::smudge)
            smudge(next);
        else
            push(previous, next);
        m_points.push_back(next);
        previous = next;
    }
    m_last = point;
}

void WarpStroke::pickUp(QPointF center)
{
    const int r = radius(), side = 2 * r + 1;
    m_carried.assign(size_t(side) * size_t(side) * 4, 0);
    const int cx = int(std::round(center.x())), cy = int(std::round(center.y()));
    const uchar *pixels = m_context.constBits();
    const qsizetype stride = m_context.bytesPerLine();
    for (int dy = -r; dy <= r; ++dy) {
        const int y = cy + dy;
        if (y < 0 || y >= height)
            continue;
        for (int dx = -r; dx <= r; ++dx) {
            const int x = cx + dx;
            if (x < 0 || x >= width)
                continue;
            const uchar *p = pixels + y * stride + x * 4;
            float *c = m_carried.data() + (size_t(dy + r) * size_t(side) + size_t(dx + r)) * 4;
            for (int k = 0; k < 4; ++k)
                c[k] = float(p[k]);
        }
    }
}

void WarpStroke::smudge(QPointF center)
{
    const int r = radius(), side = 2 * r + 1;
    const int cx = int(std::round(center.x())), cy = int(std::round(center.y()));
    const float keep = float(strength), invR = 1 / float(diameter / 2);
    uchar *pixels = m_context.bits();
    const qsizetype stride = m_context.bytesPerLine();
    for (int dy = -r; dy <= r; ++dy) {
        const int y = cy + dy;
        if (y < 0 || y >= height)
            continue;
        for (int dx = -r; dx <= r; ++dx) {
            const int x = cx + dx;
            if (x < 0 || x >= width)
                continue;
            const float w = weight(std::sqrt(float(dx * dx + dy * dy)) * invR);
            if (!(w > 0))
                continue;
            uchar *p = pixels + y * stride + x * 4;
            float *c = m_carried.data() + (size_t(dy + r) * size_t(side) + size_t(dx + r)) * 4;
            for (int k = 0; k < 4; ++k) {
                const float under = float(p[k]);
                const float painted = under + (c[k] - under) * w;
                p[k] = uchar(std::max(0.0f, std::min(255.0f, std::round(painted))));
                // The brush picks up what it left; weaker, more so.
                c[k] = painted + (c[k] - painted) * keep;
            }
        }
    }
}

void WarpStroke::push(QPointF a, QPointF b)
{
    const int r = radius();
    const float moveX = float(b.x() - a.x()) * float(strength), moveY = float(b.y() - a.y()) * float(strength);
    const int margin = int(std::ceil(std::max(std::abs(moveX), std::abs(moveY)))) + 2;
    const int cx = int(std::round(b.x())), cy = int(std::round(b.y()));
    // A copy of the area before this dab, sampled.
    const int x0 = std::max(0, cx - r - margin), x1 = std::min(width - 1, cx + r + margin);
    const int y0 = std::max(0, cy - r - margin), y1 = std::min(height - 1, cy + r + margin);
    if (x0 > x1 || y0 > y1)
        return;
    const int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
    if (m_scratch.size() < size_t(cw) * size_t(ch) * 4)
        m_scratch.assign(size_t(cw) * size_t(ch) * 4, 0);
    uchar *pixels = m_context.bits();
    const qsizetype stride = m_context.bytesPerLine();
    for (int y = 0; y < ch; ++y) {
        const uchar *row = pixels + (y + y0) * stride + x0 * 4;
        std::copy(row, row + size_t(cw) * 4, m_scratch.begin() + std::ptrdiff_t(size_t(y) * size_t(cw) * 4));
    }
    const float invR = 1 / float(diameter / 2);
    const float *s = m_scratch.data();
    for (int dy = -r; dy <= r; ++dy) {
        const int y = cy + dy;
        if (y < y0 || y > y1)
            continue;
        for (int dx = -r; dx <= r; ++dx) {
            const int x = cx + dx;
            if (x < x0 || x > x1)
                continue;
            const float w = weight(std::sqrt(float(dx * dx + dy * dy)) * invR);
            if (!(w > 0))
                continue;
            // Bilinear from the old pixels, behind the brush's travel.
            const float sx = std::min(float(cw - 1), std::max(0.0f, float(x - x0) - moveX * w));
            const float sy = std::min(float(ch - 1), std::max(0.0f, float(y - y0) - moveY * w));
            const int ix = std::min(cw - 2, int(sx)), iy = std::min(ch - 2, int(sy));
            if (ix < 0 || iy < 0)
                continue;
            const float fx = sx - float(ix), fy = sy - float(iy);
            uchar *p = pixels + y * stride + x * 4;
            const size_t s00 = (size_t(iy) * size_t(cw) + size_t(ix)) * 4, s10 = s00 + 4, s01 = s00 + size_t(cw) * 4, s11 = s01 + 4;
            for (int k = 0; k < 4; ++k) {
                const float top = s[s00 + k] + (s[s10 + k] - s[s00 + k]) * fx;
                const float bottom = s[s01 + k] + (s[s11 + k] - s[s01 + k]) * fx;
                p[k] = uchar(std::max(0.0f, std::min(255.0f, std::round(top + (bottom - top) * fy))));
            }
        }
    }
}

// Swift's EditorSession extension: a warp from press to commit.
void EditorSession::beginWarp(QPointF point)
{
    const std::optional<ImageLayer> layer = activeLayer();
    if (!canPaint() || m_isMaskSelected || !layer || !layer->asset || !m_document) {
        setBrushError(m_isMaskSelected ? std::optional(QStringLiteral("Smudge and Liquify work on a layer's pixels, not its mask.")) : paintRefusal());
        return;
    }
    finishOpacityEdit();
    try {
        // A painted asset flattens here; the catch covers it too.
        auto stroke = std::make_unique<WarpStroke>(*layer, layer->asset->image(), displayedTransform(*layer), m_document->size(), m_blurMode,
                                                   m_brushSettings);
        stroke->append(point);
        m_warpStroke = std::move(stroke);
        m_lastBrushPoint = LastBrushPoint{point, layer->id, false};
        ++m_brushRevision;
        resumeFileRequests();
        notify();
    } catch (const ExportError &error) {
        setBrushError(QString::fromUtf8(error.what()));
    }
}

// The result painted along the stroke, one step.
void EditorSession::finishWarp()
{
    if (!m_warpStroke)
        return;
    const std::unique_ptr<WarpStroke> warp = std::move(m_warpStroke);
    ++m_brushRevision;
    resumeFileRequests();
    const int index = m_document ? indexOf(m_document->layers, warp->layer.id) : -1;
    if (!warp->points().empty() && index >= 0) {
        const ImageLayer current = m_document->layers[index];
        if (current.asset && current.asset->identity() == warp->layer.asset.value().identity() && current.transform == warp->layer.transform) {
            try {
                BrushSettings settings = m_brushSettings;
                // A slightly wider hard tip covers all that moved.
                settings.diameter = warp->diameter + 4;
                settings.hardness = 1;
                settings.opacity = 1;
                const std::unique_ptr<BrushStroke> stroke = makeRasterEdit(current, settings);
                stroke->clone = BrushStroke::Clone{warp->image(), QRectF(QPointF(0, 0), warp->image().size()), false};
                stroke->replacesWithClone = true;
                stroke->editName = rawValue(warp->mode);
                for (const QPointF step : warp->points())
                    stroke->append(step);
                stroke->flush();
                if (!stroke->patches().empty())
                    commitPaintSnapshot(*stroke);
            } catch (const ProjectError &error) {
                setBrushError(QString::fromUtf8(error.what()));
            } catch (const ExportError &error) {
                setBrushError(QString::fromUtf8(error.what()));
            }
        }
    }
    notify();
}
