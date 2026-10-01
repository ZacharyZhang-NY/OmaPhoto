#include "DitherPixels.h"
#include <math.h>
#include <stdlib.h>
#include <stdatomic.h>

// Linux: GCD's dispatch_apply, run on QtConcurrent by Document/Dither.cpp. Blocks become functions with a context.
void dither_bands(size_t bands, void (*band)(void *context, size_t index), void *context);

static inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

typedef void (*band_body)(void *context, size_t start, size_t end);
typedef struct { size_t count, size; band_body body; void *context; } band_job;

static void run_band(void *context, size_t band) {
    const band_job *job = context;
    size_t count = job->count, size = job->size;
    size_t start = band * size, end = start + size < count ? start + size : count;
    if (start < end) job->body(job->context, start, end);
}

// Runs `body` over `count` items split into a few runs per core, each a (start, end) range, all at once.
static void in_bands(size_t count, band_body body, void *context) {
    size_t bands = count < 64 ? 1 : 32, size = (count + bands - 1) / bands;
    band_job job = { count, size, body, context };
    dither_bands(bands, run_band, &job);
}

// Density darkens (positive) or lightens as a gamma, so black and white stay put; contrast pivots on mid gray.
static inline float adjust_tone(float v, float gamma, float contrast) {
    v = powf(clamp01(v), gamma);
    return clamp01((v - 0.5f) * contrast + 0.5f);
}

// One error-diffusion kernel: neighbors to the right on this row and below, with their weights over `divisor`.
typedef struct { int dx, dy, weight; } Tap;
typedef struct { const Tap *taps; int count; float divisor; } Kernel;

static const Tap atkinson[] = { {1,0,1}, {2,0,1}, {-1,1,1}, {0,1,1}, {1,1,1}, {0,2,1} };
static const Tap floyd[] = { {1,0,7}, {-1,1,3}, {0,1,5}, {1,1,1} };

// Atkinson passes on only six eighths of the error, which is what gives the Mac's crisp, contrasty look.
static Kernel kernel_for(int style) {
    return style == DITHER_ATKINSON ? (Kernel){ atkinson, 6, 8 } : (Kernel){ floyd, 4, 16 };
}

static inline float quantize(float v, int levels) {
    float steps = (float)(levels - 1);
    return roundf(clamp01(v) * steps) / steps;
}

// Diffuses each plane in serpentine order, so the error's drift doesn't streak to one side.
static void diffuse(float *plane, const uint8_t *alpha, size_t width, size_t height, const DitherParams *p) {
    Kernel k = kernel_for(p->style);
    for (size_t y = 0; y < height; ++y) {
        int reverse = (int)(y & 1);
        for (size_t i = 0; i < width; ++i) {
            size_t x = reverse ? width - 1 - i : i;
            size_t at = y * width + x;
            if (!alpha[at]) continue;
            float old = plane[at], q = quantize(old, p->levels);
            plane[at] = q;
            float error = (old - q) * p->diffusion / k.divisor;
            for (int t = 0; t < k.count; ++t) {
                long nx = (long)x + (reverse ? -k.taps[t].dx : k.taps[t].dx), ny = (long)y + k.taps[t].dy;
                if (nx < 0 || nx >= (long)width || ny >= (long)height) continue;
                plane[(size_t)ny * width + (size_t)nx] += error * (float)k.taps[t].weight;
            }
        }
    }
}

static const uint8_t bayer8[64] = {
     0, 32,  8, 40,  2, 34, 10, 42, 48, 16, 56, 24, 50, 18, 58, 26,
    12, 44,  4, 36, 14, 46,  6, 38, 60, 28, 52, 20, 62, 30, 54, 22,
     3, 35, 11, 43,  1, 33,  9, 41, 51, 19, 59, 27, 49, 17, 57, 25,
    15, 47,  7, 39, 13, 45,  5, 37, 63, 31, 55, 23, 61, 29, 53, 21,
};

// The ordered threshold for a pixel, in [0, 1). Smaller Bayer matrices are the top-left corners of the 8 × 8 one,
// rescaled, which is how the recursive construction nests them.
static inline float ordered_threshold(int style, size_t x, size_t y) {
    switch (style) {
    case DITHER_BAYER_2: { static const uint8_t m[4] = { 0, 2, 3, 1 }; return ((float)m[(y & 1) * 2 + (x & 1)] + 0.5f) / 4; }
    case DITHER_BAYER_4: {
        static const uint8_t m[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
        return ((float)m[(y & 3) * 4 + (x & 3)] + 0.5f) / 16;
    }
    default: return ((float)bayer8[(y & 7) * 8 + (x & 7)] + 0.5f) / 64;
    }
}

static inline float ordered(float v, float threshold, int levels) {
    float steps = (float)(levels - 1);
    float q = floorf(clamp01(v) * steps + threshold);
    return (q > steps ? steps : q) / steps;
}

// How much of a halftone cell a point must be covered by before it's marked, for each screen shape. `u` and `v`
// run from −0.5 to 0.5 across the cell; the shapes grow from its middle as coverage rises.
static inline float spot(int style, float u, float v) {
    float au = fabsf(u), av = fabsf(v);
    switch (style) {
    case DITHER_DOTS: return 3.14159265f * (u * u + v * v);
    case DITHER_LINES: return av * 2;
    default: return au + av;
    }
}

// Old Mac fill patterns, 8 × 8, one byte per row with the leftmost pixel in the top bit, from sparsest to fullest.
static const uint8_t patterns[][8] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x80, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00 },
    { 0x88, 0x00, 0x22, 0x00, 0x88, 0x00, 0x22, 0x00 },
    { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 },
    { 0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x22 },
    { 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00 },
    { 0x11, 0x22, 0x44, 0x88, 0x11, 0x22, 0x44, 0x88 },
    { 0xAA, 0x00, 0xAA, 0x00, 0xAA, 0x00, 0xAA, 0x00 },
    { 0x88, 0x55, 0x22, 0x55, 0x88, 0x55, 0x22, 0x55 },
    { 0xFF, 0x80, 0x80, 0x80, 0xFF, 0x08, 0x08, 0x08 },
    { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
    { 0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81 },
    { 0x77, 0xAA, 0xDD, 0xAA, 0x77, 0xAA, 0xDD, 0xAA },
    { 0xEE, 0xDD, 0xBB, 0x77, 0xEE, 0xDD, 0xBB, 0x77 },
    { 0x77, 0xFF, 0xDD, 0xFF, 0x77, 0xFF, 0xDD, 0xFF },
    { 0x7F, 0xFF, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xFF },
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
};
static const int patternCount = (int)(sizeof patterns / sizeof patterns[0]);

static inline void write_pixel(uint8_t *px, float r, float g, float b) {
    float a = (float)px[3] / 255.0f;
    px[0] = (uint8_t)lroundf(clamp01(r) * a * 255.0f);
    px[1] = (uint8_t)lroundf(clamp01(g) * a * 255.0f);
    px[2] = (uint8_t)lroundf(clamp01(b) * a * 255.0f);
}

typedef struct {
    uint8_t *rgba; size_t stride, width, count; uint8_t *alpha; float *tone, *source; const DitherParams *p;
    float gamma, contrast;
} tone_job;

static void tone_band(void *context, size_t first, size_t last) {
    const tone_job *job = context;
    uint8_t *rgba = job->rgba; size_t stride = job->stride, width = job->width, count = job->count;
    uint8_t *alpha = job->alpha; float *tone = job->tone, *source = job->source; const DitherParams *p = job->p;
    float gamma = job->gamma, contrast = job->contrast;
    for (size_t y = first; y < last; ++y) {
        const uint8_t *row = rgba + y * stride;
        for (size_t x = 0; x < width; ++x) {
            const uint8_t *px = row + x * 4;
            size_t at = y * width + x;
            alpha[at] = px[3];
            float r = 0, g = 0, b = 0;
            if (px[3]) {
                float scale = 1.0f / (float)px[3];
                r = px[0] * scale; g = px[1] * scale; b = px[2] * scale;
            }
            if (p->originalColors) {
                tone[at] = adjust_tone(r, gamma, contrast);
                tone[count + at] = adjust_tone(g, gamma, contrast);
                tone[2 * count + at] = adjust_tone(b, gamma, contrast);
                source[at * 3] = r; source[at * 3 + 1] = g; source[at * 3 + 2] = b;
            } else {
                tone[at] = adjust_tone(0.2126f * r + 0.7152f * g + 0.0722f * b, gamma, contrast);
            }
        }
    }
}

typedef struct {
    uint8_t *rgba; size_t stride, width, height, count; int planes; const uint8_t *alpha; const float *tone;
    const DitherParams *p; size_t spacing; float middle, dots; const float *screen, *phosphor; atomic_int failed;
} scan_job;

static void scan_band(void *context, size_t firstLine, size_t lastLine) {
    scan_job *job = context;
    uint8_t *rgba = job->rgba; size_t stride = job->stride, width = job->width, height = job->height, count = job->count;
    int planes = job->planes; const uint8_t *alpha = job->alpha; const float *tone = job->tone; const DitherParams *p = job->p;
    size_t spacing = job->spacing; float middle = job->middle, dots = job->dots;
    const float *screen = job->screen, *phosphor = job->phosphor;
    float *scan = malloc(width * sizeof(float) * (size_t)planes);
    if (!scan) { atomic_store(&job->failed, 1); return; }
    for (size_t line = firstLine; line < lastLine; ++line) {
        size_t top = line * spacing;
        size_t bottom = top + spacing < height ? top + spacing : height;
        // Wobble: each line is pushed sideways, a slow wave down the screen with a quicker one over it, as a
        // CRT's picture wavers when its sync drifts.
        float wave = sinf((float)line * 0.45f) * 0.7f + sinf((float)line * 1.7f + 1.3f) * 0.3f;
        long shift = lroundf(p->wobble * wave);
        for (size_t x = 0; x < width; ++x) {
            float sum[3] = { 0, 0, 0 }; int n = 0;
            long sx = (long)x - shift;
            if (sx >= 0 && sx < (long)width)
                for (size_t y = top; y < bottom; ++y) {
                    size_t at = y * width + (size_t)sx;
                    if (!alpha[at]) continue;
                    for (int c = 0; c < planes; ++c) sum[c] += tone[(size_t)c * count + at];
                    ++n;
                }
            for (int c = 0; c < planes; ++c) scan[(size_t)c * width + x] = n ? sum[c] / (float)n : 0;
        }
        for (size_t y = top; y < bottom; ++y) {
            uint8_t *row = rgba + y * stride;
            float offset = fabsf((float)(y - top) + 0.5f - middle);
            for (size_t x = 0; x < width; ++x) {
                if (!alpha[y * width + x]) continue;
                // Dots: the line breaks into beads, one every line spacing, each lit in the color at its middle.
                float along = fmodf((float)x + 0.5f, (float)spacing) - middle;
                long centered = lroundf((float)x - along * dots);
                size_t at = centered < 0 ? 0 : (size_t)centered >= width ? width - 1 : (size_t)centered;
                float r, g, b, t;
                if (p->originalColors) {
                    r = scan[at]; g = scan[width + at]; b = scan[2 * width + at];
                    t = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                } else {
                    t = scan[at];
                    r = screen[0] + (phosphor[0] - screen[0]) * t;
                    g = screen[1] + (phosphor[1] - screen[1]) * t;
                    b = screen[2] + (phosphor[2] - screen[2]) * t;
                }
                // The beam is driven brighter than the picture, making up for the dark screen between lines.
                r *= 1.35f; g *= 1.35f; b *= 1.35f;
                // Half the beam's height: a thin line in the shadows, most of the way across in the highlights,
                // always leaving dark screen between lines.
                float beam = middle * (0.2f + 0.5f * sqrtf(clamp01(t)));
                float across = along * dots, distance = sqrtf(offset * offset + across * across);
                float cover = clamp01(beam - distance + 0.5f);
                // Between the lines, the screen: black in Original, else the dark color.
                float br = p->originalColors ? 0 : screen[0], bg = p->originalColors ? 0 : screen[1];
                float bb = p->originalColors ? 0 : screen[2];
                write_pixel(row + x * 4, br + (r - br) * cover, bg + (g - bg) * cover, bb + (b - bb) * cover);
            }
        }
    }
    free(scan);
}

int dither_apply(uint8_t *rgba, size_t width, size_t height, size_t stride, const DitherParams *p) {
    size_t count = width * height;
    if (!count) return 1;
    int planes = p->originalColors ? 3 : 1;
    float *tone = malloc(count * sizeof(float) * (size_t)planes);
    uint8_t *alpha = malloc(count);
    // The image's own colors, unadjusted: halftone dots and glyphs take them in Original mode.
    float *source = p->originalColors ? malloc(count * sizeof(float) * 3) : NULL;
    if (!tone || !alpha || (p->originalColors && !source)) { free(tone); free(alpha); free(source); return 0; }

    float gamma = exp2f(p->density * 1.5f);
    float contrast = p->contrast >= 0 ? 1.0f / (1.0f - 0.95f * p->contrast) : 1.0f + p->contrast;
    tone_job toning = { rgba, stride, width, count, alpha, tone, source, p, gamma, contrast };
    in_bands(height, tone_band, &toning);

    float dark[3] = { p->dark[0] / 255.0f, p->dark[1] / 255.0f, p->dark[2] / 255.0f };
    float light[3] = { p->light[0] / 255.0f, p->light[1] / 255.0f, p->light[2] / 255.0f };
    int style = p->style;
    int levels = p->levels < 2 ? 2 : p->levels > 16 ? 16 : p->levels;

    if (style <= DITHER_BAYER_8) {
        // Diffusion and ordered dithering: each plane is quantized to `levels` tones, then mapped to colors.
        if (style <= DITHER_FLOYD_STEINBERG) {
            DitherParams local = *p;
            local.levels = levels;
            for (int c = 0; c < planes; ++c) diffuse(tone + (size_t)c * count, alpha, width, height, &local);
        } else {
            for (int c = 0; c < planes; ++c) {
                float *plane = tone + (size_t)c * count;
                for (size_t y = 0; y < height; ++y)
                    for (size_t x = 0; x < width; ++x) {
                        size_t at = y * width + x;
                        if (alpha[at]) plane[at] = ordered(plane[at], ordered_threshold(style, x, y), levels);
                    }
            }
        }
        for (size_t y = 0; y < height; ++y) {
            uint8_t *row = rgba + y * stride;
            for (size_t x = 0; x < width; ++x) {
                size_t at = y * width + x;
                if (!alpha[at]) continue;
                if (p->originalColors) {
                    write_pixel(row + x * 4, tone[at], tone[count + at], tone[2 * count + at]);
                } else {
                    float t = tone[at];
                    write_pixel(row + x * 4, dark[0] + (light[0] - dark[0]) * t, dark[1] + (light[1] - dark[1]) * t,
                                dark[2] + (light[2] - dark[2]) * t);
                }
            }
        }
    } else if (style == DITHER_SCANLINES) {
        // A CRT: each line scans the image, its tone along the line the average of the rows it covers. The beam glows
        // brighter and blooms thicker where the picture is light, and the screen between lines stays dark.
        size_t spacing = (size_t)(p->cell < 2 ? 2 : p->cell);
        float middle = (float)spacing / 2, dots = clamp01(p->dots);
        // Each line is drawn on its own, so the lines are shared out across the cores.
        size_t lines = (height + spacing - 1) / spacing;
        const float *screen = dark, *phosphor = light;
        scan_job scanning = { rgba, stride, width, height, count, planes, alpha, tone, p, spacing, middle, dots, screen, phosphor, 0 };
        in_bands(lines, scan_band, &scanning);
        int failed = atomic_load(&scanning.failed);
        if (failed) { free(tone); free(alpha); free(source); return 0; }
    } else {
        // Marks (halftone shapes, patterns, glyphs) cover as much of each spot as the tone calls for. On light, they
        // stand for darkness and are drawn in the dark color; light on dark, the reverse.
        float *marks = p->originalColors ? malloc(count * sizeof(float)) : tone;
        if (!marks) { free(tone); free(alpha); free(source); return 0; }
        if (p->originalColors)
            for (size_t i = 0; i < count; ++i)
                marks[i] = 0.2126f * tone[i] + 0.7152f * tone[count + i] + 0.0722f * tone[2 * count + i];
        int cell = p->cell < 2 ? 2 : p->cell;
        float cosA = cosf(p->angle), sinA = sinf(p->angle);
        float *ink = p->lightOnDark ? light : dark, *paper = p->lightOnDark ? dark : light;
        // Glyphs: each cell shares one, picked from the cell's average tone, worked out once per cell.
        size_t gw = (size_t)(p->glyphWidth < 1 ? 1 : p->glyphWidth), gh = (size_t)(p->glyphHeight < 1 ? 1 : p->glyphHeight);
        size_t columns = (width + gw - 1) / gw, cellRows = (height + gh - 1) / gh;
        int *picked = NULL;
        if (style == DITHER_GLYPHS && p->glyphCount > 0) {
            picked = malloc(columns * cellRows * sizeof(int));
            if (!picked) { if (marks != tone) free(marks); free(tone); free(alpha); free(source); return 0; }
            for (size_t row = 0; row < cellRows; ++row)
                for (size_t column = 0; column < columns; ++column) {
                    float sum = 0; int n = 0;
                    for (size_t yy = row * gh; yy < (row + 1) * gh && yy < height; ++yy)
                        for (size_t xx = column * gw; xx < (column + 1) * gw && xx < width; ++xx) {
                            size_t i = yy * width + xx;
                            if (alpha[i]) { sum += marks[i]; ++n; }
                        }
                    float t = n ? sum / (float)n : 1;
                    float wanted = (p->lightOnDark ? t : 1 - t) * p->glyphCoverage[p->glyphCount - 1];
                    int best = 0;
                    float bestDistance = 2;
                    for (int g = 0; g < p->glyphCount; ++g) {
                        float d = fabsf(p->glyphCoverage[g] - wanted);
                        if (d < bestDistance) { bestDistance = d; best = g; }
                    }
                    picked[row * columns + column] = best;
                }
        }
        // Original colors: marks take the pixel's own color, on black (light on dark) or white.
        float paperOriginal = p->lightOnDark ? 0.0f : 1.0f;
        for (size_t y = 0; y < height; ++y) {
            uint8_t *row = rgba + y * stride;
            for (size_t x = 0; x < width; ++x) {
                size_t at = y * width + x;
                if (!alpha[at]) continue;
                float amount;
                if (picked) {
                    int glyph = picked[(y / gh) * columns + x / gw];
                    amount = p->glyphs[(size_t)glyph * gw * gh + (y % gh) * gw + x % gw] / 255.0f;
                } else if (style == DITHER_PATTERNS) {
                    float t = marks[at];
                    float coverage = p->lightOnDark ? t : 1 - t;
                    int index = (int)lroundf(coverage * (float)(patternCount - 1));
                    amount = (patterns[index][y & 7] >> (7 - (x & 7))) & 1;
                } else {
                    float fx = (float)x + 0.5f, fy = (float)y + 0.5f;
                    float u = (fx * cosA + fy * sinA) / (float)cell, v = (-fx * sinA + fy * cosA) / (float)cell;
                    u -= floorf(u) + 0.5f; v -= floorf(v) + 0.5f;
                    float t = marks[at];
                    amount = (p->lightOnDark ? t : 1 - t) > spot(style, u, v) ? 1 : 0;
                }
                if (p->originalColors) {
                    const float *s = source + at * 3;
                    write_pixel(row + x * 4, paperOriginal + (s[0] - paperOriginal) * amount,
                                paperOriginal + (s[1] - paperOriginal) * amount, paperOriginal + (s[2] - paperOriginal) * amount);
                } else {
                    write_pixel(row + x * 4, paper[0] + (ink[0] - paper[0]) * amount, paper[1] + (ink[1] - paper[1]) * amount,
                                paper[2] + (ink[2] - paper[2]) * amount);
                }
            }
        }
        free(picked);
        if (marks != tone) free(marks);
    }
    free(tone); free(alpha); free(source);
    return 1;
}

void dither_dots(uint8_t *rgba, size_t width, size_t height, size_t stride, int block, const uint8_t *gap) {
    if (block < 2) return;
    float radius = (float)block * 0.42f, middle = (float)block / 2;
    for (size_t y = 0; y < height; ++y) {
        uint8_t *row = rgba + y * stride;
        float dy = (float)(y % (size_t)block) + 0.5f - middle;
        for (size_t x = 0; x < width; ++x) {
            uint8_t *px = row + x * 4;
            if (!px[3]) continue;
            float dx = (float)(x % (size_t)block) + 0.5f - middle;
            float cover = clamp01(radius - sqrtf(dx * dx + dy * dy) + 0.5f);
            if (cover >= 1) continue;
            for (int c = 0; c < 3; ++c)
                px[c] = (uint8_t)lroundf((float)px[c] * cover + (float)gap[c] * (float)px[3] / 255.0f * (1 - cover));
        }
    }
}

typedef struct { uint8_t *rgba; const uint8_t *glow; size_t width, stride; float amount; } glow_job;

static void glow_band(void *context, size_t first, size_t last) {
    const glow_job *job = context;
    uint8_t *rgba = job->rgba; const uint8_t *glow = job->glow; size_t width = job->width, stride = job->stride;
    float amount = job->amount;
    for (size_t y = first; y < last; ++y) {
        uint8_t *row = rgba + y * stride;
        const uint8_t *light = glow + y * stride;
        for (size_t x = 0; x < width * 4; x += 4) {
            float a = row[x + 3];
            for (int c = 0; c < 3; ++c) {
                float v = (float)row[x + c] + (float)light[x + c] * amount * a / 255.0f;
                row[x + c] = (uint8_t)lroundf(v > a ? a : v);
            }
        }
    }
}

void dither_glow(uint8_t *rgba, const uint8_t *glow, size_t width, size_t height, size_t stride, float amount) {
    glow_job glowing = { rgba, glow, width, stride, amount };
    in_bands(height, glow_band, &glowing);
}
