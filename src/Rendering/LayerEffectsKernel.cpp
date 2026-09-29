#include "Rendering/LayerEffectsKernel.h"
#include "Document/DocumentLimits.h"
#include "Rendering/PoolMap.h"
#include "Document/BrushStroke.h"
#include "IO/ImageExporter.h"
#include <QtConcurrent>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

// Metal's kernels, pass after pass, in float as the shader.
namespace {
using Buffer = std::unique_ptr<float[]>;

// Allocated by the caller, where a failure throws.
template <typename Value> std::unique_ptr<Value[]> allocated(qint64 count)
{
    std::unique_ptr<Value[]> made(new (std::nothrow) Value[size_t(count)]);
    if (!made)
        throw ExportError(ExportError::Kind::render);
    return made;
}

Buffer buffer(qint64 count)
{
    return allocated<float>(count);
}

// Bands of 32 lines side by side: Metal's thread groups.
void bands(int count, const std::function<void(int band, int line)> &line)
{
    std::vector<int> firsts;
    for (int first = 0; first < count; first += 32)
        firsts.push_back(first);
    PoolMap::blocking(firsts, [&](int first) {
        for (int index = first; index < std::min(count, first + 32); ++index)
            line(first / 32, index);
    });
}

float mixed(float from, float to, float amount)
{
    return from + (to - from) * amount;
}

// Swift's extreme: a sliding window, each index in once.
void spreadLine(const float *input, float *output, int count, qsizetype step, int reach, bool smallest, int *queue)
{
    int head = 0, tail = 0, next = 0;
    for (int center = 0; center < count; ++center) {
        while (next <= std::min(count - 1, center + reach)) {
            const float value = input[next * step];
            while (tail > head) {
                const float previous = input[queue[tail - 1] * step];
                if (smallest ? previous < value : previous > value)
                    break;
                --tail;
            }
            queue[tail++] = next++;
        }
        while (head < tail && queue[head] < center - reach)
            ++head;
        // Past the edges Metal reads nothing: the smallest is none.
        const bool outside = center < reach || center + reach >= count;
        output[center * step] = smallest && outside ? 0.f : input[queue[head] * step];
    }
}

// Metal's spread, rows then columns: its loops' maxima.
void spread(const float *source, float *across, float *result, int width, int height, int reach, bool smallest)
{
    // A queue a band, as long as that pass's lines.
    const qsizetype rows = qsizetype(height + 31) / 32 * width, columns = qsizetype(width + 31) / 32 * height;
    const std::unique_ptr<int[]> queues = allocated<int>(std::max(rows, columns));
    bands(height, [&](int band, int y) {
        spreadLine(source + qsizetype(y) * width, across + qsizetype(y) * width, width, 1, reach, smallest, queues.get() + qsizetype(band) * width);
    });
    bands(width, [&](int band, int x) { spreadLine(across + x, result + x, height, width, reach, smallest, queues.get() + qsizetype(band) * height); });
}

void shifted(const float *source, float *result, int width, int height, float dx, float dy)
{
    bands(height, [&](int, int y) {
        for (int x = 0; x < width; ++x) {
            const float sx = float(x) - dx, sy = float(y) - dy;
            float value = 0.f;
            // Between pixels, so a shadow moves smoothly.
            if (sx >= 0.f && sy >= 0.f && sx <= float(width - 1) && sy <= float(height - 1)) {
                const int x0 = int(std::floor(sx)), y0 = int(std::floor(sy));
                const int x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
                const float fx = sx - float(x0), fy = sy - float(y0);
                const float top = mixed(source[qsizetype(y0) * width + x0], source[qsizetype(y0) * width + x1], fx);
                const float bottom = mixed(source[qsizetype(y1) * width + x0], source[qsizetype(y1) * width + x1], fx);
                value = mixed(top, bottom, fy);
            }
            result[qsizetype(y) * width + x] = value;
        }
    });
}

// Metal's blur, rows then columns, taps added in order.
void blurred(float *values, float *scratch, int width, int height, float sigma)
{
    if (!(sigma > 0.01f))
        return;
    const int radius = std::max(1, int(std::lround(sigma * 3)));
    // Valid effects blur at most 500, a radius of 750.
    std::array<float, 1501> weights;
    if (radius > 750)
        throw std::logic_error("an effect blurs past its bounds");
    float total = 0;
    for (int offset = -radius; offset <= radius; ++offset) {
        weights[size_t(offset + radius)] = std::exp(-float(offset * offset) / (2.f * sigma * sigma));
        total += weights[size_t(offset + radius)];
    }
    bands(height, [&](int, int y) {
        const float *row = values + qsizetype(y) * width;
        for (int x = 0; x < width; ++x) {
            float sum = 0;
            for (int offset = -radius; offset <= radius; ++offset)
                sum += weights[size_t(offset + radius)] * row[std::clamp(x + offset, 0, width - 1)];
            scratch[qsizetype(y) * width + x] = sum / total;
        }
    });
    bands(height, [&](int, int y) {
        float *out = values + qsizetype(y) * width;
        std::fill(out, out + width, 0.f);
        for (int offset = -radius; offset <= radius; ++offset) {
            const float weight = weights[size_t(offset + radius)];
            const float *in = scratch + qsizetype(std::clamp(y + offset, 0, height - 1)) * width;
            for (int x = 0; x < width; ++x)
                out[x] += weight * in[x];
        }
        for (int x = 0; x < width; ++x)
            out[x] /= total;
    });
}

// Source over: a colour, at a coverage, onto the rest.
void over(float (&colour)[3], float &alpha, const PaletteColor &paint, float coverage)
{
    colour[0] = float(paint.red) * coverage + colour[0] * (1.f - coverage);
    colour[1] = float(paint.green) * coverage + colour[1] * (1.f - coverage);
    colour[2] = float(paint.blue) * coverage + colour[2] * (1.f - coverage);
    alpha = coverage + alpha * (1.f - coverage);
}

uchar byte(float value)
{
    return uchar(std::clamp(value, 0.f, 1.f) * 255.f + 0.5f);
}
}

QImage LayerEffectsKernel::render(const QImage &pixels, const LayerEffects &effects)
{
    const int width = pixels.width(), height = pixels.height();
    const qint64 count = qint64(width) * height;
    // Past Metal's 80 million Swift draws on its CPU.
    if (count <= 0 || count > DocumentLimits::maxSurfacePixels)
        throw ExportError(ExportError::Kind::tooLarge);
    const QImage source = pixels.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    if (source.isNull())
        throw ExportError(ExportError::Kind::render);
    const Buffer shape = buffer(count);
    bands(height, [&](int, int y) {
        const uchar *row = source.constScanLine(y);
        for (int x = 0; x < width; ++x)
            shape[qsizetype(y) * width + x] = float(row[x * 4 + 3]) / 255.f;
    });
    const std::optional<StrokeEffect> stroke = effects.stroke && effects.stroke->isEnabled() && effects.stroke->size > 0 && effects.stroke->opacity > 0
        ? effects.stroke : std::nullopt;
    Buffer ring;
    if (stroke) {
        const Buffer across = buffer(count), moved = buffer(count);
        ring = buffer(count);
        spread(shape.get(), across.get(), moved.get(), width, height, std::max(1, int(std::lround(stroke->size))), stroke->inside);
        for (qint64 index = 0; index < count; ++index)
            ring[index] = std::clamp(stroke->inside ? shape[index] - moved[index] : moved[index] - shape[index], 0.f, 1.f);
    }
    const std::optional<ShadowEffect> shadow = effects.shadow && effects.shadow->isEnabled() && effects.shadow->opacity > 0 ? effects.shadow : std::nullopt;
    Buffer cast;
    if (shadow) {
        cast = buffer(count);
        const QSizeF offset = shadow->offset();
        shifted(shape.get(), cast.get(), width, height, float(offset.width()), float(offset.height()));
        blurred(cast.get(), buffer(count).get(), width, height, float(shadow->blur / 2));
    }
    const std::optional<InnerShadowEffect> inner = effects.innerShadow && effects.innerShadow->isEnabled() && effects.innerShadow->opacity > 0
        ? effects.innerShadow : std::nullopt;
    Buffer inside;
    if (inner) {
        inside = buffer(count);
        const QSizeF offset = inner->offset();
        shifted(shape.get(), inside.get(), width, height, float(offset.width()), float(offset.height()));
        blurred(inside.get(), buffer(count).get(), width, height, float(inner->blur / 2));
        // What lies outside the layer, softened, kept to its shape.
        for (qint64 index = 0; index < count; ++index)
            inside[index] = std::clamp(shape[index] * (1.f - inside[index]), 0.f, 1.f);
    }
    const std::optional<OuterGlowEffect> glow = effects.outerGlow && effects.outerGlow->isEnabled() && effects.outerGlow->size > 0 && effects.outerGlow->opacity > 0
        ? effects.outerGlow : std::nullopt;
    Buffer glowing;
    if (glow) {
        // The shape softened every way, as Metal's glow buffer.
        glowing = buffer(count);
        std::copy(shape.get(), shape.get() + count, glowing.get());
        blurred(glowing.get(), buffer(count).get(), width, height, float(glow->size / 2));
    }
    const std::optional<InnerGlowEffect> innerGlow =
        effects.innerGlow && effects.innerGlow->isEnabled() && effects.innerGlow->size > 0 && effects.innerGlow->opacity > 0 ? effects.innerGlow : std::nullopt;
    Buffer glowingInside;
    if (innerGlow) {
        // The shape softened; what it loses stays inside.
        glowingInside = buffer(count);
        std::copy(shape.get(), shape.get() + count, glowingInside.get());
        blurred(glowingInside.get(), buffer(count).get(), width, height, float(innerGlow->size / 2));
        for (qint64 index = 0; index < count; ++index)
            glowingInside[index] = std::clamp(shape[index] * (1.f - glowingInside[index]), 0.f, 1.f);
    }
    const std::optional<ColorOverlayEffect> overlay = effects.colorOverlay && effects.colorOverlay->isEnabled() && effects.colorOverlay->opacity > 0
        ? effects.colorOverlay : std::nullopt;
    QImage result = BrushRaster::context(width, height, false);
    // Taken once: scanLine() detaches, which workers must not race.
    uchar *const bits = result.bits();
    const qsizetype stride = result.bytesPerLine();
    // Shadow, glow, outside stroke, pixels, overlay, inner glow, shadow, stroke.
    bands(height, [&](int, int y) {
        const uchar *in = source.constScanLine(y);
        uchar *out = bits + y * stride;
        for (int x = 0; x < width; ++x) {
            const qsizetype index = qsizetype(y) * width + x;
            float colour[3] = {0, 0, 0}, alpha = 0;
            if (shadow)
                over(colour, alpha, shadow->color(), std::clamp(cast[index] * float(shadow->opacity), 0.f, 1.f));
            if (glow)
                over(colour, alpha, glow->color(), std::clamp(glowing[index] * (1.f - shape[index]) * float(glow->opacity), 0.f, 1.f));
            const float strokeCoverage = stroke ? std::clamp(ring[index] * float(stroke->opacity), 0.f, 1.f) : 0.f;
            if (stroke && !stroke->inside)
                over(colour, alpha, stroke->color(), strokeCoverage);
            const float sourceAlpha = float(in[x * 4 + 3]) / 255.f;
            for (int channel = 0; channel < 3; ++channel)
                colour[channel] = float(in[x * 4 + channel]) / 255.f + colour[channel] * (1.f - sourceAlpha);
            alpha = sourceAlpha + alpha * (1.f - sourceAlpha);
            if (overlay)
                over(colour, alpha, overlay->color(), std::clamp(shape[index] * float(overlay->opacity), 0.f, 1.f));
            if (innerGlow)
                over(colour, alpha, innerGlow->color(), std::clamp(glowingInside[index] * float(innerGlow->opacity), 0.f, 1.f));
            if (inner)
                over(colour, alpha, inner->color(), std::clamp(inside[index] * float(inner->opacity), 0.f, 1.f));
            if (stroke && stroke->inside)
                over(colour, alpha, stroke->color(), strokeCoverage);
            out[x * 4] = byte(colour[0]);
            out[x * 4 + 1] = byte(colour[1]);
            out[x * 4 + 2] = byte(colour[2]);
            out[x * 4 + 3] = byte(alpha);
        }
    });
    return result;
}
