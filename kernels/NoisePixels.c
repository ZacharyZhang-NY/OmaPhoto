#include "NoisePixels.h"
#include <math.h>

// A well-mixed 32-bit hash, so neighbouring pixels get unrelated values.
static inline uint32_t noise_hash(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// Uniform in [0, 1).
static inline float noise_unit(uint32_t key) { return (float)(noise_hash(key) >> 8) * (1.0f / 16777216.0f); }

void noise_add(uint8_t *rgba, size_t width, size_t height, size_t stride,
               float amount, int gaussian, int monochromatic, uint32_t seed) {
    float spread = amount / 100.0f * 127.5f;
    for (size_t y = 0; y < height; ++y) {
        uint8_t *row = rgba + y * stride;
        for (size_t x = 0; x < width; ++x) {
            uint8_t *p = row + x * 4;
            unsigned alpha = p[3];
            if (!alpha) continue;
            uint32_t base = noise_hash(seed ^ noise_hash((uint32_t)(y * width + x)));
            for (int c = 0; c < 3; ++c) {
                uint32_t key = monochromatic ? base : base + (uint32_t)c * 0x9e3779b9U;
                float n;
                if (gaussian) {
                    // Box–Muller: two uniform values make one normally distributed one.
                    float u1 = noise_unit(key), u2 = noise_unit(key ^ 0x68e31da4U);
                    n = sqrtf(-2.0f * logf(1.0f - u1)) * cosf(6.2831853f * u2) * spread * (2.0f / 3.0f);
                } else {
                    n = (noise_unit(key) * 2.0f - 1.0f) * spread;
                }
                float value = (float)p[c] * 255.0f / (float)alpha + n;
                value = value < 0 ? 0 : value > 255 ? 255 : value;
                p[c] = (uint8_t)lroundf(value * (float)alpha / 255.0f);
            }
        }
    }
}
