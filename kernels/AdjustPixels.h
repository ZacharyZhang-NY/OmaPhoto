#ifndef AdjustPixels_h
#define AdjustPixels_h
#include <stdint.h>
#include <stddef.h>
// Gradient Map on premultiplied RGBA pixels (4 bytes per pixel, `stride` bytes per row): each pixel's
// luminance picks a color from `table` (256 × 3 straight sRGB bytes, darkest first). Alpha is kept and
// fully transparent pixels are left alone.
void adjust_gradient_map(uint8_t *rgba, size_t width, size_t height, size_t stride, const uint8_t *table);
// Film grain on premultiplied RGBA pixels: the same brightness change on all three channels, strongest
// in the midtones. `amount` is 0–100, `size` the grain's scale in document units, and `roughness`
// (0–100) mixes in per-unit noise. Pixel (x, y) sits at (originX + (x + 0.5) × unitsPerPixel,
// originY + (y + 0.5) × unitsPerPixel), and its grain depends only on that position and `seed`, so a
// piece of an image gets the same grain as that part of the whole.
void adjust_grain(uint8_t *rgba, size_t width, size_t height, size_t stride, double amount, double size,
                  double roughness, uint32_t seed, double originX, double originY, double unitsPerPixel);
// After resampling with a filter that rings (Lanczos), premultiplied RGBA colors can exceed their alpha;
// this clamps each channel back to its pixel's alpha. `count` is the number of pixels.
void rgba_clamp_premultiplied(uint8_t *rgba, size_t count);
#endif
