#include "AdjustPixels.h"
#include <math.h>

void adjust_gradient_map(uint8_t *rgba, size_t width, size_t height, size_t stride, const uint8_t *table) {
    for (size_t y = 0; y < height; y++) {
        uint8_t *p = rgba + y * stride;
        for (size_t x = 0; x < width; x++, p += 4) {
            unsigned a = p[3];
            if (a == 0) continue;
            unsigned r = p[0], g = p[1], b = p[2];
            if (a < 255) {
                r = (r * 255u + a / 2) / a;
                g = (g * 255u + a / 2) / a;
                b = (b * 255u + a / 2) / a;
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;
            }
            unsigned level = (2126u * r + 7152u * g + 722u * b + 5000u) / 10000u;
            const uint8_t *color = table + (level > 255 ? 255 : level) * 3;
            p[0] = (uint8_t)((color[0] * a + 127u) / 255u);
            p[1] = (uint8_t)((color[1] * a + 127u) / 255u);
            p[2] = (uint8_t)((color[2] * a + 127u) / 255u);
        }
    }
}

static inline uint32_t mix32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// A value in −1…1 for an integer lattice point, fixed by the point and the seed. Two uniform halves
// summed give a triangular spread, closer to film grain than flat noise.
static inline float lattice(int64_t ix, int64_t iy, uint32_t seed) {
    uint32_t h = mix32((uint32_t)ix * 0x9E3779B1U ^ mix32((uint32_t)iy * 0x85EBCA77U ^ seed));
    return (float)(h & 0xFFFFU) / 65535.0f + (float)(h >> 16) / 65535.0f - 1.0f;
}

static inline float clamp255(float value) { return value < 0 ? 0 : value > 255 ? 255 : value; }

void adjust_grain(uint8_t *rgba, size_t width, size_t height, size_t stride, double amount, double size,
                  double roughness, uint32_t seed, double originX, double originY, double unitsPerPixel) {
    if (!(amount > 0) || !(unitsPerPixel > 0)) return;
    if (!(size > 0)) size = 1;
    float strength = (float)(amount > 100 ? 1.0 : amount / 100.0) * 0.35f * 255.0f;
    float rough = (float)(roughness < 0 ? 0.0 : roughness > 100 ? 1.0 : roughness / 100.0);
    uint32_t fineSeed = mix32(seed ^ 0xA511E9B3U);
    for (size_t y = 0; y < height; y++) {
        double v = originY + ((double)y + 0.5) * unitsPerPixel;
        double cellY = floor(v / size);
        float ty = (float)(v / size - cellY);
        ty = ty * ty * (3.0f - 2.0f * ty);
        int64_t iy = (int64_t)cellY, fineY = (int64_t)floor(v);
        uint8_t *p = rgba + y * stride;
        for (size_t x = 0; x < width; x++, p += 4) {
            unsigned a = p[3];
            if (a == 0) continue;
            double u = originX + ((double)x + 0.5) * unitsPerPixel;
            double cellX = floor(u / size);
            float tx = (float)(u / size - cellX);
            tx = tx * tx * (3.0f - 2.0f * tx);
            int64_t ix = (int64_t)cellX;
            float n00 = lattice(ix, iy, seed), n10 = lattice(ix + 1, iy, seed);
            float n01 = lattice(ix, iy + 1, seed), n11 = lattice(ix + 1, iy + 1, seed);
            float top = n00 + (n10 - n00) * tx, bottom = n01 + (n11 - n01) * tx;
            // Blending neighbors narrows the spread; scaling restores about the lattice's own.
            float smooth = (top + (bottom - top) * ty) * 1.6f;
            float fine = lattice((int64_t)floor(u), fineY, fineSeed);
            float noise = smooth + (fine - smooth) * rough;
            float unpremultiply = a == 255 ? 1.0f : 255.0f / (float)a;
            float r = p[0] * unpremultiply, g = p[1] * unpremultiply, b = p[2] * unpremultiply;
            float level = (0.2126f * r + 0.7152f * g + 0.0722f * b) / 255.0f;
            if (level > 1) level = 1;
            // Film grain shows most in the midtones.
            float delta = noise * strength * (0.4f + 2.4f * level * (1.0f - level));
            float coverage = (float)a / 255.0f;
            p[0] = (uint8_t)(clamp255(r + delta) * coverage + 0.5f);
            p[1] = (uint8_t)(clamp255(g + delta) * coverage + 0.5f);
            p[2] = (uint8_t)(clamp255(b + delta) * coverage + 0.5f);
        }
    }
}

void rgba_clamp_premultiplied(uint8_t *rgba, size_t count) {
    for (size_t i = 0; i < count; i++, rgba += 4) {
        uint8_t a = rgba[3];
        if (rgba[0] > a) rgba[0] = a;
        if (rgba[1] > a) rgba[1] = a;
        if (rgba[2] > a) rgba[2] = a;
    }
}

/// Which of the six ranges a color's primary and secondary fall in, and how much of each it holds.
/// A color is min(r,g,b) of gray, plus (mid-min) of the secondary between its two brightest channels,
/// plus (max-mid) of the primary of its brightest — so the weights below are exactly Photoshop's.
void adjust_black_white(uint8_t *rgba, size_t width, size_t height, size_t stride, const float *weights,
                        int tint, double tintHue, double tintSaturation) {
    for (size_t y = 0; y < height; ++y) {
        uint8_t *row = rgba + y * stride;
        for (size_t x = 0; x < width; ++x) {
            uint8_t *p = row + x * 4;
            float alpha = p[3];
            if (!alpha) continue;
            float r = p[0] * 255.0f / alpha, g = p[1] * 255.0f / alpha, b = p[2] * 255.0f / alpha;
            r = fminf(255.0f, r) / 255.0f; g = fminf(255.0f, g) / 255.0f; b = fminf(255.0f, b) / 255.0f;
            float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b));
            float md = r + g + b - mx - mn;
            // weights: 0 red, 1 yellow, 2 green, 3 cyan, 4 blue, 5 magenta
            int primary, secondary;
            if (mx == r)      { primary = 0; secondary = (g >= b) ? 1 : 5; }
            else if (mx == g) { primary = 2; secondary = (r >= b) ? 1 : 3; }
            else              { primary = 4; secondary = (g >= r) ? 3 : 5; }
            float gray = mn + (md - mn) * weights[secondary] + (mx - md) * weights[primary];
            gray = fminf(1.0f, fmaxf(0.0f, gray));
            float outR = gray, outG = gray, outB = gray;
            if (tint && tintSaturation > 0) {
                // The gray becomes the lightness of a color at the chosen hue.
                double c = (1.0 - fabs(2.0 * gray - 1.0)) * tintSaturation;
                double hp = fmod(tintHue, 360.0) / 60.0;
                double xx = c * (1.0 - fabs(fmod(hp, 2.0) - 1.0));
                double r1 = 0, g1 = 0, b1 = 0;
                if (hp < 1)      { r1 = c; g1 = xx; }
                else if (hp < 2) { r1 = xx; g1 = c; }
                else if (hp < 3) { g1 = c; b1 = xx; }
                else if (hp < 4) { g1 = xx; b1 = c; }
                else if (hp < 5) { r1 = xx; b1 = c; }
                else             { r1 = c; b1 = xx; }
                double m = gray - c / 2.0;
                outR = (float)fmin(1.0, fmax(0.0, r1 + m));
                outG = (float)fmin(1.0, fmax(0.0, g1 + m));
                outB = (float)fmin(1.0, fmax(0.0, b1 + m));
            }
            p[0] = (uint8_t)fminf(alpha, fmaxf(0.0f, roundf(outR * alpha)));
            p[1] = (uint8_t)fminf(alpha, fmaxf(0.0f, roundf(outG * alpha)));
            p[2] = (uint8_t)fminf(alpha, fmaxf(0.0f, roundf(outB * alpha)));
        }
    }
}

/// How much a tone belongs to the shadows, midtones and highlights: three overlapping curves that sum
/// to about one across the range, so a shift fades in and out rather than banding at a threshold.
static void tonal_weights(float v, float *shadow, float *mid, float *highlight) {
    const float a = 0.25f, b = 0.333f, scale = 0.7f;
    float s = (v - b) / -a + 0.5f;
    float h = (v + b - 1.0f) / a + 0.5f;
    s = fminf(1.0f, fmaxf(0.0f, s));
    h = fminf(1.0f, fmaxf(0.0f, h));
    float m1 = fminf(1.0f, fmaxf(0.0f, (v - b) / a + 0.5f));
    float m2 = fminf(1.0f, fmaxf(0.0f, (v + b - 1.0f) / -a + 0.5f));
    *shadow = s * scale;
    *mid = m1 * m2 * scale;
    *highlight = h * scale;
}

void adjust_color_balance(uint8_t *rgba, size_t width, size_t height, size_t stride, const float *shadows,
                          const float *midtones, const float *highlights, int preserveLuminosity) {
    for (size_t y = 0; y < height; ++y) {
        uint8_t *row = rgba + y * stride;
        for (size_t x = 0; x < width; ++x) {
            uint8_t *p = row + x * 4;
            float alpha = p[3];
            if (!alpha) continue;
            float c[3];
            for (int i = 0; i < 3; ++i) c[i] = fminf(255.0f, p[i] * 255.0f / alpha) / 255.0f;
            float before = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
            for (int i = 0; i < 3; ++i) {
                float s, m, h;
                tonal_weights(c[i], &s, &m, &h);
                c[i] += shadows[i] * s + midtones[i] * m + highlights[i] * h;
                c[i] = fminf(1.0f, fmaxf(0.0f, c[i]));
            }
            if (preserveLuminosity) {
                float after = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
                if (after > 0.0001f) {
                    float ratio = before / after;
                    for (int i = 0; i < 3; ++i) c[i] = fminf(1.0f, fmaxf(0.0f, c[i] * ratio));
                }
            }
            for (int i = 0; i < 3; ++i) p[i] = (uint8_t)fminf(alpha, fmaxf(0.0f, roundf(c[i] * alpha)));
        }
    }
}
