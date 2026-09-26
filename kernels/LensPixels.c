#include "LensPixels.h"
#include <math.h>

void lens_distort(const uint8_t *source, uint8_t *destination, size_t width, size_t height, size_t stride, double k) {
    double cx = width * 0.5, cy = height * 0.5;
    double halfDiagonal2 = cx * cx + cy * cy;
    for (size_t y = 0; y < height; ++y) {
        double dy = y + 0.5 - cy;
        uint8_t *out = destination + y * stride;
        for (size_t x = 0; x < width; ++x) {
            double dx = x + 0.5 - cx;
            double scale = 1.0 - k * (dx * dx + dy * dy) / halfDiagonal2;
            // Source position in pixel-center coordinates.
            double sx = cx + dx * scale - 0.5, sy = cy + dy * scale - 0.5;
            double fx0 = floor(sx), fy0 = floor(sy);
            double fx = sx - fx0, fy = sy - fy0;
            long x0 = (long)fx0, y0 = (long)fy0;
            double sums[4] = {0, 0, 0, 0};
            for (int j = 0; j < 2; ++j) {
                long row = y0 + j;
                if (row < 0 || row >= (long)height) continue;
                double wy = j ? fy : 1 - fy;
                if (wy == 0) continue;
                const uint8_t *line = source + (size_t)row * stride;
                for (int i = 0; i < 2; ++i) {
                    long column = x0 + i;
                    if (column < 0 || column >= (long)width) continue;
                    double weight = wy * (i ? fx : 1 - fx);
                    if (weight == 0) continue;
                    const uint8_t *p = line + (size_t)column * 4;
                    for (int c = 0; c < 4; ++c) sums[c] += weight * p[c];
                }
            }
            for (int c = 0; c < 4; ++c) out[x * 4 + c] = (uint8_t)lround(sums[c]);
        }
    }
}
