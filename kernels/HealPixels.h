#ifndef HealPixels_h
#define HealPixels_h
#include <stdint.h>
#include <stddef.h>
// Half-open bounds of nonzero bytes in a gray bitmap; all zero when empty.
void heal_coverage_bounds(const uint8_t *gray, size_t width, size_t height, size_t stride, long bounds[4]);
// Spot healing, in place, over premultiplied RGBA (4 bytes per pixel, `stride` bytes per row).
// `coverage` (width * height bytes, 0–255) marks what to heal.
//   mode 0, Content-Aware: copies texture from the nearby patch whose surrounding ring of pixels
//           best matches the ring around the spot;
//   mode 1, Create Texture: fills smoothly from the spot's edges and adds grain matching the
//           detail around it;
//   mode 2, Proximity Match: like 0, but takes the closest good patch.
// Copied texture is blended so it meets the surrounding tone exactly: the difference along the
// spot's edge is spread smoothly across it (a membrane fill). The result replaces the original
// by coverage × opacity. Returns 0, or -1 when memory runs out.
int spot_heal(uint8_t *rgba, const uint8_t *coverage, size_t width, size_t height, size_t stride,
              float opacity, int mode, uint32_t seed);
#endif
