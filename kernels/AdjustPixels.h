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
// Black & White on premultiplied RGBA pixels, the way Photoshop's is: a color is split into the gray
// it contains, the secondary (cyan/magenta/yellow) between its two brightest channels, and the primary
// (red/green/blue) of its brightest, and each of those six ranges has its own weight. `weights` is six
// floats in the order red, yellow, green, cyan, blue, magenta, as fractions (Photoshop's 40% is 0.4).
// Pure red at the default 40% comes out 40% gray, as it does there. With `tint`, the result is colored
// at `tintHue` degrees and `tintSaturation` (0–1) while keeping that gray as its lightness.
void adjust_black_white(uint8_t *rgba, size_t width, size_t height, size_t stride, const float *weights,
                        int tint, double tintHue, double tintSaturation);
// Color Balance on premultiplied RGBA pixels. `shadows`, `midtones` and `highlights` are each three
// floats — cyan/red, magenta/green, yellow/blue — from -1 to 1 (Photoshop's -100 to 100). Each pixel is
// shifted by however much it belongs to each tonal range, and with `preserveLuminosity` its original
// brightness is put back afterwards, so only the color moves.
void adjust_color_balance(uint8_t *rgba, size_t width, size_t height, size_t stride, const float *shadows,
                          const float *midtones, const float *highlights, int preserveLuminosity);
// After resampling with a filter that rings (Lanczos), premultiplied RGBA colors can exceed their alpha;
// this clamps each channel back to its pixel's alpha. `count` is the number of pixels.
void rgba_clamp_premultiplied(uint8_t *rgba, size_t count);
#endif
