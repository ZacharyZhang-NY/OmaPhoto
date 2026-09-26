#include "HealPixels.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { OUTSIDE = 0, RING = 1, HOLE = 2 };

void heal_coverage_bounds(const uint8_t *gray, size_t width, size_t height, size_t stride, long bounds[4]) {
    long x0 = (long)width, y0 = (long)height, x1 = 0, y1 = 0;
    for (size_t y = 0; y < height; ++y) {
        const uint8_t *row = gray + y * stride;
        for (size_t x = 0; x < width; ++x) {
            if (!row[x]) continue;
            if ((long)x < x0) x0 = (long)x;
            if ((long)x + 1 > x1) x1 = (long)x + 1;
            if ((long)y < y0) y0 = (long)y;
            if ((long)y + 1 > y1) y1 = (long)y + 1;
        }
    }
    if (x1 <= x0 || y1 <= y0) x0 = y0 = x1 = y1 = 0;
    bounds[0] = x0; bounds[1] = y0; bounds[2] = x1; bounds[3] = y1;
}

static inline uint32_t heal_hash(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
static inline double heal_unit(uint32_t key) { return (double)(heal_hash(key) >> 8) / 16777216.0; }

// Mean squared difference between the ring around the spot and the ring around the patch
// offset by (dx, dy). Infinite when the patch would overlap the spot or leave the image.
static double heal_score(const uint8_t *rgba, size_t stride, const uint8_t *role, long wx0, long wy0,
                         long ww, long wh, long dx, long dy, long W, long H) {
    if (labs(dx) < ww && labs(dy) < wh) return INFINITY;
    if (wx0 + dx < 0 || wy0 + dy < 0 || wx0 + ww + dx > W || wy0 + wh + dy > H) return INFINITY;
    double sum = 0;
    long n = 0;
    for (long y = 0; y < wh; ++y) {
        for (long x = 0; x < ww; ++x) {
            if (role[y * ww + x] != RING) continue;
            const uint8_t *t = rgba + (size_t)(wy0 + y) * stride + (size_t)(wx0 + x) * 4;
            const uint8_t *s = rgba + (size_t)(wy0 + y + dy) * stride + (size_t)(wx0 + x + dx) * 4;
            for (int c = 0; c < 4; ++c) { double d = (double)t[c] - s[c]; sum += d * d; }
            ++n;
        }
    }
    return n ? sum / n : INFINITY;
}

// Solves for smooth values over HOLE pixels, fixed to the RING values around them. A coarser
// copy is solved first and used as the starting point, so large spots settle in few passes.
static void heal_solve(float *value, const uint8_t *role, long w, long h, int depth) {
    int iterations = 300;
    if (w > 32 && h > 32 && depth < 16) {
        long cw = (w + 1) / 2, ch = (h + 1) / 2;
        float *coarse = calloc((size_t)(cw * ch * 4), sizeof(float));
        uint8_t *coarseRole = calloc((size_t)(cw * ch), 1);
        if (coarse && coarseRole) {
            for (long y = 0; y < ch; ++y) {
                for (long x = 0; x < cw; ++x) {
                    int known = 0, hole = 0;
                    float knownSum[4] = {0, 0, 0, 0}, holeSum[4] = {0, 0, 0, 0};
                    for (long j = 0; j < 2; ++j) {
                        for (long i = 0; i < 2; ++i) {
                            long fx = x * 2 + i, fy = y * 2 + j;
                            if (fx >= w || fy >= h) continue;
                            long p = fy * w + fx;
                            if (role[p] == RING) { ++known; for (int c = 0; c < 4; ++c) knownSum[c] += value[p * 4 + c]; }
                            else if (role[p] == HOLE) { ++hole; for (int c = 0; c < 4; ++c) holeSum[c] += value[p * 4 + c]; }
                        }
                    }
                    long q = y * cw + x;
                    if (known) { coarseRole[q] = RING; for (int c = 0; c < 4; ++c) coarse[q * 4 + c] = knownSum[c] / known; }
                    else if (hole) { coarseRole[q] = HOLE; for (int c = 0; c < 4; ++c) coarse[q * 4 + c] = holeSum[c] / hole; }
                }
            }
            heal_solve(coarse, coarseRole, cw, ch, depth + 1);
            for (long y = 0; y < h; ++y) {
                for (long x = 0; x < w; ++x) {
                    long p = y * w + x, q = (y / 2) * cw + x / 2;
                    if (role[p] == HOLE && coarseRole[q] == HOLE) memcpy(value + p * 4, coarse + q * 4, 4 * sizeof(float));
                }
            }
            iterations = 40;
        }
        free(coarse);
        free(coarseRole);
    }
    const float omega = 1.8f;
    for (int it = 0; it < iterations; ++it) {
        for (long y = 0; y < h; ++y) {
            for (long x = 0; x < w; ++x) {
                long p = y * w + x;
                if (role[p] != HOLE) continue;
                float sum[4] = {0, 0, 0, 0};
                int n = 0;
                long neighbors[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
                for (int k = 0; k < 4; ++k) {
                    long nx = neighbors[k][0], ny = neighbors[k][1];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    long q = ny * w + nx;
                    if (role[q] == OUTSIDE) continue;
                    for (int c = 0; c < 4; ++c) sum[c] += value[q * 4 + c];
                    ++n;
                }
                if (!n) continue;
                for (int c = 0; c < 4; ++c) value[p * 4 + c] += omega * (sum[c] / n - value[p * 4 + c]);
            }
        }
    }
}

int spot_heal(uint8_t *rgba, const uint8_t *coverage, size_t width, size_t height, size_t stride,
              float opacity, int mode, uint32_t seed) {
    long W = (long)width, H = (long)height;
    long bounds[4];
    heal_coverage_bounds(coverage, width, height, width, bounds);
    if (bounds[2] <= bounds[0]) return 0;
    long bw = bounds[2] - bounds[0], bh = bounds[3] - bounds[1], size = bw > bh ? bw : bh;
    long ring = size / 8;
    if (ring < 2) ring = 2;
    if (ring > 16) ring = 16;
    // Work box: the spot plus its ring, clipped to the image.
    long wx0 = bounds[0] - ring < 0 ? 0 : bounds[0] - ring, wy0 = bounds[1] - ring < 0 ? 0 : bounds[1] - ring;
    long wx1 = bounds[2] + ring > W ? W : bounds[2] + ring, wy1 = bounds[3] + ring > H ? H : bounds[3] + ring;
    long ww = wx1 - wx0, wh = wy1 - wy0, wn = ww * wh;

    uint8_t *role = calloc((size_t)wn, 1), *near = calloc((size_t)wn, 1);
    long *prefix = malloc((size_t)((ww > wh ? ww : wh) + 1) * sizeof(long));
    float *value = malloc((size_t)wn * 4 * sizeof(float));
    int status = -1;
    if (!role || !near || !prefix || !value) goto done;
    for (long y = 0; y < wh; ++y)
        for (long x = 0; x < ww; ++x)
            role[y * ww + x] = coverage[(size_t)(wy0 + y) * width + (size_t)(wx0 + x)] ? HOLE : OUTSIDE;
    // The ring: pixels within `ring` of the spot (a square dilation, row pass then column pass).
    for (long y = 0; y < wh; ++y) {
        prefix[0] = 0;
        for (long x = 0; x < ww; ++x) prefix[x + 1] = prefix[x] + (role[y * ww + x] == HOLE);
        for (long x = 0; x < ww; ++x) {
            long lo = x - ring < 0 ? 0 : x - ring, hi = x + ring + 1 > ww ? ww : x + ring + 1;
            near[y * ww + x] = prefix[hi] - prefix[lo] > 0;
        }
    }
    for (long x = 0; x < ww; ++x) {
        prefix[0] = 0;
        for (long y = 0; y < wh; ++y) prefix[y + 1] = prefix[y] + near[y * ww + x];
        for (long y = 0; y < wh; ++y) {
            long lo = y - ring < 0 ? 0 : y - ring, hi = y + ring + 1 > wh ? wh : y + ring + 1;
            if (role[y * ww + x] == OUTSIDE && prefix[hi] - prefix[lo] > 0) role[y * ww + x] = RING;
        }
    }
    long ringCount = 0;
    for (long p = 0; p < wn; ++p) ringCount += role[p] == RING;
    if (!ringCount) { status = 0; goto done; }

    // Source patch for Content-Aware and Proximity Match.
    long ox = 0, oy = 0;
    int haveSource = 0;
    if (mode != 1) {
        static const double factors[5] = {1.05, 1.35, 1.75, 2.25, 2.8};
        int count = mode == 2 ? 2 : 5;
        double best = INFINITY;
        for (int f = 0; f < count; ++f) {
            for (int a = 0; a < 24; ++a) {
                double angle = a * M_PI / 12.0;
                long dx = lround(cos(angle) * factors[f] * ww), dy = lround(sin(angle) * factors[f] * wh);
                double score = heal_score(rgba, stride, role, wx0, wy0, ww, wh, dx, dy, W, H);
                if (!isfinite(score)) continue;
                score *= mode == 2 ? 1.0 + 0.6 * f : 1.0 + 0.1 * f; // nearer patches win ties
                if (score < best) { best = score; ox = dx; oy = dy; }
            }
        }
        if (isfinite(best)) {
            // Fine-tune the alignment so repeating texture lines up.
            long cx = ox, cy = oy;
            double refined = heal_score(rgba, stride, role, wx0, wy0, ww, wh, cx, cy, W, H);
            for (long j = -3; j <= 3; ++j) {
                for (long i = -3; i <= 3; ++i) {
                    double score = heal_score(rgba, stride, role, wx0, wy0, ww, wh, cx + i, cy + j, W, H);
                    if (score < refined) { refined = score; ox = cx + i; oy = cy + j; }
                }
            }
            haveSource = 1;
        }
    }

    // Membrane: the edge difference between the original and the patch (or the original itself
    // for a smooth fill), spread across the spot.
    double mean[4] = {0, 0, 0, 0}, detail[3] = {0, 0, 0};
    for (long y = 0; y < wh; ++y) {
        for (long x = 0; x < ww; ++x) {
            long p = y * ww + x;
            if (role[p] != RING) { memset(value + p * 4, 0, 4 * sizeof(float)); continue; }
            long ix = wx0 + x, iy = wy0 + y;
            const uint8_t *t = rgba + (size_t)iy * stride + (size_t)ix * 4;
            const uint8_t *s = haveSource ? rgba + (size_t)(iy + oy) * stride + (size_t)(ix + ox) * 4 : NULL;
            for (int c = 0; c < 4; ++c) {
                value[p * 4 + c] = (float)t[c] - (s ? s[c] : 0);
                mean[c] += value[p * 4 + c];
            }
            if (!haveSource) {
                // Fine detail around the spot: each pixel against the average of its neighbours.
                for (int c = 0; c < 3; ++c) {
                    double around = 0; int n = 0;
                    long offsets[4][2] = {{ix - 1, iy}, {ix + 1, iy}, {ix, iy - 1}, {ix, iy + 1}};
                    for (int k = 0; k < 4; ++k) {
                        if (offsets[k][0] < 0 || offsets[k][1] < 0 || offsets[k][0] >= W || offsets[k][1] >= H) continue;
                        around += rgba[(size_t)offsets[k][1] * stride + (size_t)offsets[k][0] * 4 + c];
                        ++n;
                    }
                    if (n) { double d = t[c] - around / n; detail[c] += d * d; }
                }
            }
        }
    }
    for (int c = 0; c < 4; ++c) mean[c] /= ringCount;
    for (long p = 0; p < wn; ++p)
        if (role[p] == HOLE) for (int c = 0; c < 4; ++c) value[p * 4 + c] = (float)mean[c];
    heal_solve(value, role, ww, wh, 0);
    for (int c = 0; c < 3; ++c) detail[c] = sqrt(detail[c] / ringCount) * 0.9;

    for (long y = 0; y < wh; ++y) {
        for (long x = 0; x < ww; ++x) {
            long p = y * ww + x;
            if (role[p] != HOLE) continue;
            long ix = wx0 + x, iy = wy0 + y;
            uint8_t *t = rgba + (size_t)iy * stride + (size_t)ix * 4;
            const uint8_t *s = haveSource ? rgba + (size_t)(iy + oy) * stride + (size_t)(ix + ox) * 4 : NULL;
            double amount = coverage[(size_t)iy * width + (size_t)ix] / 255.0 * opacity;
            double grain = 0;
            if (!haveSource) {
                uint32_t key = heal_hash(seed ^ heal_hash((uint32_t)(iy * W + ix)));
                double u1 = heal_unit(key), u2 = heal_unit(key ^ 0x68e31da4U);
                grain = sqrt(-2.0 * log(1.0 - u1)) * cos(2.0 * M_PI * u2);
            }
            double out[4];
            for (int c = 0; c < 4; ++c) {
                double healed = (s ? s[c] : 0) + value[p * 4 + c] + (c < 3 ? grain * detail[c] : 0);
                out[c] = t[c] + (healed - t[c]) * amount;
            }
            double alpha = out[3] < 0 ? 0 : out[3] > 255 ? 255 : out[3];
            t[3] = (uint8_t)lround(alpha);
            for (int c = 0; c < 3; ++c) {
                double v = out[c] < 0 ? 0 : out[c] > t[3] ? t[3] : out[c];
                t[c] = (uint8_t)lround(v);
            }
        }
    }
    status = 0;
done:
    free(role);
    free(near);
    free(prefix);
    free(value);
    return status;
}
